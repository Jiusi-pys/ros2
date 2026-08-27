#!/usr/bin/env bash
# Run the bidirectional talker/listener test between the two RK3588 boards.
#
#   Direction 1: board A (talker) -> board B (listener)
#   Direction 2: board B (talker) -> board A (listener)
#
# Logs are collected under ohos_test_logs/.
# Run from the ros2/ workspace root inside Git Bash:
#   ./scripts/run_bidirectional_test.sh [seconds_per_direction]
set -euo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-/c/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00   # 192.168.77.201
BOARD_B=3e01ff55454d202020104433991c3b00   # 192.168.77.202
DEVICE_DIR=/data/local/tmp/ros2
DURATION="${1:-20}"
LOGDIR=ohos_test_logs
mkdir -p "$LOGDIR"

# Remote helpers -------------------------------------------------------------

remote_bg() {  # remote_bg <board> <logname> <talker|listener>
  local board=$1 logname=$2 prog=$3
  local varname
  varname=$(echo "ROS2_$prog" | tr 'a-z' 'A-Z')
  "$HDC" -t "$board" shell \
    ". $DEVICE_DIR/env.sh; nohup \$$varname > $DEVICE_DIR/$logname 2>&1 &" &
}

remote_stop_all() {  # kill leftover demo processes on both boards
  for board in "$BOARD_A" "$BOARD_B"; do
    "$HDC" -t "$board" shell \
      "pkill -f 'talker|listener' 2>/dev/null; true" >/dev/null 2>&1 || true
  done
  sleep 1
}

collect() {  # collect <board> <logname> <localfile>
  "$HDC" file recv "$2:$DEVICE_DIR/$3" "$4" 2>/dev/null || \
  "$HDC" -t "$1" shell "cat $DEVICE_DIR/$2" > "$3"
}

run_direction() {  # run_direction <talker_board> <listener_board> <tag>
  local talker_board=$1 listener_board=$2 tag=$3
  echo "== direction: talker=$tag-talker listener=$tag-listener =="
  remote_stop_all
  remote_bg "$listener_board" "listener_$tag.log" listener
  sleep 3
  remote_bg "$talker_board" "talker_$tag.log" talker
  echo "   running ${DURATION}s ..."
  sleep "$DURATION"
  remote_stop_all

  "$HDC" -t "$listener_board" shell "cat $DEVICE_DIR/listener_$tag.log" > "$LOGDIR/listener_$tag.log"
  "$HDC" -t "$talker_board" shell "cat $DEVICE_DIR/talker_$tag.log" > "$LOGDIR/talker_$tag.log"

  local heard
  heard=$(grep -c "I heard" "$LOGDIR/listener_$tag.log" || true)
  echo "   listener received $heard messages"
  if [ "$heard" -gt 0 ]; then
    echo "   PASS"
  else
    echo "   FAIL (see $LOGDIR/listener_$tag.log and $LOGDIR/talker_$tag.log)"
    return 1
  fi
}

overall=0
run_direction "$BOARD_A" "$BOARD_B" "a_to_b" || overall=1
run_direction "$BOARD_B" "$BOARD_A" "b_to_a" || overall=1

if [ "$overall" -eq 0 ]; then
  echo "== bidirectional test PASSED =="
else
  echo "== bidirectional test FAILED =="
fi
exit "$overall"
