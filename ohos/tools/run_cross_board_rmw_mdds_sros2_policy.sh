#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_cross_board_rmw_mdds_sros2_policy.sh <subscriber-device-id> <publisher-device-id> [domain-id]

Runs board-side SROS2 policy evidence for rmw_mdds_cpp over the native MDDS
transport:
  * positive: authorized std_msgs/String topic crosses from publisher board to subscriber board
  * negative: unauthorized publisher creation is denied before sending data
  * scope: local XML topic policy only; protected/signed DDS Security artifacts
    are covered by the full-parity protected-governance rejection gate

Environment:
  HDC_BIN                         HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX         ROS 2 prefix on both devices, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_BRIDGE_LIBRARY         MDDS bridge library, default: /data/local/tmp/libmdds_bridge_shared.z.so
  RMW_MDDS_SROS2_ALLOWED_TOPIC    Authorized topic, default: /mdds_sros2_allowed
  RMW_MDDS_SROS2_FORBIDDEN_TOPIC  Unauthorized topic, default: /mdds_sros2_forbidden
  RMW_MDDS_HDC_TIMEOUT_SECONDS    HDC shell timeout, default: 120
  RMW_MDDS_HDC_RETRY_ATTEMPTS     HDC retry attempts, default: 5
EOF
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

SUB_DEVICE_ID="$1"
PUB_DEVICE_ID="$2"
DOMAIN_ID="${3:-${ROS_DOMAIN_ID:-94}}"
[[ "${SUB_DEVICE_ID}" != "${PUB_DEVICE_ID}" ]] || {
  echo "ERROR: subscriber and publisher devices must be distinct boards" >&2
  exit 2
}

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
BRIDGE_LIBRARY="${RMW_MDDS_BRIDGE_LIBRARY:-${REMOTE_PREFIX}/lib/libmdds_bridge_shared.z.so}"
ALLOWED_TOPIC="${RMW_MDDS_SROS2_ALLOWED_TOPIC:-/mdds_sros2_allowed}"
FORBIDDEN_TOPIC="${RMW_MDDS_SROS2_FORBIDDEN_TOPIC:-/mdds_sros2_forbidden}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-120}"
HDC_RETRY_ATTEMPTS="${RMW_MDDS_HDC_RETRY_ATTEMPTS:-5}"
HDC_RETRY_DELAY_SECONDS="${RMW_MDDS_HDC_RETRY_DELAY_SECONDS:-1}"
LOG_DIR="${RMW_MDDS_LOG_DIR:-/data/local/tmp/rmw_mdds_sros2_policy}"
REMOTE_KEYSTORE_ROOT="/data/local/tmp/rmw_mdds_sros2_policy_keystore_${DOMAIN_ID}_$$"
REMOTE_TARBALL="${REMOTE_KEYSTORE_ROOT}.tgz"
HDC_SEND_VERIFY="${ROOT_DIR}/ohos/tools/hdc_send_verify.sh"
ENCLAVE_NAME="rmw_mdds_sros2_authorized"

hdc_output_succeeded() {
  local status="$1"
  local output_file="$2"
  [[ ${status} -eq 0 || ${status} -eq 139 ]] || return 1
  ! grep -qE 'Connect server failed|Connect key failed|No device|device offline|\[Fail\]' "${output_file}"
}

capture_hdc_shell() {
  local device_id="$1"
  local command="$2"
  local attempt
  local output_file
  local status
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
  if ! output="$(capture_hdc_shell "${device_id}" "test -e '${path}' && echo OK || echo MISSING:${path}")"; then
    echo "${output}" >&2
    exit 1
  fi
  if ! grep -q '^OK$' <<<"${output}"; then
    echo "${output}" >&2
    exit 1
  fi
}

topic_to_rtps() {
  local topic="${1#/}"
  printf 'rt/%s' "${topic}"
}

