#!/usr/bin/env python3
# Board-side message sweep node for mdds e2e tests (see docs/designs/mdds_test_plan.md).
#
# Publishes/subscribes std_msgs/ByteMultiArray blocks of increasing payload size.
# Every message carries a 16-byte header: [magic u32][payload_size u32][seq u64].
# The subscriber verifies per-size-block continuity (loss/reorder) and throughput.
#
# Usage (on board, after sourcing env.sh + RMW_IMPLEMENTATION=rmw_mdds):
#   python3.12 board_sweep.py --mode sub [--reliability reliable|best_effort]
#                             [--history keep_last|keep_all] [--depth N]
#                             [--sleep-ms MS] [--idle-timeout S]
#   python3.12 board_sweep.py --mode pub [--sizes 1024,4096,...] [--rate HZ]
#                             [--reliability ...] [--history ...] [--depth N]
#
# Exit code: sub mode exits 0 and prints "SWEEP_RESULT PASS" iff the reliability
# contract held (reliable: 0 lost / 0 reorder; best_effort: 0 reorder).
import argparse
import struct
import sys
import time

MAGIC = 0x4D444453  # 'MDDS'
HDR = struct.Struct('<IIQ')

DEFAULT_SIZES = [1024, 4096, 65536, 262144, 1048576, 4194304, 8388608]


