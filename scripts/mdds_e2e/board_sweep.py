#!/usr/bin/env python3
# Board-side message sweep node for mdds e2e tests (see ../../../docs/designs/mdds_test_plan.md).
#
# Publishes/subscribes std_msgs/ByteMultiArray blocks of increasing payload size.
# Every message carries a 20-byte header: [magic u32][payload_size u32][seq u64]
# [body_crc32 u32]. The subscriber verifies per-size-block continuity
# (loss/reorder) AND full-body integrity (CRC32 over every byte after the
# header) — a bare header-seq check cannot prove byte-level transparency.
#
# Usage (on board, after sourcing env.sh + RMW_IMPLEMENTATION=rmw_mdds):
#   python3.12 board_sweep.py --mode sub [--reliability reliable|best_effort]
#                             [--history keep_last|keep_all] [--depth N]
#                             [--sleep-ms MS] [--idle-timeout S]
#   python3.12 board_sweep.py --mode pub [--sizes 1024,4096,...] [--rate HZ]
#                             [--settle-ms MS] [--reliability ...]
#                             [--history ...] [--depth N]
#
# Exit code: sub mode exits 0 and prints "SWEEP_RESULT PASS" iff the reliability
# contract held for EVERY size block in --sizes (reliable: received == published
# with 0 lost / 0 reorder / 0 crc errors; best_effort: 0 reorder / 0 crc errors
# with at least one reception). A zero-reception block always fails the run.
import argparse
import errno
import math
import os
import re
import stat
import struct
import sys
import time
import zlib

MAGIC = 0x4D444453  # 'MDDS'
HDR = struct.Struct('<IIQI')

DEFAULT_SIZES = [1024, 4096, 65536, 262144, 1048576, 4194304, 8388608]
# A release barrier is a test-orchestration guard, not a production wait.  Keep
# its maximum short enough that a bad control-plane predicate cannot pin a board
# process indefinitely or conceal a dead run behind a practically infinite wait.
MAX_BARRIER_TIMEOUT_S = 120.0


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


