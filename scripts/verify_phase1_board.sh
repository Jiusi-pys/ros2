#!/bin/sh
# On-board verification for Phase 1+2 (sros2, kdl_parser_py, PyKDL, OpenCV).
# Every ROS/DSoftBus child is launcher-scoped and the loopback PIDs are owned
# by one unique run record; no process-name cleanup is permitted.
. /data/local/tmp/ros2/env.sh || exit 70
cd "$ROS2_HOME" || exit 70

RUN_ID="phase1_$(date +%Y%m%dT%H%M%S)_$$"
case "$RUN_ID" in *[!A-Za-z0-9_.-]*|'') exit 70 ;; esac
VLOG="$ROS2_HOME/verify_logs/$RUN_ID"
mkdir -p "$VLOG" || exit 70
ACTIVITY_LOCK="$ROS2_HOME/.ros2-activity-lock"
ACTIVITY_OWNER="ROS2_ACTIVITY_LOCK MODE=TEST RUN_ID=$RUN_ID OWNER=verify_phase1_board"
listener_record="$VLOG/vfy_listener.pid"
talker_record="$VLOG/vfy_talker.pid"
lock_held=0
overall=0

lock_result="$(if (umask 077; mkdir "$ACTIVITY_LOCK") 2>/dev/null && \
    (umask 077; set -C; printf '%s\n' "$ACTIVITY_OWNER" > "$ACTIVITY_LOCK/owner") 2>/dev/null && \
    test "$(cat "$ACTIVITY_LOCK/owner" 2>/dev/null)" = "$ACTIVITY_OWNER"; then
  printf ROS2_ACTIVITY_LOCK_ACQUIRED
else
  printf ROS2_ACTIVITY_LOCK_BUSY_OR_FAILED
fi)"
if [ "$lock_result" != ROS2_ACTIVITY_LOCK_ACQUIRED ]; then
  # If mkdir succeeded but owner creation failed, remove only an empty lock or
  # the exact owner written by this transaction. Any foreign content remains.
  rmdir "$ACTIVITY_LOCK" 2>/dev/null || {
    if [ -f "$ACTIVITY_LOCK/owner" ] && [ ! -L "$ACTIVITY_LOCK/owner" ] && \
        [ "$(cat "$ACTIVITY_LOCK/owner" 2>/dev/null)" = "$ACTIVITY_OWNER" ] && \
        [ "$(find "$ACTIVITY_LOCK" -mindepth 1 -maxdepth 1)" = "$ACTIVITY_LOCK/owner" ]; then
      rm -f "$ACTIVITY_LOCK/owner" && rmdir "$ACTIVITY_LOCK"
    fi
  }
  echo "ERROR: phase1 activity lock unavailable: $lock_result" >&2
  exit 3
fi
lock_held=1

stop_owned()
{
  record=$1
  [ -e "$record" ] || return 0
  [ -f "$record" ] && [ ! -L "$record" ] || return 1
  IFS=' ' read -r kind run_field pid_field start_field extra < "$record" || return 1
  [ "$kind" = ROS2_PHASE1_PROCESS ] && [ "$run_field" = "RUN_ID=$RUN_ID" ] && \
    [ -z "${extra:-}" ] || return 1
  pid=${pid_field#PID=}
  start=${start_field#START=}
  [ "$pid_field" = "PID=$pid" ] && [ "$start_field" = "START=$start" ] || return 1
  case "$pid" in ''|*[!0-9]*) return 1 ;; esac
  case "$start" in ''|*[!0-9]*) return 1 ;; esac
  if [ -r "/proc/$pid/stat" ] && \
      [ "$(cut -d ' ' -f22 "/proc/$pid/stat" 2>/dev/null)" = "$start" ]; then
    kill "$pid" 2>/dev/null || true
    i=0
    while [ "$i" -lt 20 ] && [ -r "/proc/$pid/stat" ] && \
        [ "$(cut -d ' ' -f22 "/proc/$pid/stat" 2>/dev/null)" = "$start" ]; do
      i=$((i + 1))
      sleep 0.1
    done
    if [ -r "/proc/$pid/stat" ] && \
        [ "$(cut -d ' ' -f22 "/proc/$pid/stat" 2>/dev/null)" = "$start" ]; then
      kill -9 "$pid" 2>/dev/null || true
      sleep 1
    fi
  fi
  if [ -r "/proc/$pid/stat" ] && \
      [ "$(cut -d ' ' -f22 "/proc/$pid/stat" 2>/dev/null)" = "$start" ]; then
    return 1
  fi
  rm -f "$record"
}

