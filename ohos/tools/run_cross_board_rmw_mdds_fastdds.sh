#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_cross_board_rmw_mdds_fastdds.sh <mdds-device-id> <fastdds-device-id> [domain-id]

Runs a dual-RK3588A ROS 2 std_msgs/String interop smoke:
  A: RMW_IMPLEMENTATION=rmw_mdds_cpp through broker-owned MDDS bridge
  B: RMW_IMPLEMENTATION=rmw_fastrtps_cpp plus mdds_dds_gateway

Environment:
  HDC_BIN                         HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX         ROS 2 prefix on both devices, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_BRIDGE_LIBRARY         MDDS bridge library, default: /data/local/tmp/libmdds_bridge_shared.z.so
  MDDS_GATEWAY_ENV                Gateway env script, default: /data/local/tmp/device_gateway_env.sh
  MDDS_GATEWAY_BIN                Gateway binary, default: /data/local/tmp/mdds_dds_gateway
  MDDS_GATEWAY_CONFIG             Gateway config, default: /data/local/tmp/gateway_rmw_mdds_rt.yaml
  RMW_MDDS_TOPIC                  MDDS-side topic, default: /rt/rmw_mdds_dual_chatter
  RMW_FASTDDS_TOPIC               FastDDS-side topic, default: /rmw_mdds_dual_chatter
  RMW_MDDS_POLL_TIMEOUT_SECONDS   Poll timeout per direction, default: 90
  RMW_MDDS_KEEP_GATEWAY           Keep gateway running after test when set to 1
EOF
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

MDDS_DEVICE_ID="$1"
FASTDDS_DEVICE_ID="$2"
DOMAIN_ID="${3:-${ROS_DOMAIN_ID:-89}}"
if ! [[ "${DOMAIN_ID}" =~ ^[0-9]+$ ]] || (( DOMAIN_ID > 232 )); then
  echo "domain-id must be an integer in the ROS 2 RTPS-safe range 0..232" >&2
  exit 2
fi
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
BRIDGE_LIBRARY="${RMW_MDDS_BRIDGE_LIBRARY:-${REMOTE_PREFIX}/lib/libmdds_bridge_shared.z.so}"
GATEWAY_ENV="${MDDS_GATEWAY_ENV:-/data/local/tmp/device_gateway_env.sh}"
GATEWAY_BIN="${MDDS_GATEWAY_BIN:-/data/local/tmp/mdds_dds_gateway}"
GATEWAY_CONFIG="${MDDS_GATEWAY_CONFIG:-/data/local/tmp/gateway_rmw_mdds_rt.yaml}"
MDDS_TOPIC="${RMW_MDDS_TOPIC:-/rt/rmw_mdds_dual_chatter}"
FASTDDS_TOPIC="${RMW_FASTDDS_TOPIC:-/rmw_mdds_dual_chatter}"
POLL_TIMEOUT_SECONDS="${RMW_MDDS_POLL_TIMEOUT_SECONDS:-90}"
KEEP_GATEWAY="${RMW_MDDS_KEEP_GATEWAY:-0}"
LOG_DIR="${RMW_MDDS_LOG_DIR:-/data/local/tmp/rmw_mdds_fastdds}"
GATEWAY_LOG="${LOG_DIR}/gateway.log"