def wait_for_barrier_release(release_file, token, timeout_s):
    """Wait for an immutable, exact-token release file.

    The DS-03 runner first proves control-plane readiness on both sides of the
    DSoftBus hop, then commits this file using an atomic hard link.  Treat a
    link, a non-regular file, or a wrong body as a fail-closed test setup error:
    publishing a probe sample would consume a sequence/history slot and turn a
    readiness failure into ambiguous data-plane evidence.

    This helper deliberately has no ROS dependencies so its filesystem contract
    can be unit-tested on the host.
    """
    expected = f'MDDS_SWEEP_RELEASE token={token}\n'.encode('ascii')
    deadline = time.monotonic() + timeout_s
    if not hasattr(os, 'O_NOFOLLOW'):
        # The test must not fall back to a path-based check/open sequence: that
        # would reintroduce a symlink replacement race at the release boundary.
        return False, 'o_nofollow_unavailable'
    if not hasattr(os, 'O_NONBLOCK'):
        # A FIFO can block open(2) before fstat can reject it.  A test release
        # must remain bounded even if an untrusted entry appears at this path.
        return False, 'o_nonblock_unavailable'
    while True:
        try:
            fd = os.open(release_file, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            fd = None
        except OSError as e:
            if e.errno == errno.ELOOP:
                return False, 'symlink'
            return False, f'lstat={e!r}'
        else:
            try:
                if not stat.S_ISREG(os.fstat(fd).st_mode):
                    return False, 'not_regular'
                # Read through the already opened, no-follow descriptor.  Do
                # not reopen by pathname after fstat: the path could change
                # between checks on a shared board filesystem.
                body = os.read(fd, len(expected) + 1)
            except OSError as e:
                return False, f'read={e!r}'
            finally:
                os.close(fd)
            if body == expected:
                return True, 'released'
            return False, 'token_mismatch'
        now = time.monotonic()
        if now >= deadline:
            return False, 'timeout'
        time.sleep(min(0.1, deadline - now))


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
        timeout_s = args.match_timeout_ms / 1000.0
        while rclpy.ok() and pub.get_subscription_count() < 1 and \
                time.monotonic() - m0 < timeout_s:
            rclpy.spin_once(node, timeout_sec=0.1)
        local_subs = pub.get_subscription_count() if rclpy.ok() else 0
        elapsed_ms = int((time.monotonic() - m0) * 1000)
        if local_subs < 1:
            print(f'SWEEP-PUB-NO-MATCH timeout_ms={args.match_timeout_ms} '
                  f'local_subs={local_subs} elapsed_ms={elapsed_ms}', flush=True)
            if rclpy.ok():
                node.destroy_node()
                rclpy.shutdown()
            return 1
        print(f'SWEEP-PUB-MATCHED local_subs={local_subs} elapsed_ms={elapsed_ms}', flush=True)
    if args.barrier_release_file:
        # The local match is necessary but not sufficient: the orchestrator
        # also observes the remote gateway's admitted writer before it commits
        # this release.  No DATA may be constructed or published before that.
        local_subs = pub.get_subscription_count() if rclpy.ok() else 0
        print(f'SWEEP-PUB-BARRIER-READY token={args.barrier_token} '
              f'local_subs={local_subs}', flush=True)
        released, reason = wait_for_barrier_release(
            args.barrier_release_file, args.barrier_token, args.barrier_timeout_s)
        if not released:
            print(f'SWEEP-PUB-BARRIER-FAIL token={args.barrier_token} reason={reason}', flush=True)
            if rclpy.ok():
                node.destroy_node()
                rclpy.shutdown()
            return 1
        print(f'SWEEP-PUB-BARRIER-RELEASED token={args.barrier_token}', flush=True)
    if args.settle_ms:
        # A match proves endpoint discovery, but a just-restarted backend may
        # still be reconnecting its transport session.  Keep this explicit and
        # opt-in: scenario runners use it when they need a steady data plane,
        # rather than turning an initial reconnect burst into a DDS history
        # overflow that obscures the end-to-end assertion.
        print(f'SWEEP-PUB-SETTLE ms={args.settle_ms}', flush=True)
        time.sleep(args.settle_ms / 1000.0)
    sizes = [int(s) for s in args.sizes.split(',')]
    t0 = time.monotonic()
    for size in sizes:
        n = target_count(args, size)
        period = period_for(size, args.rate, args.rate_bps)
        body = bytes(size - HDR.size)
        # body is constant within a block: one CRC covers every seq.
        body_crc = zlib.crc32(body)
        next_t = time.monotonic()
        for seq in range(n):
            next_t += period
            msg = ByteMultiArray()
            msg.data = HDR.pack(MAGIC, size, seq, body_crc) + body
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
    __slots__ = ('received', 'max_seq', 'lost', 'reorder', 'crc_errors',
                 'first_t', 'last_t', 'bytes')

    def __init__(self):
        self.received = 0
        self.max_seq = -1
        self.lost = 0
        self.reorder = 0
        self.crc_errors = 0
        self.first_t = None
        self.last_t = None
        self.bytes = 0


def run_sub(args):
    import rclpy
    from std_msgs.msg import ByteMultiArray
    init_ros(args)
    node = rclpy.create_node('mdds_sweep_sub')
    # Pre-initialize one stats record per EXPECTED size block (--sizes is the
    # set this run's pub side sends). A block that receives nothing must still
    # produce a SWEEP-SUB line and fail the run — iterating only over sizes
    # observed in callbacks would silently skip a dead block (false PASS).
    stats = {size: SizeStats() for size in (int(s) for s in args.sizes.split(','))}
    state = {'last_msg_t': None}

    def cb(msg):
        data = as_bytes(msg.data)
        if len(data) < HDR.size:
            return
        magic, size, seq, body_crc = HDR.unpack_from(data)
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
        # Full-body integrity: any flipped/truncated byte past the header
        # breaks the CRC even when the sequence numbers look fine.
        if zlib.crc32(data[HDR.size:]) != body_crc:
            st.crc_errors += 1
        if seq <= st.max_seq:
            st.reorder += 1
        else:
            missing = seq - st.max_seq - 1
            if missing:
                print(f'SWEEP-SUB-GAP size={size} expected={st.max_seq + 1} got={seq} '
                      f'missing={missing}', flush=True)
            st.lost += missing
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
        block_ok = st.reorder == 0 and st.crc_errors == 0 and st.received >= 1
        if args.reliability == 'reliable':
            # exact per-block accounting: reliable delivery means every
            # published message of the block arrived exactly once — neither
            # loss / tail truncation (received < expected) nor duplicate
            # delivery (received > expected) is acceptable
            block_ok = block_ok and st.lost == 0 and st.received == expected
        ok = ok and block_ok
        print(f'SWEEP-SUB size={size} received={st.received}/{expected} lost={st.lost} '
              f'reorder={st.reorder} crc={st.crc_errors} mbps={mbps:.2f} '
              f'{"OK" if block_ok else "BAD"}',
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
                    help='pub only: require >=1 matched subscription before '
                    'publishing (VOLATILE head-loss guard)')
    ap.add_argument('--match-timeout-ms', type=int, default=10_000,
                    help='pub only: bounded wait for --wait-match (default: 10000)')
    ap.add_argument('--barrier-release-file', default='',
                    help='pub only: exact-token regular file that releases a '
                    'post-match publisher barrier')
    ap.add_argument('--barrier-token', default='',
                    help='pub only: ASCII token required in the barrier release file')
    ap.add_argument('--barrier-timeout-s', type=float, default=0.0,
                    help='pub only: positive release-barrier wait timeout')
    ap.add_argument('--settle-ms', type=int, default=0,
                    help='pub only: bounded post-match delay for transport-session '
                         'stabilization before the first sample')
    ap.add_argument('--flush-ms', type=int, default=2000,
                    help='pub only: linger after the last sample so reliable '
                         'retransmission of the tail can finish')
    args = ap.parse_args()
    if args.settle_ms < 0:
        ap.error('--settle-ms must be >= 0')
    if args.match_timeout_ms < 0:
        ap.error('--match-timeout-ms must be >= 0')
    barrier_args_present = bool(
        args.barrier_release_file or args.barrier_token or args.barrier_timeout_s)
    if barrier_args_present:
        if args.mode != 'pub':
            ap.error('publisher barrier arguments require --mode pub')
        if not args.wait_match:
            ap.error('publisher barrier requires --wait-match')
        if not args.barrier_release_file or not args.barrier_token:
            ap.error('publisher barrier requires --barrier-release-file and --barrier-token')
        if not math.isfinite(args.barrier_timeout_s) or not 0 < args.barrier_timeout_s <= MAX_BARRIER_TIMEOUT_S:
            ap.error('--barrier-timeout-s must be finite and in (0, 120] with publisher barrier')
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,200}', args.barrier_token):
            ap.error('--barrier-token must use 1..200 A-Z a-z 0-9 _ - characters')
    return run_pub(args) if args.mode == 'pub' else run_sub(args)


if __name__ == '__main__':
    sys.exit(main())
