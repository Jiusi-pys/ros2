#!/usr/bin/env python3
"""Single-process board probes for DSoftBus-only gateway scenarios.

The DSoftBus Socket/Bytes service is intentionally one MDDS participant per
board/process/domain.  These probes keep publish and subscribe endpoints in a
single rclpy context when a gateway scenario needs both roles, so a test never
"passes" over UDP merely because two same-board rmw_mdds processes cannot bind
the DSoftBus session concurrently.
"""

import argparse
import collections
import sys
import time


def qos(depth):
    from rclpy.qos import HistoryPolicy, QoSProfile, ReliabilityPolicy

    return QoSProfile(
        history=HistoryPolicy.KEEP_LAST,
        depth=depth,
        reliability=ReliabilityPolicy.RELIABLE,
    )


def init_ros():
    import rclpy
    from rclpy.signals import SignalHandlerOptions

    # The launcher owns process termination; avoid installing a second signal
    # policy inside a test helper and keep normal completion deterministic.
    rclpy.init(signal_handler_options=SignalHandlerOptions.NO)
    return rclpy


def finish_ros(rclpy, node):
    try:
        node.destroy_node()
    finally:
        if rclpy.ok():
            rclpy.shutdown()


def run_duplex(args):
    import rclpy
    from std_msgs.msg import String

    init_ros()
    node = rclpy.create_node("mdds_gateway_dsoftbus_duplex")
    received = collections.Counter()
    publisher = node.create_publisher(String, args.outgoing_topic, qos(args.depth))

    def on_incoming(message):
        received[message.data] += 1
        print(
            f"DUPLEX_RX payload={message.data!r} count={received[message.data]}",
            flush=True,
        )

    node.create_subscription(String, args.incoming_topic, on_incoming, qos(args.depth))
    try:
        # The remote gateway must create its MDDS reader before this probe
        # starts its finite board->PC sequence.  Spin meanwhile so PC->board
        # traffic can already be observed during type/discovery convergence.
        match_deadline = time.monotonic() + args.match_timeout
        matched = False
        while rclpy.ok() and time.monotonic() < match_deadline:
            rclpy.spin_once(node, timeout_sec=0.1)
            if publisher.get_subscription_count() >= 1:
                matched = True
                break
        if not matched:
            print("DUPLEX_RESULT FAIL reason=no_outgoing_gateway_match", flush=True)
            return 2

        next_publish = time.monotonic()
        period = 1.0 / args.rate
        sent = 0
        while rclpy.ok() and sent < args.publish_count:
            rclpy.spin_once(node, timeout_sec=0.02)
            now = time.monotonic()
            if now < next_publish:
                continue
            sent += 1
            message = String()
            message.data = f"{args.payload_prefix}-{sent:04d}"
            publisher.publish(message)
            print(f"DUPLEX_TX payload={message.data!r}", flush=True)
            next_publish += period

        linger_deadline = time.monotonic() + args.linger
        while rclpy.ok() and time.monotonic() < linger_deadline:
            rclpy.spin_once(node, timeout_sec=0.1)

        total = sum(received.values())
        duplicates = sum(count - 1 for count in received.values() if count > 1)
        ok = sent == args.publish_count and total >= args.min_received and duplicates == 0
        print(
            "DUPLEX_RESULT {} sent={} received={} unique={} duplicates={} min_received={}".format(
                "PASS" if ok else "FAIL",
                sent,
                total,
                len(received),
                duplicates,
                args.min_received,
            ),
            flush=True,
        )
        return 0 if ok else 1
    finally:
        finish_ros(rclpy, node)


def run_loop(args):
    import rclpy
    from std_msgs.msg import String

    init_ros()
    node = rclpy.create_node("mdds_gateway_dsoftbus_loop")
    received = collections.Counter()
    publisher = node.create_publisher(String, args.topic, qos(args.depth))

    def on_message(message):
        received[message.data] += 1
        print(f"LOOP_RX payload={message.data!r} count={received[message.data]}", flush=True)

    node.create_subscription(String, args.topic, on_message, qos(args.depth))
    try:
        # Let the gateway discover both local endpoints before the finite
        # sequence starts. The harness separately requires the gateway bridge
        # log, so this delay cannot turn an unbridged local-only run into PASS.
        ready_deadline = time.monotonic() + args.startup_delay
        while rclpy.ok() and time.monotonic() < ready_deadline:
            rclpy.spin_once(node, timeout_sec=0.1)

        next_publish = time.monotonic()
        period = 1.0 / args.rate
        expected = []
        while rclpy.ok() and len(expected) < args.count:
            rclpy.spin_once(node, timeout_sec=0.02)
            now = time.monotonic()
            if now < next_publish:
                continue
            payload = f"{args.payload_prefix}-{len(expected) + 1:04d}"
            message = String()
            message.data = payload
            publisher.publish(message)
            expected.append(payload)
            print(f"LOOP_TX payload={payload!r}", flush=True)
            next_publish += period

        linger_deadline = time.monotonic() + args.linger
        while rclpy.ok() and time.monotonic() < linger_deadline:
            rclpy.spin_once(node, timeout_sec=0.1)

        expected_set = set(expected)
        total = sum(received.values())
        duplicates = sum(count - 1 for count in received.values() if count > 1)
        unexpected = sum(
            count for payload, count in received.items() if payload not in expected_set
        )
        exactly_once = all(received[payload] == 1 for payload in expected)
        ok = len(expected) == args.count and exactly_once and total == args.count and \
            duplicates == 0 and unexpected == 0
        print(
            "LOOP_RESULT {} expected={} received={} duplicates={} unexpected={} matched_readers={}".format(
                "PASS" if ok else "FAIL",
                args.count,
                total,
                duplicates,
                unexpected,
                publisher.get_subscription_count(),
            ),
            flush=True,
        )
        return 0 if ok else 1
    finally:
        finish_ros(rclpy, node)


def main():
    parser = argparse.ArgumentParser()
    subcommands = parser.add_subparsers(dest="mode", required=True)

    duplex = subcommands.add_parser("duplex")
    duplex.add_argument("--incoming-topic", default="/chatter")
    duplex.add_argument("--outgoing-topic", default="/chatter_back")
    duplex.add_argument("--publish-count", type=int, default=60)
    duplex.add_argument("--min-received", type=int, default=40)
    duplex.add_argument("--rate", type=float, default=1.0)
    duplex.add_argument("--depth", type=int, default=100)
    duplex.add_argument("--match-timeout", type=float, default=20.0)
    duplex.add_argument("--linger", type=float, default=5.0)
    duplex.add_argument("--payload-prefix", default="gw03-board")
    duplex.set_defaults(run=run_duplex)

    loop = subcommands.add_parser("loop")
    loop.add_argument("--topic", default="/chatter")
    loop.add_argument("--count", type=int, default=30)
    loop.add_argument("--rate", type=float, default=1.0)
    loop.add_argument("--depth", type=int, default=100)
    loop.add_argument("--startup-delay", type=float, default=10.0)
    loop.add_argument("--linger", type=float, default=8.0)
    loop.add_argument("--payload-prefix", default="gw05-loop")
    loop.set_defaults(run=run_loop)

    args = parser.parse_args()
    if args.rate <= 0 or args.depth < 1:
        parser.error("--rate must be positive and --depth must be at least 1")
    if args.mode == "duplex" and (args.publish_count < 1 or args.min_received < 1):
        parser.error("--publish-count and --min-received must be at least 1")
    if args.mode == "loop" and args.count < 1:
        parser.error("--count must be at least 1")
    return args.run(args)


if __name__ == "__main__":
    sys.exit(main())