capture_hdc_shell() {
  local device_id="$1"
  local command="$2"
  local output_file
  output_file="$(mktemp)"
  set +e
  timeout 45s "${HDC_BIN}" -t "${device_id}" shell "${command}" >"${output_file}" 2>&1
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

cleanup_gateway() {
  if [[ "${KEEP_GATEWAY}" == "1" ]]; then
    return
  fi
  capture_hdc_shell "${FASTDDS_DEVICE_ID}" \
    "old=\$(pidof mdds_dds_gateway 2>/dev/null); [ -z \"\${old}\" ] || kill -9 \${old}" >/dev/null || true
}
trap cleanup_gateway EXIT

cleanup_mdds_rmw_processes() {
  capture_hdc_shell "${MDDS_DEVICE_ID}" \
    "kill_matches() { pattern=\"\$1\"; ps -ef | grep \"\${pattern}\" | grep -v grep | while read -r user pid rest; do [ -z \"\${pid}\" ] || kill -9 \"\${pid}\" 2>/dev/null || true; done; }; kill_matches 'ros2-daemon .*rmw_mdds_cpp'; kill_matches 'topic echo .*rmw_mdds'; kill_matches 'topic pub .*rmw_mdds'; kill_matches 'mdds_interop_probe'" >/dev/null || true
}

wait_for_log() {
  local device_id="$1"
  local log_path="$2"
  local pattern="$3"
  local start_ts
  start_ts="$(date +%s)"
  while (( $(date +%s) - start_ts < POLL_TIMEOUT_SECONDS )); do
    local output
    output="$(capture_hdc_shell "${device_id}" "cat '${log_path}' 2>/dev/null || true" || true)"
    if grep -qE "${pattern}" <<< "${output}"; then
      printf '%s\n' "${output}"
      return 0
    fi
    sleep 1
  done
  echo "Timed out waiting for ${pattern} in ${log_path}" >&2
  capture_hdc_shell "${device_id}" "cat '${log_path}' 2>/dev/null || true" >&2 || true
  return 1
}

run_direction() {
  local tag="$1"
  local sub_device="$2"
  local sub_rmw="$3"
  local sub_topic="$4"
  local pub_device="$5"
  local pub_rmw="$6"
  local pub_topic="$7"
  local payload="$8"
  local echo_log="${LOG_DIR}/${tag}_echo.log"
  local pub_log="${LOG_DIR}/${tag}_pub.log"
  local sub_bridge_env=""
  local pub_bridge_env=""
  local echo_once_arg=" --once"

  if [[ "${sub_rmw}" == "rmw_mdds_cpp" ]]; then
    sub_bridge_env=" RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BRIDGE_LIBRARY}"
    echo_once_arg=""
  fi
  if [[ "${pub_rmw}" == "rmw_mdds_cpp" ]]; then
    pub_bridge_env=" RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BRIDGE_LIBRARY}"
  fi

  capture_hdc_shell "${sub_device}" \
    "mkdir -p '${LOG_DIR}'; rm -f '${echo_log}'; nohup sh -c 'ROS_DOMAIN_ID=${DOMAIN_ID} RMW_IMPLEMENTATION=${sub_rmw}${sub_bridge_env} ${REMOTE_PREFIX}/bin/ros2 topic echo ${sub_topic} std_msgs/msg/String${echo_once_arg} --no-daemon > ${echo_log} 2>&1' >/dev/null 2>&1 & echo ${tag}_echo_started" >/dev/null
  sleep 5
  capture_hdc_shell "${pub_device}" \
    "mkdir -p '${LOG_DIR}'; rm -f '${pub_log}'; nohup sh -c 'ROS_DOMAIN_ID=${DOMAIN_ID} RMW_IMPLEMENTATION=${pub_rmw}${pub_bridge_env} ${REMOTE_PREFIX}/bin/ros2 topic pub --times 20 -r 2 -w 0 ${pub_topic} std_msgs/msg/String '\\''{data: ${payload}}'\\'' > ${pub_log} 2>&1' >/dev/null 2>&1 & echo ${tag}_pub_started" >/dev/null

  local start_ts
  start_ts="$(date +%s)"
  while (( $(date +%s) - start_ts < POLL_TIMEOUT_SECONDS )); do
    local output
    output="$(capture_hdc_shell "${sub_device}" "cat '${echo_log}' 2>/dev/null || true" || true)"
    if grep -q "${payload}" <<< "${output}"; then
      printf '%s\n' "${output}"
      case "${tag}" in
        mdds_to_fastdds)
          echo "RESULT|mdds_to_fastdds|PASS|${payload}"
          ;;
        fastdds_to_mdds)
          echo "RESULT|fastdds_to_mdds|PASS|${payload}"
          ;;
        *)
          echo "RESULT|${tag}|PASS|${payload}"
          ;;
      esac
      capture_hdc_shell "${sub_device}" \
        "pkill -f 'topic echo ${sub_topic}' 2>/dev/null || true" >/dev/null || true
      return 0
    fi
    sleep 1
  done

  echo "RESULT|${tag}|FAIL|${payload}" >&2
  echo "--- ${tag} echo log ---" >&2
  capture_hdc_shell "${sub_device}" "cat '${echo_log}' 2>/dev/null || true" >&2 || true
  echo "--- ${tag} pub log ---" >&2
  capture_hdc_shell "${pub_device}" "cat '${pub_log}' 2>/dev/null || true" >&2 || true
  echo "--- gateway log ---" >&2
  capture_hdc_shell "${FASTDDS_DEVICE_ID}" "cat '${GATEWAY_LOG}' 2>/dev/null || true" >&2 || true
  capture_hdc_shell "${sub_device}" \
    "pkill -f 'topic echo ${sub_topic}' 2>/dev/null || true" >/dev/null || true
  return 1
}