start_owned()
{
  program=$1
  log=$2
  record=$3
  shift 3
  [ ! -e "$record" ] && [ ! -L "$record" ] && \
    [ ! -e "$log" ] && [ ! -L "$log" ] || return 1
  nohup "$program" "$@" > "$log" 2>&1 &
  pid=$!
  start=$(cut -d ' ' -f22 "/proc/$pid/stat" 2>/dev/null)
  case "$pid" in ''|*[!0-9]*) kill "$pid" 2>/dev/null || true; return 1 ;; esac
  case "$start" in ''|*[!0-9]*) kill "$pid" 2>/dev/null || true; return 1 ;; esac
  (umask 077; set -C; printf 'ROS2_PHASE1_PROCESS RUN_ID=%s PID=%s START=%s\n' \
    "$RUN_ID" "$pid" "$start" > "$record") 2>/dev/null || {
      kill "$pid" 2>/dev/null || true
      return 1
    }
}

release_lock()
{
  [ "$lock_held" -eq 1 ] || return 0
  result="$(if [ -d "$ACTIVITY_LOCK" ] && [ ! -L "$ACTIVITY_LOCK" ] && \
      [ -f "$ACTIVITY_LOCK/owner" ] && [ ! -L "$ACTIVITY_LOCK/owner" ] && \
      [ "$(cat "$ACTIVITY_LOCK/owner" 2>/dev/null)" = "$ACTIVITY_OWNER" ] && \
      [ "$(find "$ACTIVITY_LOCK" -mindepth 1 -maxdepth 1)" = "$ACTIVITY_LOCK/owner" ] && \
      rm -f "$ACTIVITY_LOCK/owner" && rmdir "$ACTIVITY_LOCK"; then
    printf ROS2_ACTIVITY_LOCK_RELEASED
  else
    printf ROS2_ACTIVITY_LOCK_NOT_OWNED
  fi)"
  [ "$result" = ROS2_ACTIVITY_LOCK_RELEASED ] || return 1
  lock_held=0
}

finish()
{
  rc=$1
  trap - 0 INT TERM HUP
  stop_owned "$talker_record" || rc=1
  stop_owned "$listener_record" || rc=1
  if [ ! -e "$talker_record" ] && [ ! -e "$listener_record" ]; then
    release_lock || rc=1
  else
    echo "ERROR: retaining activity lock because an owned process is unresolved" >&2
    rc=1
  fi
  exit "$rc"
}
trap 'finish "$?"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP

echo "== 1. sros2 keystore =="
ros2 security create_keystore "$VLOG/keystore" > "$VLOG/sros2_out.txt" 2>&1
ls "$VLOG/keystore" >/dev/null 2>&1 && echo "SROS2_OK" || {
  echo "SROS2_FAIL"; head -20 "$VLOG/sros2_out.txt"; overall=1;
}

echo "== 2. kdl_parser_py + PyKDL =="
python3.12 - <<'EOF' && echo "KDL_OK" || { echo "KDL_FAIL"; overall=1; }
from kdl_parser_py.urdf import treeFromString
urdf = '''
<robot name="t">
  <link name="base"/>
  <link name="tip"/>
  <joint name="j1" type="revolute">
    <parent link="base"/><child link="tip"/>
    <origin xyz="0 0 1"/><axis xyz="0 0 1"/>
    <limit lower="-1" upper="1" effort="1" velocity="1"/>
  </joint>
</robot>'''
ok, tree = treeFromString(urdf)
print("ok:", ok, "segments:", tree.getNrOfSegments())
assert ok and tree.getNrOfSegments() == 1
EOF

echo "== 3. tf2_bullet on ament index =="
ros2 pkg list 2>/dev/null | grep -x tf2_bullet && echo "TF2BULLET_OK" || {
  echo "TF2BULLET_FAIL"; overall=1;
}

echo "== 4. image_tools cam2image (burger mode, no camera) =="
timeout 12 "$ROS2_HOME/Lib/image_tools/cam2image" \
  --ros-args -p burger_mode:=true -p width:=64 -p height:=64 \
  > "$VLOG/cam2image.txt" 2>&1
grep -q "Publishing image" "$VLOG/cam2image.txt" && echo "CAM2IMAGE_OK" || {
  echo "CAM2IMAGE_FAIL"; head -5 "$VLOG/cam2image.txt"; overall=1;
}

echo "== 5. loopback regression =="
topic="/phase1_${RUN_ID}"
if start_owned "$ROS2_LISTENER_RAW" "$VLOG/vfy_listener.log" "$listener_record" \
    --ros-args -r "chatter:=$topic"; then
  sleep 3
  if start_owned "$ROS2_TALKER_RAW" "$VLOG/vfy_talker.log" "$talker_record" \
      --ros-args -r "chatter:=$topic"; then
    sleep 10
  else
    echo "LOOPBACK_TALKER_START_FAIL"
    overall=1
  fi
else
  echo "LOOPBACK_LISTENER_START_FAIL"
  overall=1
fi
stop_owned "$talker_record" || overall=1
stop_owned "$listener_record" || overall=1
heard=$(grep -c "I heard" "$VLOG/vfy_listener.log" 2>/dev/null || true)
echo "listener heard: $heard topic=$topic"
[ "${heard:-0}" -gt 0 ] && echo "LOOPBACK_OK" || {
  echo "LOOPBACK_FAIL"; overall=1;
}

echo "== done: run=$RUN_ID log=$VLOG =="
exit "$overall"