make_keystore_tarball() {
  local tarball="$1"
  local work_dir="$2"
  local enclave_dir="${work_dir}/keystore/enclaves/${ENCLAVE_NAME}"
  mkdir -p "${enclave_dir}"
  cat >"${enclave_dir}/governance.xml" <<EOF
<dds><domain_access_rules><domain_rule>
<domains><id>0</id></domains>
<allow_unauthenticated_participants>false</allow_unauthenticated_participants>
<enable_join_access_control>true</enable_join_access_control>
<discovery_protection_kind>NONE</discovery_protection_kind>
<liveliness_protection_kind>NONE</liveliness_protection_kind>
<rtps_protection_kind>NONE</rtps_protection_kind>
<topic_access_rules><topic_rule>
<topic_expression>$(topic_to_rtps "${ALLOWED_TOPIC}")</topic_expression>
<enable_discovery_protection>false</enable_discovery_protection>
<enable_read_access_control>true</enable_read_access_control>
<enable_write_access_control>true</enable_write_access_control>
<metadata_protection_kind>NONE</metadata_protection_kind>
<data_protection_kind>NONE</data_protection_kind>
</topic_rule></topic_access_rules>
</domain_rule></domain_access_rules></dds>
EOF
  cat >"${enclave_dir}/permissions.xml" <<EOF
<dds><permissions><grant name="${ENCLAVE_NAME}">
<subject_name>CN=${ENCLAVE_NAME}</subject_name>
<validity><not_before>2026-01-01T00:00:00</not_before><not_after>2036-01-01T00:00:00</not_after></validity>
<allow_rule><domains><id>0</id></domains>
<publish><topics>
<topic>$(topic_to_rtps "${ALLOWED_TOPIC}")</topic>
<topic>rt/rosout</topic>
<topic>rt/parameter_events</topic>
</topics></publish>
<subscribe><topics>
<topic>$(topic_to_rtps "${ALLOWED_TOPIC}")</topic>
<topic>rt/parameter_events</topic>
</topics></subscribe>
</allow_rule><default>DENY</default>
</grant></permissions></dds>
EOF
  tar -C "${work_dir}/keystore" -czf "${tarball}" .
}

deploy_keystore() {
  local device_id="$1"
  OHOS_HDC_BIN="${HDC_BIN}" "${HDC_SEND_VERIFY}" "${device_id}" "${KEYSTORE_TARBALL}" "${REMOTE_TARBALL}" >/dev/null
  capture_hdc_shell "${device_id}" \
    "rm -rf '${REMOTE_KEYSTORE_ROOT}' && mkdir -p '${REMOTE_KEYSTORE_ROOT}' && tar xzf '${REMOTE_TARBALL}' -C '${REMOTE_KEYSTORE_ROOT}' && test -e '${REMOTE_KEYSTORE_ROOT}/enclaves/${ENCLAVE_NAME}/governance.xml' && test -e '${REMOTE_KEYSTORE_ROOT}/enclaves/${ENCLAVE_NAME}/permissions.xml' && echo SROS2_KEYSTORE_READY" >/dev/null
}

