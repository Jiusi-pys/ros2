#!/usr/bin/env bash
# Host-orchestrated cross-board ROS 2 CLI pub/echo over the live eth1 link.
# Usage: run_cross_board_cli_pubsub.sh <device_a> <device_b> [domain_id]
# Runs echo on the subscriber board in the background, pub on the other,
# both directions. Emits RESULT|cross_<dir>|PASS/FAIL lines.

set -u

DEV_A="${1:?device A id}"
DEV_B="${2:?device B id}"
DOM="${3:-55}"
OVERLAY=/data/local/tmp/ohos-colcon-rk3588a
ENVSTR="ROS_DOMAIN_ID=${DOM} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET"

run_direction() {
  local pub_dev="$1" sub_dev="$2" tag="$3" payload="$4"
  local topic="/rk3588a_cross_${tag}"
  local echo_log="/data/local/tmp/val/cross_echo_${tag}.log"

  hdc -t "${sub_dev}" shell "mkdir -p /data/local/tmp/val; rm -f ${echo_log}; nohup sh -c 'env ${ENVSTR} ${OVERLAY}/bin/ros2 topic echo ${topic} std_msgs/msg/String --once > ${echo_log} 2>&1' >/dev/null 2>&1 &" >/dev/null 2>&1
  sleep 8
  hdc -t "${pub_dev}" shell "env ${ENVSTR} ${OVERLAY}/bin/ros2 topic pub --times 8 ${topic} std_msgs/msg/String '{data: ${payload}}'" >/dev/null 2>&1
  sleep 3
  local got
  got=$(hdc -t "${sub_dev}" shell "cat ${echo_log} 2>/dev/null" 2>/dev/null)
  if echo "${got}" | grep -q "${payload}"; then
    echo "RESULT|cross_${tag}|PASS|${payload}"
  else
    echo "RESULT|cross_${tag}|FAIL|echo_log=$(echo "${got}" | head -2 | tr '\n' ' ')"
  fi
}

run_direction "${DEV_B}" "${DEV_A}" "b_to_a" "hello_from_B_${DOM}"
run_direction "${DEV_A}" "${DEV_B}" "a_to_b" "hello_from_A_${DOM}"
echo "CROSS_BOARD_DONE"
