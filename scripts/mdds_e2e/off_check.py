#!/usr/bin/env python3
# Discovery-range OFF check for mdds e2e (E2E-13, see run_mdds_e2e.sh).
#
# rcl parses ROS_AUTOMATIC_DISCOVERY_RANGE and forwards it to rmw init; with
# rmw_mdds, OFF must make ParticipantConfig.discovery_enabled=false so the
# participant never announces and never accepts announces. Two processes in the
# same domain on the same board, both OFF, must after several announce periods
# (default 500 ms each) still observe:
#   graph:   no node other than itself
#   matched: 0 remote endpoints (pub: subscription count, sub: publisher count)
#   data:    0 messages
# A control pair run with the default range MUST discover and communicate —
# otherwise the OFF assertions are vacuous.
#
# Usage (on board, env sourced, RMW_IMPLEMENTATION=rmw_mdds):
#   ROS_AUTOMATIC_DISCOVERY_RANGE=OFF python3.12 off_check.py --role pub --expect off
#   ROS_AUTOMATIC_DISCOVERY_RANGE=OFF python3.12 off_check.py --role sub --expect off
#   python3.12 off_check.py --role pub --expect on   # control
#
# Prints one OFF_CHECK evidence line and "OFF_CHECK RESULT PASS|FAIL";
# exit code 0 iff PASS.
import argparse
import os
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--role', choices=['pub', 'sub'], required=True)
    ap.add_argument('--expect', choices=['off', 'on'], required=True)
    ap.add_argument('--topic', default='/mdds_off_check')
    ap.add_argument('--duration', type=float, default=8.0,
                    help='seconds to run; must span several announce periods '
                         '(fleet default 500 ms)')
    args = ap.parse_args()

    import rclpy
    from rclpy.signals import SignalHandlerOptions
    from std_msgs.msg import String

    rclpy.init(signal_handler_options=SignalHandlerOptions.NO)
    node = rclpy.create_node('mdds_off_check_' + args.role)
    heard = {'n': 0}

    pub = None
    sub = None
    if args.role == 'pub':
        pub = node.create_publisher(String, args.topic, 10)
    else:
        def cb(_msg):
            heard['n'] += 1
        sub = node.create_subscription(String, args.topic, cb, 10)

    t0 = time.monotonic()
    seq = 0
    while rclpy.ok() and time.monotonic() - t0 < args.duration:
        if pub is not None:
            msg = String()
            msg.data = f'off-check {seq}'
            seq += 1
            pub.publish(msg)
        rclpy.spin_once(node, timeout_sec=0.1)

    # Evidence snapshot after the full window. The node-name graph query is
    # best-effort evidence: rmw graph introspection may be limited, so a query
    # failure is recorded but never counts as a discovery leak by itself —
    # matched counts and actual data reception are the hard signals.
    try:
        others = sorted(n for n in node.get_node_names() if n != node.get_name())
    except Exception as e:
        others = ['<graph query failed: %r>' % e]
    if pub is not None:
        matched = pub.get_subscription_count()
    else:
        matched = sub.get_publisher_count()
    rng = os.environ.get('ROS_AUTOMATIC_DISCOVERY_RANGE', '(unset)')
    print(f'OFF_CHECK role={args.role} range={rng} others={others} '
          f'matched={matched} heard={heard["n"]}', flush=True)

    graph_clean = not others or all(n.startswith('<graph query failed') for n in others)
    if args.expect == 'off':
        ok = graph_clean and matched == 0 and heard['n'] == 0
    elif args.role == 'pub':
        ok = matched >= 1
    else:
        ok = heard['n'] >= 1
    print(f'OFF_CHECK RESULT {"PASS" if ok else "FAIL"}', flush=True)

    if rclpy.ok():
        node.destroy_node()
        rclpy.shutdown()
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