def count_for(size):
    # Fewer samples as size grows: enough to exercise fragmentation/repair
    # while keeping the block short at byte-rate-paced periods.
    return max(8, min(300, 12_000_000 // size))


def target_count(args, size):
    # --count overrides the per-size block length (e.g. fixed-rate endurance
    # runs: 100 Hz x 30 s = 3000); the default stays size-scaled.
    return args.count if args.count > 0 else count_for(size)


def period_for(size, base_rate, rate_bps):
    # Pace by BOTH a message-rate cap and an offered-byte-rate cap. The
    # cross-board dsoftbus lane sustains ~2.3 MB/s; an offered load beyond
    # capacity makes reliable repair unwinnable (the >=64-seq receive window
    # forfeits holes once newer samples advance the baseline), so the default
    # sweep must stay well under it.
    return max(1.0 / base_rate, size / rate_bps)


def make_qos(args):
    from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
    return QoSProfile(
        depth=args.depth,
        reliability=ReliabilityPolicy.RELIABLE if args.reliability == 'reliable'
        else ReliabilityPolicy.BEST_EFFORT,
        history=HistoryPolicy.KEEP_LAST if args.history == 'keep_last'
        else HistoryPolicy.KEEP_ALL,
        durability=DurabilityPolicy.VOLATILE,
    )


def init_ros(args):
    import rclpy
    from rclpy.signals import SignalHandlerOptions
    if args.rclpy_signals:
        # parity mode: rclpy's default signal handling (own SIGINT/SIGTERM
        # handlers + SignalHandlerGuardCondition), to prove the sweep behaves
        # identically with them on
        rclpy.init()
        return
    # default for batch testing: no rclpy signal handlers; the orchestrator
    # terminates processes directly and nothing here needs Ctrl-C semantics
    rclpy.init(signal_handler_options=SignalHandlerOptions.NO)
    if getattr(args, 'debug_signals', False):
        import signal

        def _dbg(sig, frame):
            print(f'RECEIVED-SIGNAL {sig}', flush=True)
        signal.signal(signal.SIGTERM, _dbg)
        signal.signal(signal.SIGINT, _dbg)


def run_pub(args):
    import rclpy
    from std_msgs.msg import ByteMultiArray
    init_ros(args)
    node = rclpy.create_node('mdds_sweep_pub')
    pub = node.create_publisher(ByteMultiArray, args.topic, make_qos(args))
    if args.wait_match:
        # VOLATILE durability: samples published before discovery/matching
        # completes are silently dropped by the middleware. Wait (bounded) for
        # at least one matched subscription so block heads are not lost to
        # SEDP/discovery latency.
        m0 = time.monotonic()
        while rclpy.ok() and pub.get_subscription_count() < 1 and \
                time.monotonic() - m0 < 10.0:
            rclpy.spin_once(node, timeout_sec=0.1)
    sizes = [int(s) for s in args.sizes.split(',')]
    t0 = time.monotonic()
    for size in sizes:
        n = target_count(args, size)
        period = period_for(size, args.rate, args.rate_bps)
        body = bytes(size - HDR.size)
        next_t = time.monotonic()
        for seq in range(n):
            next_t += period
            msg = ByteMultiArray()
            msg.data = HDR.pack(MAGIC, size, seq) + body
            try:
                pub.publish(msg)
            except Exception as e:
                print(f'SWEEP-PUB-ERROR size={size} seq={seq} t={time.monotonic()-t0:.2f}s '
                      f'ok={rclpy.ok()} err={e!r}', flush=True)
                raise
            if seq % max(1, n // 5) == 0:
                print(f'SWEEP-PUB size={size} seq={seq}/{n}', flush=True)
            # absolute-deadline pacing: a 64 KB publish costs milliseconds of
            # fragmentation/send, and naive sleep(period) drifts the effective
            # rate well below the nominal one over long runs
            delay = next_t - time.monotonic()
            if delay > 0:
                time.sleep(delay)
        print(f'SWEEP-PUB-DONE size={size} count={n}', flush=True)
    print('SWEEP-PUB-ALL-DONE', flush=True)
    # linger so the reliable writer can finish retransmitting the tail; a fixed
    # 2 s is not enough when the last block is multi-MB over a lossy link
    time.sleep(args.flush_ms / 1000.0)
    node.destroy_node()
    rclpy.shutdown()
    return 0


def as_bytes(d):
    # OHOS rclpy quirk: a received sequence<uint8> comes back as a list of
    # per-element bytes objects (verified identical under cyclonedds); the
    # publish side accepts real bytes. Normalize both shapes here.
    if isinstance(d, bytes):
        return d
    if isinstance(d, (bytearray, memoryview)):
        return bytes(d)
    out = bytearray()
    for e in d:
        if isinstance(e, int):
            out.append(e & 0xFF)
        else:
            out += bytes(e)
    return bytes(out)


class SizeStats:
    __slots__ = ('received', 'max_seq', 'lost', 'reorder', 'first_t', 'last_t', 'bytes')

    def __init__(self):
        self.received = 0
        self.max_seq = -1
        self.lost = 0
        self.reorder = 0
        self.first_t = None
        self.last_t = None
        self.bytes = 0


def run_sub(args):
    import rclpy
    from std_msgs.msg import ByteMultiArray
    init_ros(args)
    node = rclpy.create_node('mdds_sweep_sub')
    stats = {}
    state = {'last_msg_t': None}

    def cb(msg):
        data = as_bytes(msg.data)
        if len(data) < HDR.size:
            return
        magic, size, seq = HDR.unpack_from(data)
        if magic != MAGIC:
            return
        st = stats.setdefault(size, SizeStats())
        now = time.monotonic()
        if st.first_t is None:
            st.first_t = now
        st.last_t = now
        state['last_msg_t'] = now
        st.received += 1
        st.bytes += len(data)
        if seq <= st.max_seq:
            st.reorder += 1
        else:
            st.lost += seq - st.max_seq - 1
            st.max_seq = seq
        if st.received % 60 == 0:
            print(f'SWEEP-SUB-PROGRESS size={size} seq={seq} received={st.received}',
                  flush=True)
        if args.sleep_ms:
            time.sleep(args.sleep_ms / 1000.0)

    node.create_subscription(ByteMultiArray, args.topic, cb, make_qos(args))
    deadline = time.monotonic() + args.idle_timeout
    spin_t0 = time.monotonic()
    try:
        while rclpy.ok():
            rclpy.spin_once(node, timeout_sec=0.2)
            now = time.monotonic()
            if state['last_msg_t'] is not None:
                if now - state['last_msg_t'] > args.idle_timeout:
                    break
            elif now > deadline:
                break
    except Exception as e:
        # report mid-run death (e.g. ExternalShutdownException) with timing and
        # reception progress instead of dying silently
        print(f'SWEEP-SUB-ERROR t={time.monotonic()-spin_t0:.2f}s '
              f'received={ {s: st.received for s, st in stats.items()} } err={e!r}',
              flush=True)

    ok = True
    for size in sorted(stats):
        st = stats[size]
        expected = target_count(args, size)
        dur = (st.last_t - st.first_t) if st.first_t else 0.0
        mbps = (st.bytes / 1e6 / dur) if dur > 0 else 0.0
        block_ok = st.reorder == 0
        if args.reliability == 'reliable':
            # tail truncation counts too: reliable delivery means every
            # published message of the block arrived
            block_ok = block_ok and st.lost == 0 and st.received >= expected
        ok = ok and block_ok
        print(f'SWEEP-SUB size={size} received={st.received}/{expected} lost={st.lost} '
              f'reorder={st.reorder} mbps={mbps:.2f} {"OK" if block_ok else "BAD"}',
              flush=True)
    if not stats:
        ok = False
        print('SWEEP-SUB no messages received', flush=True)
    print(f'SWEEP_RESULT {"PASS" if ok else "FAIL"}', flush=True)
    # the context may already be down (ExternalShutdownException path above);
    # only touch what is still alive
    if rclpy.ok():
        node.destroy_node()
        rclpy.shutdown()
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mode', choices=['pub', 'sub'], required=True)
    ap.add_argument('--topic', default='/mdds_sweep')
    ap.add_argument('--sizes', default=','.join(str(s) for s in DEFAULT_SIZES))
    ap.add_argument('--count', type=int, default=0,
                    help='messages per size block (default: size-scaled, see '
                         'count_for); use for fixed-rate endurance runs')
    ap.add_argument('--rate', type=int, default=50,
                    help='pub only: message-rate cap in Hz')
    ap.add_argument('--rate-bps', type=int, default=1_200_000,
                    help='pub only: offered byte-rate cap in bytes/s; keeps the '
                         'sweep below the cross-board lane capacity (~2.3 MB/s) '
                         'so reliable repair has headroom')
    ap.add_argument('--reliability', choices=['reliable', 'best_effort'], default='reliable')
    ap.add_argument('--history', choices=['keep_last', 'keep_all'], default='keep_last')
    ap.add_argument('--depth', type=int, default=10)
    ap.add_argument('--sleep-ms', type=int, default=0,
                    help='sub only: per-callback sleep to simulate a slow consumer')
    ap.add_argument('--idle-timeout', type=float, default=15.0,
                    help='sub only: stop after this many seconds without messages')
    ap.add_argument('--debug-signals', action='store_true',
                    help='with default (no rclpy handlers) init, install '
                         'print-only OS signal handlers to diagnose unexpected '
                         'context shutdown')
    ap.add_argument('--rclpy-signals', action='store_true',
                    help='use rclpy default signal handlers (parity check)')
    ap.add_argument('--wait-match', action='store_true',
                    help='pub only: wait (<=10s) for >=1 matched subscription '
                         'before publishing (VOLATILE head-loss guard)')
    ap.add_argument('--flush-ms', type=int, default=2000,
                    help='pub only: linger after the last sample so reliable '
                         'retransmission of the tail can finish')
    args = ap.parse_args()
    return run_pub(args) if args.mode == 'pub' else run_sub(args)


if __name__ == '__main__':
    sys.exit(main())
