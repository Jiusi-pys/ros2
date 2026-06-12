#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/test_cross_board_pubsub.sh"
# language: "shell"
# summary: "Shell script defining `run_hdc_capture`, `build_runtime_env`, `wait_for_device_connected`, and `configure_ip`."
# symbols: ["run_hdc_capture", "build_runtime_env", "wait_for_device_connected", "configure_ip", "ping_peer", "push_bundle"]
# generated_by: "codebase-frontmatter-summary"
# codex-file-meta: end

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUB_DEVICE_ID="${OHOS_SUB_DEVICE_ID:-${1:-}}"
PUB_DEVICE_ID="${OHOS_PUB_DEVICE_ID:-${2:-}}"
REMOTE_DIR="${OHOS_REMOTE_DIR:-/data/local/tmp}"
DEFAULT_WRAPPER="/home/kaihong/.codex/skills/ohos-hdc/scripts/device-control.sh"
HDC_TIMEOUT_SEND="${OHOS_HDC_TIMEOUT_SEND:-45s}"
HDC_TIMEOUT_SHELL="${OHOS_HDC_TIMEOUT_SHELL:-60s}"
HDC_RETRIES="${OHOS_HDC_RETRIES:-3}"
HDC_READY_TIMEOUT_SECONDS="${OHOS_HDC_READY_TIMEOUT_SECONDS:-30}"

TEST_IFACE="${OHOS_TEST_IFACE:-eth0}"
SUB_IP="${OHOS_SUB_IP:-192.168.88.1}"
PUB_IP="${OHOS_PUB_IP:-192.168.88.2}"
if [[ -v OHOS_STATIC_PEERS ]]; then
  STATIC_PEERS="${OHOS_STATIC_PEERS}"
else
  STATIC_PEERS=""
fi
ROS_DOMAIN_ID_VALUE="${ROS2_OHOS_DOMAIN_ID:-88}"
DISCOVERY_RANGE="${ROS2_OHOS_DISCOVERY_RANGE:-SYSTEM_DEFAULT}"
TOPIC_NAME="${OHOS_CROSS_TOPIC:-/ros2_cross_board_pubsub_$(date +%s)}"
PAYLOAD="${OHOS_CROSS_PAYLOAD:-cross-board-kaihongos-pubsub}"
EXTRA_ENV="${ROS2_OHOS_EXTRA_ENV:-}"

INSTALL_DIR="${ROS2_OHOS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-arm64}"
SMOKE_BINARY="${INSTALL_DIR}/bin/ros2_ohos_pubsub_smoke"
RUNTIME_LIBS_RAW="${ROS2_OHOS_RUNTIME_LIBS:-$(ROS2_OHOS_INSTALL_DIR="${INSTALL_DIR}" "${ROOT_DIR}/ohos/print_pubsub_runtime_libs.sh")}"

if [[ -z "${SUB_DEVICE_ID}" || -z "${PUB_DEVICE_ID}" ]]; then
  echo "Usage: $0 <subscriber-device-id> <publisher-device-id>" >&2
  exit 1
fi

if [[ -n "${OHOS_HDC_WRAPPER:-}" ]]; then
  HDC_BIN="${OHOS_HDC_WRAPPER}"
elif [[ -x "${DEFAULT_WRAPPER}" ]]; then
  HDC_BIN="${DEFAULT_WRAPPER}"
elif command -v hdc >/dev/null 2>&1; then
  HDC_BIN="hdc"
else
  echo "No HDC wrapper or hdc binary found." >&2
  exit 1
fi

IFS=':' read -r -a RUNTIME_LIBS <<< "${RUNTIME_LIBS_RAW}"

for artifact in "${SMOKE_BINARY}" "${RUNTIME_LIBS[@]}"; do
  if [[ ! -f "${artifact}" ]]; then
    echo "Missing artifact: ${artifact}" >&2
    exit 1
  fi
done

run_hdc_capture() {
  local device_id="$1"
  local timeout_value="$2"
  shift 2

  local attempt
  for ((attempt = 1; attempt <= HDC_RETRIES; ++attempt)); do
    local output_file
    output_file="$(mktemp)"

    set +e
    timeout "${timeout_value}" "${HDC_BIN}" -t "${device_id}" "$@" >"${output_file}" 2>&1
    local status=$?
    set -e

    local output
    output="$(cat "${output_file}")"
    rm -f "${output_file}"

    if [[ -n "${output}" ]]; then
      printf '%s\n' "${output}"
    fi

    if [[ ${status} -eq 0 ]]; then
      return 0
    fi

    if [[ "${output}" == *"FileTransfer finish"* || "${output}" == *"subscriber_ready"* || "${output}" == *"publisher_sent"* || "${output}" == *"subscriber_received"* ]]; then
      return 0
    fi

    if [[ ${attempt} -lt ${HDC_RETRIES} ]] && \
       [[ "${output}" == *"Device not founded or connected"* || "${output}" == *"Connect server failed"* || "${status}" -eq 139 ]]; then
      sleep 1
      continue
    fi

    return "${status}"
  done
}

