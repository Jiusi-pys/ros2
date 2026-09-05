#!/usr/bin/env bash
# Run-owned same-board talker/listener smoke.
#   ./scripts/smoke_loopback.sh [board_id] [seconds]
set -euo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-/c/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD="${1:-3e01ff55454d202020104033bf453b00}"
DURATION="${2:-15}"
DEVICE_DIR=/data/local/tmp/ros2
[[ "$BOARD" =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "ERROR: unsafe board id" >&2; exit 2; }
[[ "$DURATION" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: duration must be a positive integer" >&2; exit 2; }

export MSYS2_ARG_CONV_EXCL='*'
shell() { "$HDC" -t "$1" shell "$2" </dev/null; }
source scripts/lib/mdds_owned_processes.sh
mdds_owned_init smoke_loopback "$BOARD" || exit 3
LOGDIR="ohos_test_logs/smoke_loopback/$MDDS_OWNED_RUN_ID"
mkdir -p "$LOGDIR"
finish() {
  local rc="$1"
  trap - EXIT INT TERM HUP
  mdds_owned_finish || rc=1
  exit "$rc"
}
trap 'finish "$?"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

RENVS=". $DEVICE_DIR/env.sh || exit 70; export RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION:-rmw_mdds};"
TOPIC="/mdds_loop_${MDDS_OWNED_RUN_ID//[^A-Za-z0-9_]/_}"
mdds_owned_launch "$BOARD" "$RENVS" \
  "\$ROS2_LISTENER --ros-args -r chatter:=$TOPIC" loop_listener.log
sleep 3
mdds_owned_launch "$BOARD" "$RENVS" \
  "\$ROS2_TALKER --ros-args -r chatter:=$TOPIC" loop_talker.log
echo "running ${DURATION}s on $BOARD (run=$MDDS_OWNED_RUN_ID) ..."
sleep "$DURATION"
mdds_owned_stop_all

shell "$BOARD" "cat '$MDDS_OWNED_REMOTE_DIR/loop_listener.log'" > "$LOGDIR/loop_listener.log"
shell "$BOARD" "cat '$MDDS_OWNED_REMOTE_DIR/loop_talker.log'" > "$LOGDIR/loop_talker.log"
heard="$(grep -c "I heard" "$LOGDIR/loop_listener.log" || true)"
if [ "$heard" -gt 0 ]; then
  echo "LOOPBACK_RESULT PASS run=$MDDS_OWNED_RUN_ID topic=$TOPIC heard=$heard"
else
  echo "LOOPBACK_RESULT FAIL run=$MDDS_OWNED_RUN_ID topic=$TOPIC heard=$heard logs=$LOGDIR" >&2
  exit 1
fi