remote_env() {
  cat <<EOF
PREFIX='${REMOTE_PREFIX}'; UNDERLAY_PREFIX='/data/local/tmp/ohos-prefix'; FASTDDS_PREFIX='/data/local/tmp/ohos-fastdds'; BR='${BRIDGE_LIBRARY}'; VENDOR_LIB_PATH=; for dir in \${PREFIX}/opt/*/lib; do [ -d \${dir} ] && VENDOR_LIB_PATH=\${VENDOR_LIB_PATH:+\${VENDOR_LIB_PATH}:}\${dir}; done; UNDERLAY_VENDOR_LIB_PATH=; for dir in \${UNDERLAY_PREFIX}/opt/*/lib; do [ -d \${dir} ] && UNDERLAY_VENDOR_LIB_PATH=\${UNDERLAY_VENDOR_LIB_PATH:+\${UNDERLAY_VENDOR_LIB_PATH}:}\${dir}; done; export LD_PRELOAD=/data/local/release/usr/lib/libpython3.12.so.1.0; export PYTHONHOME='/data/local/release/usr'; export HOME='/data/local/tmp'; export ROS_LOG_DIR='${LOG_DIR}'; export LD_LIBRARY_PATH=\${PREFIX}/lib:\${UNDERLAY_PREFIX}/lib:\${FASTDDS_PREFIX}/lib\${VENDOR_LIB_PATH:+:\${VENDOR_LIB_PATH}}\${UNDERLAY_VENDOR_LIB_PATH:+:\${UNDERLAY_VENDOR_LIB_PATH}}:/data/local/tmp:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64; export AMENT_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}; export CMAKE_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}:\${FASTDDS_PREFIX}; export COLCON_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}; export PYTHONPATH=\${PREFIX}/lib/python3.12/site-packages:\${UNDERLAY_PREFIX}/lib/python3.12/site-packages:\${UNDERLAY_PREFIX}/lib/python3.11/site-packages; export ROS_DOMAIN_ID='${DOMAIN_ID}'; export RMW_IMPLEMENTATION='rmw_mdds_cpp'; export RMW_MDDS_BROKER=1; export RMW_MDDS_BRIDGE_LIBRARY=\${BR}; export ROS_SECURITY_ENABLE=true; export ROS_SECURITY_STRATEGY=Enforce; export ROS_SECURITY_KEYSTORE='${REMOTE_KEYSTORE_ROOT}'; export ROS_SECURITY_ENCLAVE_OVERRIDE='/${ENCLAVE_NAME}';
EOF
}

kill_remote_pattern() {
  local device_id="$1"
  local pattern="$2"
  capture_hdc_shell "${device_id}" \
    "ps -ef | grep '${pattern}' | grep -v grep | while read -r user pid rest; do [ -z \"\${pid}\" ] || kill -9 \"\${pid}\" 2>/dev/null || true; done" >/dev/null || true
}

cleanup() {
  kill_remote_pattern "${SUB_DEVICE_ID}" "topic echo ${ALLOWED_TOPIC}"
  kill_remote_pattern "${PUB_DEVICE_ID}" "topic pub .*${ALLOWED_TOPIC}"
  kill_remote_pattern "${PUB_DEVICE_ID}" "topic pub .*${FORBIDDEN_TOPIC}"
}

run_authorized_cross_board() {
  local payload="board_sros2_allowed_${DOMAIN_ID}_$$"
  local echo_log="${LOG_DIR}/sros2_allowed_echo.log"
  local pub_log="${LOG_DIR}/sros2_allowed_pub.log"

  capture_hdc_shell "${SUB_DEVICE_ID}" \
    "mkdir -p '${LOG_DIR}'; rm -f '${echo_log}'; $(remote_env) nohup sh -c '${REMOTE_PREFIX}/bin/ros2 topic echo ${ALLOWED_TOPIC} std_msgs/msg/String --no-daemon > ${echo_log} 2>&1' >/dev/null 2>&1 & echo SROS2_ECHO_STARTED" >/dev/null
  sleep 15
  capture_hdc_shell "${PUB_DEVICE_ID}" \
    "mkdir -p '${LOG_DIR}'; rm -f '${pub_log}'; $(remote_env) nohup sh -c '${REMOTE_PREFIX}/bin/ros2 topic pub --times 20 -r 2 -w 0 ${ALLOWED_TOPIC} std_msgs/msg/String '\\''{data: ${payload}}'\\'' > ${pub_log} 2>&1' >/dev/null 2>&1 & echo SROS2_PUB_STARTED" >/dev/null
  sleep 22

  local rx
  rx="$(capture_hdc_shell "${SUB_DEVICE_ID}" "grep -c '${payload}' '${echo_log}' 2>/dev/null || true")"
  rx="${rx:-0}"
  [[ "${rx}" =~ ^[0-9]+$ ]] || rx=0
  if [[ "${rx}" -gt 0 ]]; then
    echo "RESULT|board_sros2_authorized_pubsub|PASS|topic=${ALLOWED_TOPIC}|received=${rx}"
    return 0
  fi
  echo "RESULT|board_sros2_authorized_pubsub|FAIL|topic=${ALLOWED_TOPIC}|received=${rx}" >&2
  echo "--- subscriber echo log ---" >&2
  capture_hdc_shell "${SUB_DEVICE_ID}" "cat '${echo_log}' 2>/dev/null | tail -40" >&2 || true
  echo "--- publisher log ---" >&2
  capture_hdc_shell "${PUB_DEVICE_ID}" "cat '${pub_log}' 2>/dev/null | tail -40" >&2 || true
  return 1
}

