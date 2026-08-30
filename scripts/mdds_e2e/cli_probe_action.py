#!/usr/bin/env python3
# CLI-05 helper: verify the action graph data that `ros2 action send_goal`'s
# wait_for_server depends on, via application-level rclpy graph queries:
#   - publisher counts on the action's status/feedback topics
#   - service counts on send_goal/cancel_goal/get_result
#   - the action's services visible in get_service_names_and_types
# Prints machine-greppable lines:
#   PROBE pub_count <topic> <n>
#   PROBE srv_count <service> <n>
#   PROBE_RESULT PASS|FAIL
# Used because `ros2 action send_goal` itself hangs platform-wide on KaihongOS
# (identical failure under rmw_cyclonedds_cpp, so it is not an rmw_mdds defect);
# this probe asserts the RMW actually serves complete action graph data.

import sys

import rclpy

ACTION = '/fibonacci'
TOPICS = (ACTION + '/_action/status', ACTION + '/_action/feedback')
SERVICES = (
    ACTION + '/_action/send_goal',
    ACTION + '/_action/cancel_goal',
    ACTION + '/_action/get_result')


def main():
    rclpy.init()
    node = rclpy.create_node('cli_probe_action')
    ok = True
    for t in TOPICS:
        n = node.count_publishers(t)
        print(f'PROBE pub_count {t} {n}', flush=True)
        ok = ok and n >= 1
    for s in SERVICES:
        n = node.count_services(s)
        print(f'PROBE srv_count {s} {n}', flush=True)
        ok = ok and n >= 1
    visible = {s for s, _ in node.get_service_names_and_types()}
    missing = [s for s in SERVICES if s not in visible]
    print(f'PROBE services_missing {missing}', flush=True)
    ok = ok and not missing
    print(f'PROBE_RESULT {"PASS" if ok else "FAIL"}', flush=True)
    node.destroy_node()
    rclpy.shutdown()
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
