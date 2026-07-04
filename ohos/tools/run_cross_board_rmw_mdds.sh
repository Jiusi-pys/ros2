#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_cross_board_rmw_mdds.sh <device-a-id> <device-b-id> [domain-id]

Runs a dual-RK3588A ROS 2 std_msgs/String native MDDS RMW smoke:
  A: RMW_IMPLEMENTATION=rmw_mdds_cpp through broker-owned MDDS bridge
  B: RMW_IMPLEMENTATION=rmw_mdds_cpp through broker-owned MDDS bridge

Environment:
  HDC_BIN                         HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX         ROS 2 prefix on both devices, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_BRIDGE_LIBRARY         MDDS bridge library, default: /data/local/tmp/libmdds_bridge_shared.z.so
  RMW_MDDS_TOPIC                  Topic name, default: /rmw_mdds_native_chatter
  RMW_MDDS_POLL_TIMEOUT_SECONDS   Poll timeout per direction, default: 90
  RMW_MDDS_PUBLISH_COUNT          Number of messages to publish per direction, default: 30
  RMW_MDDS_PUBLISH_RATE           Publish rate in Hz, default: 2
  RMW_MDDS_HDC_TIMEOUT_SECONDS    HDC shell timeout, default: 120
EOF
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

DEVICE_A_ID="$1"
DEVICE_B_ID="$2"
DOMAIN_ID="${3:-${ROS_DOMAIN_ID:-91}}"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
BRIDGE_LIBRARY="${RMW_MDDS_BRIDGE_LIBRARY:-${REMOTE_PREFIX}/lib/libmdds_bridge_shared.z.so}"
TOPIC_NAME="${RMW_MDDS_TOPIC:-/rmw_mdds_native_chatter}"
POLL_TIMEOUT_SECONDS="${RMW_MDDS_POLL_TIMEOUT_SECONDS:-90}"
PUBLISH_COUNT="${RMW_MDDS_PUBLISH_COUNT:-30}"
PUBLISH_RATE="${RMW_MDDS_PUBLISH_RATE:-2}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-120}"
LOG_DIR="${RMW_MDDS_LOG_DIR:-/data/local/tmp/rmw_mdds_cross_board}"

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
  local device_id="$1"
  local path="$2"
  local output
  output="$(capture_hdc_shell "${device_id}" "test -e '${path}' && echo OK || echo MISSING:${path}")"
  if ! grep -q '^OK$' <<< "${output}"; then
    echo "${output}" >&2
    exit 1
  fi
}

cleanup_device() {
  local device_id="$1"
  capture_hdc_shell "${device_id}" \
    "pkill -f 'topic echo ${TOPIC_NAME}' 2>/dev/null || true; pkill -f 'topic pub .*${TOPIC_NAME}' 2>/dev/null || true" >/dev/null || true
}

run_direction() {
  local tag="$1"
  local sub_device="$2"
  local pub_device="$3"
  local payload="$4"
  local echo_log="${LOG_DIR}/${tag}_echo.log"
  local pub_log="${LOG_DIR}/${tag}_pub.log"

  capture_hdc_shell "${sub_device}" \
    "mkdir -p '${LOG_DIR}'; rm -f '${echo_log}'; nohup sh -c 'ROS_DOMAIN_ID=${DOMAIN_ID} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BRIDGE_LIBRARY} ${REMOTE_PREFIX}/bin/ros2 topic echo ${TOPIC_NAME} std_msgs/msg/String --no-daemon > ${echo_log} 2>&1' >/dev/null 2>&1 & echo ${tag}_echo_started" >/dev/null
  sleep 5
  capture_hdc_shell "${pub_device}" \
    "mkdir -p '${LOG_DIR}'; rm -f '${pub_log}'; nohup sh -c 'ROS_DOMAIN_ID=${DOMAIN_ID} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BRIDGE_LIBRARY} ${REMOTE_PREFIX}/bin/ros2 topic pub --times ${PUBLISH_COUNT} -r ${PUBLISH_RATE} -w 0 ${TOPIC_NAME} std_msgs/msg/String '\\''{data: ${payload}}'\\'' > ${pub_log} 2>&1' >/dev/null 2>&1 & echo ${tag}_pub_started" >/dev/null

  local start_ts
  start_ts="$(date +%s)"
  while (( $(date +%s) - start_ts < POLL_TIMEOUT_SECONDS )); do
    local output
    output="$(capture_hdc_shell "${sub_device}" "cat '${echo_log}' 2>/dev/null || true" || true)"
    if grep -q "${payload}" <<< "${output}"; then
      printf '%s\n' "${output}"
      case "${tag}" in
        mdds_a_to_b)
          echo "RESULT|mdds_a_to_b|PASS|${payload}"
          ;;
        mdds_b_to_a)
          echo "RESULT|mdds_b_to_a|PASS|${payload}"
          ;;
        *)
          echo "RESULT|${tag}|PASS|${payload}"
          ;;
      esac
      capture_hdc_shell "${sub_device}" \
        "pkill -f 'topic echo ${TOPIC_NAME}' 2>/dev/null || true" >/dev/null || true
      return 0
    fi
    sleep 1
  done

  echo "RESULT|${tag}|FAIL|${payload}" >&2
  echo "--- ${tag} echo log ---" >&2
  capture_hdc_shell "${sub_device}" "cat '${echo_log}' 2>/dev/null || true" >&2 || true
  echo "--- ${tag} pub log ---" >&2
  capture_hdc_shell "${pub_device}" "cat '${pub_log}' 2>/dev/null || true" >&2 || true
  capture_hdc_shell "${sub_device}" \
    "pkill -f 'topic echo ${TOPIC_NAME}' 2>/dev/null || true" >/dev/null || true
  return 1
}

require_remote_file "${DEVICE_A_ID}" "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${DEVICE_A_ID}" "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${DEVICE_A_ID}" "${BRIDGE_LIBRARY}"
require_remote_file "${DEVICE_B_ID}" "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${DEVICE_B_ID}" "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${DEVICE_B_ID}" "${BRIDGE_LIBRARY}"

cleanup_device "${DEVICE_A_ID}"
cleanup_device "${DEVICE_B_ID}"
trap 'cleanup_device "${DEVICE_A_ID}"; cleanup_device "${DEVICE_B_ID}"' EXIT

run_direction \
  "mdds_a_to_b" \
  "${DEVICE_B_ID}" \
  "${DEVICE_A_ID}" \
  "mdds_a_to_b_${DOMAIN_ID}_$(date +%s)"

run_direction \
  "mdds_b_to_a" \
  "${DEVICE_A_ID}" \
  "${DEVICE_B_ID}" \
  "mdds_b_to_a_${DOMAIN_ID}_$(date +%s)"

echo "cross_board_rmw_mdds_ok"
