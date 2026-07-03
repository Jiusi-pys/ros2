#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_broker_pubsub.sh <device-id> [domain-id]

Runs a same-board ROS 2 std_msgs/String smoke through one rmw_mdds_broker
process and two independent rmw_mdds_cpp client processes.

Environment:
  HDC_BIN                         HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX         ROS 2 prefix on device, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_BROKER_SOCKET          Broker socket path, default: /data/local/tmp/rmw_mdds_cpp.sock
  RMW_MDDS_BROKER_TOPIC           Topic name, default: /rmw_mdds_broker_chatter
  RMW_MDDS_PUBLISH_COUNT          Number of messages to publish, default: 20
  RMW_MDDS_PUBLISH_RATE           Publish rate in Hz, default: 2
  RMW_MDDS_POLL_TIMEOUT_SECONDS   Poll timeout, default: 60
  RMW_MDDS_HDC_TIMEOUT_SECONDS    HDC shell timeout, default: 120
EOF
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage
  exit 2
fi

DEVICE_ID="$1"
DOMAIN_ID="${2:-${ROS_DOMAIN_ID:-188}}"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
BROKER_BIN="${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker"
BROKER_SOCKET="${RMW_MDDS_BROKER_SOCKET:-/data/local/tmp/rmw_mdds_cpp.sock}"
TOPIC_NAME="${RMW_MDDS_BROKER_TOPIC:-/rmw_mdds_broker_chatter}"
PUBLISH_COUNT="${RMW_MDDS_PUBLISH_COUNT:-20}"
PUBLISH_RATE="${RMW_MDDS_PUBLISH_RATE:-2}"
POLL_TIMEOUT_SECONDS="${RMW_MDDS_POLL_TIMEOUT_SECONDS:-60}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-120}"
LOG_DIR="${RMW_MDDS_LOG_DIR:-/data/local/tmp/rmw_mdds_broker_pubsub}"
BROKER_LOG="${LOG_DIR}/broker.log"
ECHO_LOG="${LOG_DIR}/echo.log"
PUB_LOG="${LOG_DIR}/pub.log"
ROS_LOG_DIR_REMOTE="${LOG_DIR}/roslog"
REMOTE_LD_LIBRARY_PATH="${REMOTE_PREFIX}/lib:${REMOTE_PREFIX}/lib/rmw_mdds_cpp"
PAYLOAD="rmw_mdds_broker_${DOMAIN_ID}_$(date +%s)"
DEFAULT_RMW_MDDS_BRIDGE_LIBRARY="/no/such/libmdds_bridge_shared.z.so"
INVALID_BRIDGE_LIBRARY="${RMW_MDDS_INVALID_BRIDGE_LIBRARY:-${DEFAULT_RMW_MDDS_BRIDGE_LIBRARY}}"

capture_hdc_shell() {
  local device_id="$1"
  local command="$2"
  local output_file
  output_file="$(mktemp)"
  set +e
  timeout "${HDC_TIMEOUT_SECONDS}s" "${HDC_BIN}" -t "${device_id}" shell "${command}" >"${output_file}" 2>&1
  local status=$?
  set -e
  cat "${output_file}"
  rm -f "${output_file}"
  # Some local HDC builds complete the device command and then exit 139.
  [[ ${status} -eq 0 || ${status} -eq 139 ]]
}

require_remote_file() {
  local path="$1"
  local output
  output="$(capture_hdc_shell "${DEVICE_ID}" "test -e '${path}' && echo OK || echo MISSING:${path}")"
  if ! grep -q '^OK$' <<< "${output}"; then
    echo "${output}" >&2
    exit 1
  fi
}

cleanup_remote_processes() {
  capture_hdc_shell "${DEVICE_ID}" \
    "pkill -f 'rmw_mdds_broker --socket ${BROKER_SOCKET}' 2>/dev/null || true; pkill -f 'topic echo ${TOPIC_NAME}' 2>/dev/null || true; pkill -f 'topic pub .*${TOPIC_NAME}' 2>/dev/null || true" >/dev/null || true
}