run_unauthorized_publish_denied() {
  local payload="board_sros2_forbidden_${DOMAIN_ID}_$$"
  local deny_log="${LOG_DIR}/sros2_forbidden_pub.log"
  local output
  output="$(
    capture_hdc_shell "${PUB_DEVICE_ID}" \
      "mkdir -p '${LOG_DIR}'; rm -f '${deny_log}'; $(remote_env) ${REMOTE_PREFIX}/bin/ros2 topic pub --times 1 -r 1 -w 0 ${FORBIDDEN_TOPIC} std_msgs/msg/String '{data: ${payload}}' > '${deny_log}' 2>&1; status=\$?; echo SROS2_DENY_STATUS=\${status}; exit 0"
  )"
  local deny_log_output
  deny_log_output="$(capture_hdc_shell "${PUB_DEVICE_ID}" "cat '${deny_log}' 2>/dev/null || true")"
  if grep -q 'SROS2_DENY_STATUS=0' <<<"${output}"; then
    echo "RESULT|board_sros2_unauthorized_publish|FAIL|topic=${FORBIDDEN_TOPIC}|publisher_succeeded" >&2
    echo "--- denied publish log ---" >&2
    printf '%s\n' "${deny_log_output}" >&2
    return 1
  fi
  if grep -q "ROS security policy denies publish access to topic ${FORBIDDEN_TOPIC}" <<<"${deny_log_output}"; then
    echo "RESULT|board_sros2_unauthorized_publish|PASS|topic=${FORBIDDEN_TOPIC}|denied"
    return 0
  fi
  echo "RESULT|board_sros2_unauthorized_publish|FAIL|topic=${FORBIDDEN_TOPIC}|missing_denial" >&2
  echo "--- status output ---" >&2
  printf '%s\n' "${output}" >&2
  echo "--- denied publish log ---" >&2
  printf '%s\n' "${deny_log_output}" >&2
  return 1
}

[[ -f "${HDC_SEND_VERIFY}" ]] || { echo "Missing ${HDC_SEND_VERIFY}" >&2; exit 1; }
require_remote_file "${SUB_DEVICE_ID}" "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${SUB_DEVICE_ID}" "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${SUB_DEVICE_ID}" "${BRIDGE_LIBRARY}"
require_remote_file "${PUB_DEVICE_ID}" "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${PUB_DEVICE_ID}" "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${PUB_DEVICE_ID}" "${BRIDGE_LIBRARY}"

WORK_DIR="$(mktemp -d /tmp/rmw_mdds_sros2_policy.XXXXXX)"
KEYSTORE_TARBALL="${WORK_DIR}/keystore.tgz"
trap 'rm -rf "${WORK_DIR}"; cleanup' EXIT
make_keystore_tarball "${KEYSTORE_TARBALL}" "${WORK_DIR}"
deploy_keystore "${SUB_DEVICE_ID}"
deploy_keystore "${PUB_DEVICE_ID}"

cleanup
capture_hdc_shell "${SUB_DEVICE_ID}" "mkdir -p '${LOG_DIR}'; rm -f '${LOG_DIR}'/sros2_*.log; true" >/dev/null
capture_hdc_shell "${PUB_DEVICE_ID}" "mkdir -p '${LOG_DIR}'; rm -f '${LOG_DIR}'/sros2_*.log; true" >/dev/null

run_authorized_cross_board
cleanup
run_unauthorized_publish_denied

echo "cross_board_rmw_mdds_sros2_policy_ok"
