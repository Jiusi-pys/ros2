#!/usr/bin/env bash
# Same-board loopback smoke test: run talker + listener on ONE board and
# verify the listener receives messages. Validates the port itself before
# attempting board-to-board tests.
#   ./scripts/smoke_loopback.sh [board_id] [seconds]
set -euo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-/c/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD="${1:-3e01ff55454d202020104033bf453b00}"
DURATION="${2:-15}"
DEVICE_DIR=/data/local/tmp/ros2
mkdir -p ohos_test_logs

"$HDC" -t "$BOARD" shell "pkill -f 'talker|listener' 2>/dev/null; true" >/dev/null 2>&1 || true
sleep 1

"$HDC" -t "$BOARD" shell \
  ". $DEVICE_DIR/env.sh; nohup \$ROS2_LISTENER > $DEVICE_DIR/loop_listener.log 2>&1 &" &
sleep 3
"$HDC" -t "$BOARD" shell \
  ". $DEVICE_DIR/env.sh; nohup \$ROS2_TALKER > $DEVICE_DIR/loop_talker.log 2>&1 &" &

echo "running ${DURATION}s on $BOARD ..."
sleep "$DURATION"
"$HDC" -t "$BOARD" shell "pkill -f 'talker|listener' 2>/dev/null; true" >/dev/null 2>&1 || true
sleep 1

"$HDC" -t "$BOARD" shell "cat $DEVICE_DIR/loop_listener.log" > ohos_test_logs/loop_listener.log
"$HDC" -t "$BOARD" shell "cat $DEVICE_DIR/loop_talker.log" > ohos_test_logs/loop_talker.log

heard=$(grep -c "I heard" ohos_test_logs/loop_listener.log || true)
echo "listener received $heard messages"
if [ "$heard" -gt 0 ]; then
  echo "== loopback smoke test PASSED =="
else
  echo "== loopback smoke test FAILED (see ohos_test_logs/loop_*.log) =="
  exit 1
fi