wait_for_socket() {
  local start_ts
  start_ts="$(date +%s)"
  while (( $(date +%s) - start_ts < POLL_TIMEOUT_SECONDS )); do
    local output
    output="$(capture_hdc_shell "${DEVICE_ID}" "test -e '${BROKER_SOCKET}' && echo OK || true")"
    if grep -q '^OK$' <<< "${output}"; then
      return 0
    fi
    sleep 1
  done
  echo "RESULT|rmw_mdds_broker_pubsub|FAIL|broker_socket_missing" >&2
  capture_hdc_shell "${DEVICE_ID}" "cat '${BROKER_LOG}' 2>/dev/null || true" >&2 || true
  return 1
}

wait_for_payload() {
  local start_ts
  start_ts="$(date +%s)"
  while (( $(date +%s) - start_ts < POLL_TIMEOUT_SECONDS )); do
    local output
    output="$(capture_hdc_shell "${DEVICE_ID}" "cat '${ECHO_LOG}' 2>/dev/null || true")"
    if grep -q "${PAYLOAD}" <<< "${output}"; then
      printf '%s\n' "${output}"
      echo "RESULT|rmw_mdds_broker_pubsub|PASS|${PAYLOAD}"
      return 0
    fi
    sleep 1
  done
  echo "RESULT|rmw_mdds_broker_pubsub|FAIL|${PAYLOAD}" >&2
  echo "--- broker log ---" >&2
  capture_hdc_shell "${DEVICE_ID}" "cat '${BROKER_LOG}' 2>/dev/null || true" >&2 || true
  echo "--- echo log ---" >&2
  capture_hdc_shell "${DEVICE_ID}" "cat '${ECHO_LOG}' 2>/dev/null || true" >&2 || true
  echo "--- pub log ---" >&2
  capture_hdc_shell "${DEVICE_ID}" "cat '${PUB_LOG}' 2>/dev/null || true" >&2 || true
  return 1
}

trap cleanup_remote_processes EXIT

require_remote_file "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${BROKER_BIN}"

cleanup_remote_processes
capture_hdc_shell "${DEVICE_ID}" \
  "mkdir -p '${LOG_DIR}' '${ROS_LOG_DIR_REMOTE}' && rm -f '${BROKER_SOCKET}' '${BROKER_LOG}' '${ECHO_LOG}' '${PUB_LOG}' && chmod +x '${BROKER_BIN}'" >/dev/null
capture_hdc_shell "${DEVICE_ID}" \
  "nohup sh -c 'LD_LIBRARY_PATH=${REMOTE_LD_LIBRARY_PATH} ${BROKER_BIN} --socket ${BROKER_SOCKET} > ${BROKER_LOG} 2>&1' >/dev/null 2>&1 & echo rmw_mdds_broker_started" >/dev/null
wait_for_socket

capture_hdc_shell "${DEVICE_ID}" \
  "nohup sh -c 'HOME=/data/local/tmp ROS_LOG_DIR=${ROS_LOG_DIR_REMOTE} LD_LIBRARY_PATH=${REMOTE_LD_LIBRARY_PATH} ROS_DOMAIN_ID=${DOMAIN_ID} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BROKER_SOCKET=${BROKER_SOCKET} RMW_MDDS_BRIDGE_LIBRARY=${INVALID_BRIDGE_LIBRARY} ${REMOTE_PREFIX}/bin/ros2 topic echo ${TOPIC_NAME} std_msgs/msg/String --once --no-daemon > ${ECHO_LOG} 2>&1' >/dev/null 2>&1 & echo rmw_mdds_broker_echo_started" >/dev/null
sleep 3
capture_hdc_shell "${DEVICE_ID}" \
  "HOME=/data/local/tmp ROS_LOG_DIR=${ROS_LOG_DIR_REMOTE} LD_LIBRARY_PATH=${REMOTE_LD_LIBRARY_PATH} ROS_DOMAIN_ID=${DOMAIN_ID} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BROKER_SOCKET=${BROKER_SOCKET} RMW_MDDS_BRIDGE_LIBRARY=${INVALID_BRIDGE_LIBRARY} ${REMOTE_PREFIX}/bin/ros2 topic pub --times ${PUBLISH_COUNT} -r ${PUBLISH_RATE} -w 0 ${TOPIC_NAME} std_msgs/msg/String '{data: ${PAYLOAD}}' > ${PUB_LOG} 2>&1" >/dev/null

wait_for_payload
echo "rmw_mdds_broker_pubsub_ok"
