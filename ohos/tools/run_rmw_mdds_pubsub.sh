#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_pubsub.sh <device-id> [domain-id]

Runs a ROS 2 std_msgs/String pub/echo smoke on one RK3588A board with
RMW_IMPLEMENTATION=rmw_mdds_cpp.

Environment:
  HDC_BIN                         HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX         ROS 2 prefix on device, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_LOCAL_TOPIC            Topic name, default: /rmw_mdds_local_chatter
  RMW_MDDS_PUBLISH_COUNT          Number of messages to publish, default: 1
  RMW_MDDS_PUBLISH_RATE           Publish rate in Hz, default: 2
  RMW_MDDS_POLL_TIMEOUT_SECONDS   Poll timeout, default: 120
  RMW_MDDS_HDC_TIMEOUT_SECONDS    HDC shell timeout, default: 180
EOF
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage
  exit 2
fi

DEVICE_ID="$1"
DOMAIN_ID="${2:-${ROS_DOMAIN_ID:-88}}"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
TOPIC_NAME="${RMW_MDDS_LOCAL_TOPIC:-/rmw_mdds_local_chatter}"
PUBLISH_COUNT="${RMW_MDDS_PUBLISH_COUNT:-1}"
PUBLISH_RATE="${RMW_MDDS_PUBLISH_RATE:-2}"
POLL_TIMEOUT_SECONDS="${RMW_MDDS_POLL_TIMEOUT_SECONDS:-120}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-180}"
LOG_DIR="${RMW_MDDS_LOG_DIR:-/data/local/tmp/rmw_mdds_pubsub}"
ECHO_LOG="${LOG_DIR}/echo.log"
PUB_LOG="${LOG_DIR}/pub.log"
PAYLOAD="rmw_mdds_local_${DOMAIN_ID}_$(date +%s)"

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

wait_for_payload() {
  local start_ts
  start_ts="$(date +%s)"
  while (( $(date +%s) - start_ts < POLL_TIMEOUT_SECONDS )); do
    local output
    output="$(capture_hdc_shell "${DEVICE_ID}" "cat '${ECHO_LOG}' 2>/dev/null || true")"
    if grep -q "${PAYLOAD}" <<< "${output}"; then
      printf '%s\n' "${output}"
      echo "RESULT|rmw_mdds_pubsub|PASS|${PAYLOAD}"
      return 0
    fi
    sleep 1
  done
  echo "RESULT|rmw_mdds_pubsub|FAIL|${PAYLOAD}" >&2
  echo "--- echo log ---" >&2
  capture_hdc_shell "${DEVICE_ID}" "cat '${ECHO_LOG}' 2>/dev/null || true" >&2 || true
  echo "--- pub log ---" >&2
  capture_hdc_shell "${DEVICE_ID}" "cat '${PUB_LOG}' 2>/dev/null || true" >&2 || true
  return 1
}

require_remote_file "${REMOTE_PREFIX}/bin/ros2"

capture_hdc_shell "${DEVICE_ID}" \
  "ps -ef | grep -E '[r]os2 topic echo|[r]os2 topic pub|[r]os2-daemon.*rmw_mdds_cpp|[r]mw_mdds_broker_trigger_server.py|[r]mw_mdds_broker' | while read -r user pid rest; do kill -9 \"\${pid}\" 2>/dev/null || true; done; rm -f /data/local/tmp/rmw_mdds_cpp.sock; mkdir -p '${LOG_DIR}' && rm -f '${ECHO_LOG}' '${PUB_LOG}'" >/dev/null
capture_hdc_shell "${DEVICE_ID}" \
  "nohup sh -c 'ROS_DOMAIN_ID=${DOMAIN_ID} RMW_IMPLEMENTATION=rmw_mdds_cpp ${REMOTE_PREFIX}/bin/ros2 topic echo ${TOPIC_NAME} std_msgs/msg/String --once --no-daemon > ${ECHO_LOG} 2>&1' >/dev/null 2>&1 & echo rmw_mdds_echo_started" >/dev/null
sleep 5
capture_hdc_shell "${DEVICE_ID}" \
  "ROS_DOMAIN_ID=${DOMAIN_ID} RMW_IMPLEMENTATION=rmw_mdds_cpp ${REMOTE_PREFIX}/bin/ros2 topic pub --times ${PUBLISH_COUNT} -r ${PUBLISH_RATE} -w 0 ${TOPIC_NAME} std_msgs/msg/String '{data: ${PAYLOAD}}' > ${PUB_LOG} 2>&1" >/dev/null

wait_for_payload
echo "rmw_mdds_pubsub_ok"
