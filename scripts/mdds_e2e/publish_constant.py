#!/usr/bin/env python3
"""Publish an exact finite run of identical ROS String samples.

Used by GW-08 to distinguish an intentional sequence of equal payloads from
gateway loop suppression. The script waits for one matched reader before it
starts, publishes exactly the requested count, then lingers so a RELIABLE
writer can finish delivery before the harness stops it.
"""

import argparse
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--topic", default="/chatter")
    parser.add_argument("--payload", default="constant")
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--rate", type=float, default=2.0)
    parser.add_argument("--match-timeout", type=float, default=15.0)
    parser.add_argument("--linger", type=float, default=3.0)
    args = parser.parse_args()
    if args.count < 1 or args.rate <= 0:
        parser.error("--count and --rate must be positive")

    import rclpy
    from rclpy.qos import (
        HistoryPolicy,
        QoSProfile,
        ReliabilityPolicy,
    )
    from std_msgs.msg import String

    rclpy.init()
    node = rclpy.create_node("mdds_gateway_constant_pub")
    qos = QoSProfile(
        history=HistoryPolicy.KEEP_LAST,
        depth=max(10, args.count),
        reliability=ReliabilityPolicy.RELIABLE,
    )
    publisher = node.create_publisher(String, args.topic, qos)
    try:
        deadline = time.monotonic() + args.match_timeout
        while rclpy.ok() and publisher.get_subscription_count() < 1:
            if time.monotonic() >= deadline:
                print("CONSTANT_PUB_NO_MATCH", flush=True)
                return 2
            rclpy.spin_once(node, timeout_sec=0.1)

        message = String()
        message.data = args.payload
        period = 1.0 / args.rate
        for sequence in range(args.count):
            publisher.publish(message)
            print(
                f"CONSTANT_PUB seq={sequence + 1}/{args.count} "
                f"payload={args.payload!r}",
                flush=True,
            )
            time.sleep(period)
        time.sleep(args.linger)
        print(f"CONSTANT_PUB_DONE count={args.count}", flush=True)
        return 0
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    sys.exit(main())
