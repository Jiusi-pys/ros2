#!/usr/bin/env bash
# Execute the real monitor with short-lived local children, never a board.
set -euo pipefail
cd "$(dirname "$0")/.."
DEVICE_DIR=/not-used
. scripts/lib/ros2_owned_processes.sh
ROS2_OWNED_RUN_ID=exit_contract
fixture="$(mktemp -d)"
trap 'rm -rf -- "$fixture"' EXIT
guard='printf "ROS2_OWNED_START RUN_ID=%s TAG=%s PID=%s START=123\n" "$5" "$6" "$$" > "$2"; exit "$8"'
for expected_status in 0 7 143; do
  log="$fixture/exit-$expected_status.log"
  set +e
  sh -c "$(ros2_owned_monitor_script)" sh "$guard" unused "$log" unused unused "$ROS2_OWNED_RUN_ID" test_tag unused "$expected_status"
  actual_status=$?
  set -e
  [ "$actual_status" = "$expected_status" ]
  if [ "$expected_status" = 0 ]; then
    ros2_owned_require_successful_exit "$log"
  elif ros2_owned_require_successful_exit "$log"; then
    echo "ERROR: nonzero child exit accepted: $expected_status" >&2
    exit 1
  fi
done
sed '/^ROS2_OWNED_EXIT /d' "$fixture/exit-0.log" > "$fixture/missing.log"
if ros2_owned_require_successful_exit "$fixture/missing.log"; then exit 1; fi
cat "$fixture/exit-0.log" "$fixture/exit-0.log" > "$fixture/duplicate.log"
if ros2_owned_require_successful_exit "$fixture/duplicate.log"; then exit 1; fi
sed '/^ROS2_OWNED_EXIT /s/TAG=test_tag/TAG=wrong/' "$fixture/exit-0.log" > "$fixture/wrong-tag.log"
if ros2_owned_require_successful_exit "$fixture/wrong-tag.log"; then exit 1; fi
echo 'ROS2_OWNED_EXIT_CONTRACT result=PASS cases=success,exit7,exit143,missing,duplicate,wrong-tag'
