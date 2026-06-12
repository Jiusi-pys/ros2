#!/bin/sh
# rosbag2 lanes for RK3588A validation. The recorder must run in the
# FOREGROUND: POSIX sh forces SIGINT to SIG_IGN for background jobs and
# CPython never re-enables ignored signals, so a backgrounded `ros2 bag
# record` can never stop gracefully and metadata.yaml is lost. A background
# watchdog delivers INT (graceful) and a late KILL (hang safety) instead.

PREFIX="${PREFIX:-/data/local/tmp/ohos-prefix}"
OVERLAY="${OVERLAY:-/data/local/tmp/ohos-colcon-rk3588a}"
WORK_DIR="${WORK_DIR:-/data/local/tmp/val}"
ROS2="${OVERLAY}/bin/ros2"
DOM_BASE="${DOM_BASE:-60}"

mkdir -p "${WORK_DIR}"
export HOME=/data/local/tmp
export ROS_LOG_DIR=/data/local/tmp/roslogs
export AMENT_PREFIX_PATH="${PREFIX}"
export ROS_DISTRO=jazzy
mkdir -p "${ROS_LOG_DIR}"

CXX_LD="${PREFIX}/lib:/data/local/tmp:/data/local/release/usr/lib"

result() { echo "RESULT|$1|$2|$3"; }

kill_pattern() {
  ps -ef | grep "$1" | grep -v grep | while read _u _p _rest; do
    kill -"${2:-9}" "${_p}" 2>/dev/null
  done
}

run_talker() {
  env LD_LIBRARY_PATH="${CXX_LD}" RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
      ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET ROS_DOMAIN_ID="$1" \
      "${PREFIX}/lib/demo_nodes_cpp/talker" >"$2" 2>&1 &
  echo $!
}

record_lane() {
  lane="$1"; storage="$2"; dom="$3"
  bagdir="${WORK_DIR}/bag_${storage}"
  rm -rf "${bagdir}"
  tp=$(run_talker "${dom}" "${WORK_DIR}/bag_talker_${storage}.log")
  sleep 3
  ( sleep 12; kill_pattern "bag record" INT; sleep 12; kill_pattern "bag record" ) &
  wd=$!
  env ROS_DOMAIN_ID="${dom}" RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
      "${ROS2}" bag record --storage "${storage}" --topics /chatter -o "${bagdir}" \
      >"${WORK_DIR}/bag_rec_${storage}.log" 2>&1
  kill "${wd}" 2>/dev/null
  kill "${tp}" 2>/dev/null; wait "${tp}" 2>/dev/null
  info=$("${ROS2}" bag info "${bagdir}" 2>&1)
  case "${info}" in
    *"${storage}"*"std_msgs/msg/String"*) result "${lane}" PASS "$(echo "${info}" | grep -oE 'Messages: +[0-9]+' | head -1)";;
    *) result "${lane}" FAIL "$(echo "${info}" | tail -2 | tr '\n' ' ')";;
  esac
}

record_lane bag_sqlite3 sqlite3 $((DOM_BASE+15))
record_lane bag_mcap mcap $((DOM_BASE+16))

### bag play + listener
dom=$((DOM_BASE+17))
env LD_LIBRARY_PATH="${CXX_LD}" RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
    ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET ROS_DOMAIN_ID="${dom}" \
    "${PREFIX}/lib/demo_nodes_cpp/listener" >"${WORK_DIR}/play_listener.log" 2>&1 &
lp=$!
sleep 3
( sleep 15; kill_pattern "bag play" INT; sleep 5; kill_pattern "bag play" ) &
wd=$!
env ROS_DOMAIN_ID="${dom}" RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
    "${ROS2}" bag play "${WORK_DIR}/bag_sqlite3" >"${WORK_DIR}/bag_play.log" 2>&1
kill "${wd}" 2>/dev/null
kill "${lp}" 2>/dev/null; wait "${lp}" 2>/dev/null
if grep -q "I heard" "${WORK_DIR}/play_listener.log"; then result bag_play PASS "replay_received"; else result bag_play FAIL "see play_listener.log"; fi

echo "BAG_LANES_DONE"
