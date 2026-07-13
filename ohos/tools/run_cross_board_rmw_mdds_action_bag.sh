#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_cross_board_rmw_mdds_action_bag.sh <record-client-device> <action-server-device> [record-domain] [play-domain]

Records a cross-device Fibonacci action with the native rosbag2 action CLI,
validates exact action metadata, and replays the bag as an action client.
Both boards are forced to use rmw_mdds_cpp.

Environment:
  HDC_BIN                              HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX              Board ROS 2 overlay
  RMW_MDDS_ACTION_BAG_WORK_DIR         Board evidence directory
  RMW_MDDS_ACTION_BAG_DISCOVERY_TRIES  Action discovery attempts, default: 15
  RMW_MDDS_HDC_TIMEOUT_SECONDS         Per-HDC timeout, default: 120
  RMW_MDDS_HDC_RETRY_ATTEMPTS          HDC retry attempts, default: 5
EOF
}

if [[ $# -lt 2 || $# -gt 4 ]]; then
  usage
  exit 2
fi

CLIENT_DEVICE_ID="$1"
SERVER_DEVICE_ID="$2"
RECORD_DOMAIN="${3:-${RMW_MDDS_ACTION_BAG_RECORD_DOMAIN:-195}}"
PLAY_DOMAIN="${4:-${RMW_MDDS_ACTION_BAG_PLAY_DOMAIN:-196}}"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
WORK_DIR="${RMW_MDDS_ACTION_BAG_WORK_DIR:-/data/local/tmp/rmw_mdds_action_bag_cross}"
DISCOVERY_TRIES="${RMW_MDDS_ACTION_BAG_DISCOVERY_TRIES:-15}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-120}"
HDC_RETRY_ATTEMPTS="${RMW_MDDS_HDC_RETRY_ATTEMPTS:-5}"
HDC_RETRY_DELAY_SECONDS="${RMW_MDDS_HDC_RETRY_DELAY_SECONDS:-1}"

ACTION_NAME="/rmw_mdds_action_bag"
PROBE="${REMOTE_PREFIX}/lib/rmw_mdds_action_bag_probe/rmw_mdds_action_bag_probe"
ROS2_BIN="${REMOTE_PREFIX}/bin/ros2"
BAG_DIR="${WORK_DIR}/bag"
RECORD_SOCKET="${WORK_DIR}/record_broker.sock"
PLAY_SOCKET="${WORK_DIR}/play_broker.sock"
RECORD_SERVER_LOG="${WORK_DIR}/record_server.log"
RECORD_LOG="${WORK_DIR}/record.log"
CLIENT_LOG="${WORK_DIR}/client.log"
INFO_LOG="${WORK_DIR}/info.log"
PLAY_SERVER_LOG="${WORK_DIR}/play_server.log"
PLAY_LOG="${WORK_DIR}/play.log"
RECORD_SERVER_PID="${WORK_DIR}/record_server.pid"
RECORDER_PID="${WORK_DIR}/recorder.pid"
PLAY_SERVER_PID="${WORK_DIR}/play_server.pid"

if ! [[ "${RECORD_DOMAIN}" =~ ^[0-9]+$ && "${PLAY_DOMAIN}" =~ ^[0-9]+$ ]]; then
  echo "domain IDs must be non-negative integers" >&2
  exit 2
fi
if [[ "${RECORD_DOMAIN}" == "${PLAY_DOMAIN}" ]]; then
  echo "record and play domains must differ" >&2
  exit 2
fi

hdc_output_succeeded() {
  local status="$1"
  local output_file="$2"
  [[ ${status} -eq 0 || ${status} -eq 139 ]] || return 1
  ! grep -qE 'Connect server failed|Connect key failed|No device|device offline|\[Fail\]' "${output_file}"
}

capture_hdc_shell() {
  local device_id="$1"
  local command="$2"
  local attempt output_file status
  for ((attempt = 1; attempt <= HDC_RETRY_ATTEMPTS; attempt += 1)); do
    output_file="$(mktemp)"
    set +e
    timeout "${HDC_TIMEOUT_SECONDS}s" "${HDC_BIN}" -t "${device_id}" shell "${command}" >"${output_file}" 2>&1
    status=$?
    set -e
    if hdc_output_succeeded "${status}" "${output_file}"; then
      cat "${output_file}"
      rm -f "${output_file}"
      return 0
    fi
    if [[ ${attempt} -lt ${HDC_RETRY_ATTEMPTS} ]]; then
      rm -f "${output_file}"
      sleep "${HDC_RETRY_DELAY_SECONDS}"
      continue
    fi
    cat "${output_file}"
    rm -f "${output_file}"
    return 1
  done
}

require_remote_file() {
  local device_id="$1"
  local path="$2"
  local output
  output="$(capture_hdc_shell "${device_id}" "test -e '${path}' && echo OK || echo MISSING:${path}")"
  if ! grep -q '^OK$' <<<"${output}"; then
    echo "${output}" >&2
    exit 1
  fi
}

remote_env() {
  local domain="$1"
  local socket="$2"
  cat <<EOF
export HOME='/data/local/tmp'; export ROS_LOG_DIR='${WORK_DIR}/roslogs'; export ROS_DISTRO='jazzy'; export LD_LIBRARY_PATH='${REMOTE_PREFIX}/lib:/data/local/tmp/ohos-prefix/lib:/data/local/tmp/ohos-fastdds/lib:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64'; unset LD_PRELOAD; export PYTHONHOME='/data/local/release/usr'; export PYTHONPATH='${REMOTE_PREFIX}/lib/python3.12/site-packages:/data/local/tmp/ohos-prefix/lib/python3.12/site-packages:/data/local/tmp/ohos-prefix/lib/python3.11/site-packages'; export AMENT_PREFIX_PATH='${REMOTE_PREFIX}:/data/local/tmp/ohos-prefix'; export ROS_DOMAIN_ID='${domain}'; export RMW_IMPLEMENTATION='rmw_mdds_cpp'; export RMW_MDDS_BROKER=1; export RMW_MDDS_BROKER_SOCKET='${socket}'; export RMW_MDDS_BRIDGE_LIBRARY='/data/local/tmp/libmdds_bridge_shared.z.so';
EOF
}

stop_remote_daemon() {
  local device_id="$1"
  local domain="$2"
  local socket="$3"
  local env
  env="$(remote_env "${domain}" "${socket}")"
  capture_hdc_shell "${device_id}" \
    "${env} timeout 15 '${ROS2_BIN}' daemon stop >/dev/null 2>&1 || true; echo DAEMON_STOP_DONE" \
    >/dev/null || true
}

kill_remote_pattern() {
  local device_id="$1"
  local pattern="$2"
  capture_hdc_shell "${device_id}" \
    "ps -ef | grep '${pattern}' | grep -v grep | while read -r user pid rest; do [ -z \"\${pid}\" ] || kill -9 \"\${pid}\" 2>/dev/null || true; done" >/dev/null || true
}

stop_remote_pid() {
  local device_id="$1"
  local pid_file="$2"
  local signal="${3:-2}"
  capture_hdc_shell "${device_id}" \
    "pid=\$(cat '${pid_file}' 2>/dev/null || true); if [ -n \"\${pid}\" ]; then kill -${signal} \"\${pid}\" 2>/dev/null || true; i=0; while kill -0 \"\${pid}\" 2>/dev/null && [ \"\${i}\" -lt 50 ]; do sleep 0.2; i=\$((i + 1)); done; kill -9 \"\${pid}\" 2>/dev/null || true; fi" >/dev/null || true
}

remove_remote_runtime_files() {
  local device_id="$1"
  capture_hdc_shell "${device_id}" \
    "rm -f '${RECORD_SOCKET}' '${PLAY_SOCKET}' '${RECORDER_PID}' '${RECORD_SERVER_PID}' '${PLAY_SERVER_PID}'" \
    >/dev/null || true
}

cleanup() {
  stop_remote_daemon "${CLIENT_DEVICE_ID}" "${RECORD_DOMAIN}" "${RECORD_SOCKET}"
  stop_remote_daemon "${CLIENT_DEVICE_ID}" "${PLAY_DOMAIN}" "${PLAY_SOCKET}"
  stop_remote_pid "${CLIENT_DEVICE_ID}" "${RECORDER_PID}" 2
  stop_remote_pid "${SERVER_DEVICE_ID}" "${RECORD_SERVER_PID}" 2
  stop_remote_pid "${SERVER_DEVICE_ID}" "${PLAY_SERVER_PID}" 2
  kill_remote_pattern "${CLIENT_DEVICE_ID}" "rmw_mdds_action_bag_probe client"
  kill_remote_pattern "${SERVER_DEVICE_ID}" "rmw_mdds_action_bag_probe server"
  kill_remote_pattern "${CLIENT_DEVICE_ID}" "bag record -s sqlite3 --actions ${ACTION_NAME}"
  kill_remote_pattern "${CLIENT_DEVICE_ID}" "rmw_mdds_broker --socket ${RECORD_SOCKET}"
  kill_remote_pattern "${CLIENT_DEVICE_ID}" "rmw_mdds_broker --socket ${PLAY_SOCKET}"
  kill_remote_pattern "${SERVER_DEVICE_ID}" "rmw_mdds_broker --socket ${RECORD_SOCKET}"
  kill_remote_pattern "${SERVER_DEVICE_ID}" "rmw_mdds_broker --socket ${PLAY_SOCKET}"
  remove_remote_runtime_files "${CLIENT_DEVICE_ID}"
  remove_remote_runtime_files "${SERVER_DEVICE_ID}"
}
trap cleanup EXIT

wait_for_marker() {
  local device_id="$1"
  local log_file="$2"
  local marker="$3"
  local attempts="${4:-80}"
  local output
  for ((i = 0; i < attempts; i += 1)); do
    output="$(capture_hdc_shell "${device_id}" "test -f '${log_file}' && grep -F '${marker}' '${log_file}' || true")"
    if grep -Fq "${marker}" <<<"${output}"; then
      return 0
    fi
    sleep 0.25
  done
  echo "timed out waiting for ${marker} in ${log_file}" >&2
  capture_hdc_shell "${device_id}" "test -f '${log_file}' && cat '${log_file}' || true" >&2 || true
  return 1
}

wait_for_action() {
  local domain="$1"
  local socket="$2"
  local env output topic_output
  env="$(remote_env "${domain}" "${socket}")"
  stop_remote_daemon "${CLIENT_DEVICE_ID}" "${domain}" "${socket}"
  for ((i = 1; i <= DISCOVERY_TRIES; i += 1)); do
    output="$(capture_hdc_shell "${CLIENT_DEVICE_ID}" \
      "${env} timeout 20 '${ROS2_BIN}' action list -t 2>&1; rc=\$?; echo BOARD_RC=\${rc}")"
    if grep -Fq "${ACTION_NAME} [action_tutorials_interfaces/action/Fibonacci]" <<<"${output}" &&
        grep -q 'BOARD_RC=0' <<<"${output}"; then
      topic_output="$(capture_hdc_shell "${CLIENT_DEVICE_ID}" \
        "${env} timeout 20 '${ROS2_BIN}' topic list -t --include-hidden-topics 2>&1; rc=\$?; echo BOARD_RC=\${rc}")"
      if grep -Fq "${ACTION_NAME}/_action/feedback" <<<"${topic_output}" &&
          grep -Fq "${ACTION_NAME}/_action/status" <<<"${topic_output}" &&
          grep -Fq "${ACTION_NAME}/_action/send_goal/_service_event" <<<"${topic_output}" &&
          grep -Fq "${ACTION_NAME}/_action/cancel_goal/_service_event" <<<"${topic_output}" &&
          grep -Fq "${ACTION_NAME}/_action/get_result/_service_event" <<<"${topic_output}" &&
          grep -q 'BOARD_RC=0' <<<"${topic_output}"; then
        sleep 2
        return 0
      fi
    fi
    sleep 1
  done
  echo "action server did not become visible on the client board" >&2
  echo "${output}" >&2
  return 1
}

start_server() {
  local domain="$1"
  local socket="$2"
  local log_file="$3"
  local pid_file="$4"
  local env
  env="$(remote_env "${domain}" "${socket}")"
  capture_hdc_shell "${SERVER_DEVICE_ID}" \
    "mkdir -p '${WORK_DIR}/roslogs'; rm -f '${socket}' '${log_file}' '${pid_file}'; nohup sh -c \"${env} exec '${PROBE}' server > '${log_file}' 2>&1\" >/dev/null 2>&1 & echo \$! > '${pid_file}'; echo SERVER_STARTED" >/dev/null
  wait_for_marker "${SERVER_DEVICE_ID}" "${log_file}" 'ACTION_BAG_SERVER_READY'
}

for device_id in "${CLIENT_DEVICE_ID}" "${SERVER_DEVICE_ID}"; do
  require_remote_file "${device_id}" "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
  require_remote_file "${device_id}" "${REMOTE_PREFIX}/lib/librcl_action.so"
  require_remote_file "${device_id}" "${REMOTE_PREFIX}/lib/librclcpp_action.so"
  require_remote_file "${device_id}" "${PROBE}"
  require_remote_file "${device_id}" "/data/local/tmp/libmdds_bridge_shared.z.so"
done
require_remote_file "${CLIENT_DEVICE_ID}" "${ROS2_BIN}"
require_remote_file "${CLIENT_DEVICE_ID}" "${REMOTE_PREFIX}/lib/librosbag2_transport.so"
require_remote_file "${CLIENT_DEVICE_ID}" "${REMOTE_PREFIX}/lib/librosbag2_storage_sqlite3.so"
require_remote_file "${CLIENT_DEVICE_ID}" "${REMOTE_PREFIX}/lib/python3.12/site-packages/rosbag2_py/_transport.so"
require_remote_file "${CLIENT_DEVICE_ID}" "${REMOTE_PREFIX}/lib/python3.12/site-packages/ros2bag/verb/record.py"

cleanup
capture_hdc_shell "${CLIENT_DEVICE_ID}" \
  "mkdir -p '${WORK_DIR}/roslogs'; rm -rf '${BAG_DIR}'; rm -f '${WORK_DIR}'/*.log '${WORK_DIR}'/*.pid '${RECORD_SOCKET}' '${PLAY_SOCKET}'" >/dev/null
capture_hdc_shell "${SERVER_DEVICE_ID}" \
  "mkdir -p '${WORK_DIR}/roslogs'; rm -f '${WORK_DIR}'/*.log '${WORK_DIR}'/*.pid '${RECORD_SOCKET}' '${PLAY_SOCKET}'" >/dev/null

start_server "${RECORD_DOMAIN}" "${RECORD_SOCKET}" "${RECORD_SERVER_LOG}" "${RECORD_SERVER_PID}"

record_env="$(remote_env "${RECORD_DOMAIN}" "${RECORD_SOCKET}")"
capture_hdc_shell "${CLIENT_DEVICE_ID}" \
  "nohup sh -c \"${record_env} exec '${ROS2_BIN}' bag record -s sqlite3 --actions '${ACTION_NAME}' -o '${BAG_DIR}' --disable-keyboard-controls > '${RECORD_LOG}' 2>&1\" >/dev/null 2>&1 & echo \$! > '${RECORDER_PID}'; echo RECORDER_STARTED" >/dev/null
wait_for_marker "${CLIENT_DEVICE_ID}" "${RECORD_LOG}" "${ACTION_NAME}/_action/feedback"
wait_for_marker "${CLIENT_DEVICE_ID}" "${RECORD_LOG}" "${ACTION_NAME}/_action/status"
wait_for_marker "${CLIENT_DEVICE_ID}" "${RECORD_LOG}" "${ACTION_NAME}/_action/send_goal/_service_event"
wait_for_marker "${CLIENT_DEVICE_ID}" "${RECORD_LOG}" "${ACTION_NAME}/_action/cancel_goal/_service_event"
wait_for_marker "${CLIENT_DEVICE_ID}" "${RECORD_LOG}" "${ACTION_NAME}/_action/get_result/_service_event"
wait_for_marker "${CLIENT_DEVICE_ID}" "${RECORD_LOG}" 'Recording...'
sleep 3

client_output="$(capture_hdc_shell "${CLIENT_DEVICE_ID}" \
  "${record_env} timeout 90 '${PROBE}' client > '${CLIENT_LOG}' 2>&1; rc=\$?; cat '${CLIENT_LOG}'; echo BOARD_RC=\${rc}")"
printf '%s\n' "${client_output}"
grep -q 'BOARD_RC=0' <<<"${client_output}"
grep -q 'ACTION_BAG_CLIENT_PASS|completed=1|canceled=1|feedback=1' <<<"${client_output}"

sleep 1
stop_remote_pid "${CLIENT_DEVICE_ID}" "${RECORDER_PID}" 2
wait_for_marker "${CLIENT_DEVICE_ID}" "${RECORD_LOG}" 'Recording stopped'

info_output="$(capture_hdc_shell "${CLIENT_DEVICE_ID}" \
  "${record_env} timeout 60 '${ROS2_BIN}' bag info --verbose '${BAG_DIR}' > '${INFO_LOG}' 2>&1; rc=\$?; cat '${INFO_LOG}'; echo BOARD_RC=\${rc}")"
printf '%s\n' "${info_output}"
grep -q 'BOARD_RC=0' <<<"${info_output}"
grep -Eq 'Actions:[[:space:]]*1' <<<"${info_output}"
grep -Fq "Action: ${ACTION_NAME} | Type: action_tutorials_interfaces/action/Fibonacci" <<<"${info_output}"
grep -Eq 'Topic: feedback \| Count: [1-9][0-9]*' <<<"${info_output}"
grep -Fq 'Service: send_goal | Request Count: 2 | Response Count: 2' <<<"${info_output}"
grep -Fq 'Service: cancel_goal | Request Count: 1 | Response Count: 1' <<<"${info_output}"
grep -Fq 'Service: get_result | Request Count: 2 | Response Count: 2' <<<"${info_output}"

stop_remote_pid "${SERVER_DEVICE_ID}" "${RECORD_SERVER_PID}" 2
kill_remote_pattern "${CLIENT_DEVICE_ID}" "rmw_mdds_broker --socket ${RECORD_SOCKET}"
kill_remote_pattern "${SERVER_DEVICE_ID}" "rmw_mdds_broker --socket ${RECORD_SOCKET}"
capture_hdc_shell "${CLIENT_DEVICE_ID}" "rm -f '${RECORD_SOCKET}'" >/dev/null
capture_hdc_shell "${SERVER_DEVICE_ID}" "rm -f '${RECORD_SOCKET}'" >/dev/null
sleep 2

start_server "${PLAY_DOMAIN}" "${PLAY_SOCKET}" "${PLAY_SERVER_LOG}" "${PLAY_SERVER_PID}"
wait_for_action "${PLAY_DOMAIN}" "${PLAY_SOCKET}"

play_env="$(remote_env "${PLAY_DOMAIN}" "${PLAY_SOCKET}")"
play_output="$(capture_hdc_shell "${CLIENT_DEVICE_ID}" \
  "${play_env} timeout 60 '${ROS2_BIN}' bag play '${BAG_DIR}' --send-actions-as-client --disable-keyboard-controls > '${PLAY_LOG}' 2>&1; rc=\$?; cat '${PLAY_LOG}'; echo BOARD_RC=\${rc}")"
printf '%s\n' "${play_output}"
grep -q 'BOARD_RC=0' <<<"${play_output}"
if grep -Eq '\[(WARN|ERROR|FATAL)\]|terminate called|Aborted|Signal 11' <<<"${play_output}"; then
  echo "action playback emitted a failure marker" >&2
  exit 1
fi

wait_for_marker "${SERVER_DEVICE_ID}" "${PLAY_SERVER_LOG}" 'ACTION_BAG_SERVER_GOAL|count=2'
wait_for_marker "${SERVER_DEVICE_ID}" "${PLAY_SERVER_LOG}" 'ACTION_BAG_SERVER_CANCEL|count=1'
play_server_output="$(capture_hdc_shell "${SERVER_DEVICE_ID}" "cat '${PLAY_SERVER_LOG}'")"
printf '%s\n' "${play_server_output}"
if grep -q 'ACTION_BAG_SERVER_GOAL|count=3' <<<"${play_server_output}"; then
  echo "playback delivered an unexpected third goal" >&2
  exit 1
fi

stop_remote_pid "${SERVER_DEVICE_ID}" "${PLAY_SERVER_PID}" 2
echo "RESULT|rmw_mdds_cross_board_action_bag|PASS|rmw=rmw_mdds_cpp|actions=1|send_goal=2|cancel_goal=1|get_result=2"
echo "ACTION_BAG_CLIENT_EVIDENCE=${WORK_DIR}"
echo "ACTION_BAG_SERVER_EVIDENCE=${WORK_DIR}"
echo "ACTION_BAG_URI=${BAG_DIR}"
echo "rmw_mdds_cross_board_action_bag_ok"
