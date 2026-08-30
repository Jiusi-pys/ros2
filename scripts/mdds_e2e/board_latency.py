#!/usr/bin/env python3
# Ping-pong RTT benchmark for mdds / gateway paths. No clock sync needed:
# the ping side embeds its own monotonic timestamp and measures the round
# trip when the pong side echoes the payload back unchanged.
#
#   ping:  board_latency.py --mode ping --sizes 64,1024 --count 200
#   pong:  board_latency.py --mode pong
#
# Topics: /mdds_lat_req (ping -> pong), /mdds_lat_rsp (pong -> ping).
# Output lines are machine-greppable:
#   LAT-MATCH req_ms=.. rsp_ms=..      (discovery/matching latency, ping only)
#   LAT size=.. n=.. lost=.. min_us=.. mean_us=.. p50_us=.. p95_us=.. p99_us=.. max_us=..
#   LAT_RESULT PASS|FAIL

import argparse
import struct
import sys
import time

MAGIC = b'MDL1'
HDR = struct.Struct('<4sIIq')  # magic, size, seq, ping monotonic_ns
REQ_TOPIC = '/mdds_lat_req'
RSP_TOPIC = '/mdds_lat_rsp'


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
        out += e
    return bytes(out)


def init_ros(args):
    import rclpy
    from rclpy.signals import SignalHandlerOptions
    if args.rclpy_signals:
        rclpy.init()
        return
    rclpy.init(signal_handler_options=SignalHandlerOptions.NO)


def make_qos(args):
    from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
    return QoSProfile(
        depth=args.depth,
        reliability=ReliabilityPolicy.RELIABLE if args.reliability == 'reliable'
        else ReliabilityPolicy.BEST_EFFORT,
        history=HistoryPolicy.KEEP_LAST,
        durability=DurabilityPolicy.VOLATILE)


def wait_match(node, pub=None, sub=None, timeout=10.0):
    import rclpy
    t0 = time.monotonic()
    while rclpy.ok() and time.monotonic() - t0 < timeout:
        ok = True
        if pub is not None:
            ok = ok and pub.get_subscription_count() >= 1
        if sub is not None:
            ok = ok and sub.get_publisher_count() >= 1
        if ok:
            return (time.monotonic() - t0) * 1000.0
        rclpy.spin_once(node, timeout_sec=0.05)
    return -1.0


def run_pong(args):
    import rclpy
    from std_msgs.msg import ByteMultiArray
    init_ros(args)
    node = rclpy.create_node('mdds_lat_pong')
    qos = make_qos(args)
    pub = node.create_publisher(ByteMultiArray, RSP_TOPIC, qos)

    def cb(msg):
        # echo the payload verbatim; the ping side owns the clock
        out = ByteMultiArray()
        out.data = msg.data
        pub.publish(out)

    node.create_subscription(ByteMultiArray, REQ_TOPIC, cb, qos)
    print('LAT-PONG up', flush=True)
    try:
        while rclpy.ok():
            rclpy.spin_once(node, timeout_sec=0.2)
    except Exception as e:
        print(f'LAT-PONG-ERROR err={e!r}', flush=True)
    if rclpy.ok():
        node.destroy_node()
        rclpy.shutdown()
    return 0


def run_ping(args):
    import rclpy
    from std_msgs.msg import ByteMultiArray
    init_ros(args)
    node = rclpy.create_node('mdds_lat_ping')
    qos = make_qos(args)
    pub = node.create_publisher(ByteMultiArray, REQ_TOPIC, qos)
    state = {'echo_seq': -1, 'echo_t': 0.0}

    def cb(msg):
        data = as_bytes(msg.data)
        if len(data) < HDR.size:
            return
        magic, size, seq, ts = HDR.unpack_from(data)
        if magic != MAGIC:
            return
        state['echo_seq'] = seq
        state['echo_t'] = time.monotonic_ns()

    sub = node.create_subscription(ByteMultiArray, RSP_TOPIC, cb, qos)

    # discovery/matching latency: publisher matched + subscriber matched
    m0 = time.monotonic()
    req_ms = wait_match(node, pub=pub, timeout=args.match_timeout)
    rsp_ms = wait_match(node, sub=sub, timeout=args.match_timeout)
    total_ms = (time.monotonic() - m0) * 1000.0
    print(f'LAT-MATCH req_ms={req_ms:.1f} rsp_ms={rsp_ms:.1f} total_ms={total_ms:.1f}',
          flush=True)

    ok_all = True
    for size in [int(s) for s in args.sizes.split(',')]:
        n = args.count if args.count > 0 else (50 if size >= 65536 else 200)
        body = bytes(size - HDR.size)
        rtts = []
        lost = 0
        for seq in range(n):
            sent_ns = time.monotonic_ns()
            msg = ByteMultiArray()
            msg.data = HDR.pack(MAGIC, size, seq, sent_ns) + body
            state['echo_seq'] = -1
            pub.publish(msg)
            t0 = time.monotonic()
            while state['echo_seq'] != seq:
                if time.monotonic() - t0 > args.echo_timeout:
                    lost += 1
                    break
                rclpy.spin_once(node, timeout_sec=0.001)
            if state['echo_seq'] == seq:
                rtts.append((state['echo_t'] - sent_ns) / 1000.0)
            if args.interval_ms > 0:
                time.sleep(args.interval_ms / 1000.0)
        if rtts:
            rtts.sort()
            mean = sum(rtts) / len(rtts)
            p = lambda q: rtts[min(len(rtts) - 1, int(q * len(rtts)))]
            print(f'LAT size={size} n={n} lost={lost} '
                  f'min_us={rtts[0]:.0f} mean_us={mean:.0f} p50_us={p(0.5):.0f} '
                  f'p95_us={p(0.95):.0f} p99_us={p(0.99):.0f} max_us={rtts[-1]:.0f}',
                  flush=True)
        else:
            print(f'LAT size={size} n={n} lost={lost} (no echoes)', flush=True)
        if lost > max(0, n // 20):
            ok_all = False
    print(f'LAT_RESULT {"PASS" if ok_all else "FAIL"}', flush=True)
    if rclpy.ok():
        node.destroy_node()
        rclpy.shutdown()
    return 0 if ok_all else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mode', choices=['ping', 'pong'], required=True)
    ap.add_argument('--sizes', default='64,1024,4096,16384,65536')
    ap.add_argument('--count', type=int, default=0,
                    help='echoes per size (default: 200, or 50 for >=64KB)')
    ap.add_argument('--depth', type=int, default=10)
    ap.add_argument('--reliability', choices=['reliable', 'best_effort'], default='reliable')
    ap.add_argument('--interval-ms', type=float, default=0,
                    help='ping only: extra delay between echoes (default synchronous)')
    ap.add_argument('--echo-timeout', type=float, default=2.0,
                    help='ping only: per-echo timeout in seconds')
    ap.add_argument('--match-timeout', type=float, default=10.0)
    ap.add_argument('--rclpy-signals', action='store_true')
    args = ap.parse_args()
    return run_ping(args) if args.mode == 'ping' else run_pong(args)


if __name__ == '__main__':
    sys.exit(main())