shell_quote() {
  local value="$1"
  value=${value//\'/\'\"\'\"\'}
  printf "'%s'" "${value}"
}

build_runtime_env() {
  local peer_env=""
  if [[ -n "${STATIC_PEERS}" ]]; then
    peer_env="ROS_STATIC_PEERS=\"${STATIC_PEERS}\" "
  fi
  printf '%s' \
    "env RMW_IMPLEMENTATION=rmw_fastrtps_cpp ROS_DOMAIN_ID=${ROS_DOMAIN_ID_VALUE} ROS_AUTOMATIC_DISCOVERY_RANGE=${DISCOVERY_RANGE} ${peer_env}${EXTRA_ENV:+${EXTRA_ENV} }LD_LIBRARY_PATH=${REMOTE_DIR}"
}

wait_for_device_connected() {
  local device_id="$1"
  local start_ts
  start_ts="$(date +%s)"
  while true; do
    local output_file
    output_file="$(mktemp)"
    set +e
    timeout 10s "${HDC_BIN}" list targets -v >"${output_file}" 2>&1
    set -e
    local output
    output="$(cat "${output_file}")"
    rm -f "${output_file}"

    if grep -Eq "^${device_id}[[:space:]]+USB[[:space:]]+Connected" <<< "${output}"; then
      return 0
    fi

    if (( $(date +%s) - start_ts >= HDC_READY_TIMEOUT_SECONDS )); then
      echo "Timed out waiting for device ${device_id} to report Connected." >&2
      return 1
    fi

    sleep 1
  done
}

configure_ip() {
  local device_id="$1"
  local ip_addr="$2"
  run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SHELL}" shell \
    "ip link set ${TEST_IFACE} up && ip addr show dev ${TEST_IFACE} | grep -q '${ip_addr}/24' || ip addr add ${ip_addr}/24 dev ${TEST_IFACE}"
}

ping_peer() {
  local device_id="$1"
  local peer_ip="$2"
  run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SHELL}" shell \
    "ping -c 1 -W 2 ${peer_ip}"
}

push_bundle() {
  local device_id="$1"
  run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SHELL}" shell \
    "pkill -f ros2_ohos_pubsub_smoke >/dev/null 2>&1 || true"
  run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SEND}" file send "${SMOKE_BINARY}" "${REMOTE_DIR}/ros2_ohos_pubsub_smoke"
  for artifact in "${RUNTIME_LIBS[@]}"; do
    run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SEND}" file send "${artifact}" "${REMOTE_DIR}/$(basename "${artifact}")"
  done
  wait_for_device_connected "${device_id}"
  run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SHELL}" shell \
    "chmod 755 ${REMOTE_DIR}/ros2_ohos_pubsub_smoke ${REMOTE_DIR}/*.so ${REMOTE_DIR}/*.so.* >/dev/null 2>&1"
}

run_hdc_capture "${SUB_DEVICE_ID}" "${HDC_TIMEOUT_SHELL}" shell "mkdir -p ${REMOTE_DIR} && rm -f ${REMOTE_DIR}/cross_pubsub_sub.log ${REMOTE_DIR}/cross_pubsub_pub.log"
run_hdc_capture "${PUB_DEVICE_ID}" "${HDC_TIMEOUT_SHELL}" shell "mkdir -p ${REMOTE_DIR} && rm -f ${REMOTE_DIR}/cross_pubsub_sub.log ${REMOTE_DIR}/cross_pubsub_pub.log"

configure_ip "${SUB_DEVICE_ID}" "${SUB_IP}"
configure_ip "${PUB_DEVICE_ID}" "${PUB_IP}"

ping_peer "${SUB_DEVICE_ID}" "${PUB_IP}"
ping_peer "${PUB_DEVICE_ID}" "${SUB_IP}"

push_bundle "${SUB_DEVICE_ID}"
push_bundle "${PUB_DEVICE_ID}"

RUNTIME_ENV="$(build_runtime_env)"
SUBSCRIBER_CMD="${RUNTIME_ENV} ./ros2_ohos_pubsub_smoke subscriber $(shell_quote "${TOPIC_NAME}") $(shell_quote "${PAYLOAD}") > cross_pubsub_sub.log 2>&1"
PUBLISHER_CMD="${RUNTIME_ENV} ./ros2_ohos_pubsub_smoke publisher $(shell_quote "${TOPIC_NAME}") $(shell_quote "${PAYLOAD}")"

run_hdc_capture "${SUB_DEVICE_ID}" "${HDC_TIMEOUT_SHELL}" shell \
  "cd ${REMOTE_DIR} && nohup sh -c $(shell_quote "${SUBSCRIBER_CMD}") >/dev/null 2>&1 &"

subscriber_ready="$(
  run_hdc_capture "${SUB_DEVICE_ID}" "${HDC_TIMEOUT_SHELL}" shell \
    "cd ${REMOTE_DIR} && i=0; while [ \$i -lt 30 ]; do grep subscriber_ready cross_pubsub_sub.log >/dev/null 2>&1 && break; sleep 1; i=\$((i+1)); done; cat cross_pubsub_sub.log"
)"
printf '%s\n' "${subscriber_ready}"

publisher_output="$(
  run_hdc_capture "${PUB_DEVICE_ID}" "${HDC_TIMEOUT_SHELL}" shell \
    "cd ${REMOTE_DIR} && sh -c $(shell_quote "${PUBLISHER_CMD}")"
)"
printf '%s\n' "${publisher_output}"

subscriber_output="$(
  run_hdc_capture "${SUB_DEVICE_ID}" "${HDC_TIMEOUT_SHELL}" shell \
    "cd ${REMOTE_DIR} && i=0; while [ \$i -lt 30 ]; do grep subscriber_received cross_pubsub_sub.log >/dev/null 2>&1 && break; sleep 1; i=\$((i+1)); done; cat cross_pubsub_sub.log"
)"
printf '%s\n' "${subscriber_output}"

if [[ "${publisher_output}" != *"publisher_sent"* ]]; then
  echo "Publisher output missing publisher_sent marker." >&2
  exit 1
fi

if [[ "${subscriber_output}" != *"subscriber_received"* ]]; then
  echo "Subscriber output missing subscriber_received marker." >&2
  exit 1
fi

if [[ "${subscriber_output}" != *"payload=${PAYLOAD}"* ]]; then
  echo "Subscriber output missing expected payload." >&2
  exit 1
fi

echo "cross_board_pubsub_ok"