require_remote_file "${MDDS_DEVICE_ID}" "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${MDDS_DEVICE_ID}" "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${MDDS_DEVICE_ID}" "${BRIDGE_LIBRARY}"
require_remote_file "${FASTDDS_DEVICE_ID}" "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${FASTDDS_DEVICE_ID}" "${REMOTE_PREFIX}/lib/librmw_fastrtps_cpp.so"
require_remote_file "${FASTDDS_DEVICE_ID}" "${GATEWAY_ENV}"
require_remote_file "${FASTDDS_DEVICE_ID}" "${GATEWAY_BIN}"
require_remote_file "${FASTDDS_DEVICE_ID}" "${GATEWAY_CONFIG}"
require_remote_file "${FASTDDS_DEVICE_ID}" "${BRIDGE_LIBRARY}"

cleanup_mdds_rmw_processes
capture_hdc_shell "${MDDS_DEVICE_ID}" \
  "pkill -f 'topic echo ${MDDS_TOPIC}' 2>/dev/null || true; pkill -f 'topic pub .*${MDDS_TOPIC}' 2>/dev/null || true" >/dev/null
capture_hdc_shell "${FASTDDS_DEVICE_ID}" \
  "pkill -f 'topic echo ${FASTDDS_TOPIC}' 2>/dev/null || true; pkill -f 'topic pub .*${FASTDDS_TOPIC}' 2>/dev/null || true" >/dev/null
capture_hdc_shell "${FASTDDS_DEVICE_ID}" \
  "mkdir -p '${LOG_DIR}' && rm -f '${GATEWAY_LOG}' && old=\$(pidof mdds_dds_gateway 2>/dev/null); [ -z \"\${old}\" ] || kill -9 \${old}; nohup sh -c 'ROS_DOMAIN_ID=${DOMAIN_ID} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${GATEWAY_ENV} ${GATEWAY_BIN} ${GATEWAY_CONFIG} > ${GATEWAY_LOG} 2>&1' >/dev/null 2>&1 &" >/dev/null
wait_for_log "${FASTDDS_DEVICE_ID}" "${GATEWAY_LOG}" "gateway started" >/dev/null

run_direction \
  "mdds_to_fastdds" \
  "${FASTDDS_DEVICE_ID}" "rmw_fastrtps_cpp" "${FASTDDS_TOPIC}" \
  "${MDDS_DEVICE_ID}" "rmw_mdds_cpp" "${MDDS_TOPIC}" \
  "mdds_to_fastdds_${DOMAIN_ID}_$(date +%s)"

run_direction \
  "fastdds_to_mdds" \
  "${MDDS_DEVICE_ID}" "rmw_mdds_cpp" "${MDDS_TOPIC}" \
  "${FASTDDS_DEVICE_ID}" "rmw_fastrtps_cpp" "${FASTDDS_TOPIC}" \
  "fastdds_to_mdds_${DOMAIN_ID}_$(date +%s)"

capture_hdc_shell "${FASTDDS_DEVICE_ID}" "cat '${GATEWAY_LOG}' 2>/dev/null | tail -40" || true
echo "cross_board_rmw_mdds_fastdds_ok"
