#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/test_cross_board_fastdds.sh"
# language: "shell"
# summary: "Shell script defining `run_hdc_capture`, `wait_for_device_connected`, `configure_ip`, and `ping_peer`."
# symbols: ["run_hdc_capture", "wait_for_device_connected", "configure_ip", "ping_peer", "push_bundle"]
# generated_by: "codebase-frontmatter-summary"
# codex-file-meta: end

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUB_DEVICE_ID="${OHOS_SUB_DEVICE_ID:-${1:-}}"
PUB_DEVICE_ID="${OHOS_PUB_DEVICE_ID:-${2:-}}"
REMOTE_DIR="${OHOS_REMOTE_DIR:-/data/local/tmp/fastdds_cross}"
DEFAULT_WRAPPER="/home/kaihong/.codex/skills/ohos-hdc/scripts/device-control.sh"
HDC_TIMEOUT_SEND="${OHOS_HDC_TIMEOUT_SEND:-45s}"
HDC_TIMEOUT_SHELL="${OHOS_HDC_TIMEOUT_SHELL:-60s}"
HDC_READY_TIMEOUT_SECONDS="${OHOS_HDC_READY_TIMEOUT_SECONDS:-30}"

TEST_IFACE="${OHOS_TEST_IFACE:-eth0}"
SUB_IP="${OHOS_SUB_IP:-192.168.88.1}"
PUB_IP="${OHOS_PUB_IP:-192.168.88.2}"

STACK_INSTALL_DIR="${ROS2_OHOS_FASTDDS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-fastdds}"
SMOKE_INSTALL_DIR="${ROS2_OHOS_FASTDDS_SMOKE_INSTALL_DIR:-${ROOT_DIR}/install/ohos-fastdds-smoke}"
SMOKE_BINARY="${SMOKE_INSTALL_DIR}/examples/cpp/dds/BasicConfigurationExample/BasicConfigurationExample"
LIB_FASTRTPS="${STACK_INSTALL_DIR}/lib/libfastrtps.so.2.14.6"
LIB_FASTCDR="${STACK_INSTALL_DIR}/lib/libfastcdr.so.2.2.7"
TOPIC_NAME="${ROS2_OHOS_FASTDDS_TOPIC:-KaihongCrossFastDDS_$(date +%s)}"
DOMAIN_ID="${ROS2_OHOS_FASTDDS_DOMAIN:-52}"
TRANSPORT="${ROS2_OHOS_FASTDDS_TRANSPORT:-udp}"

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

for artifact in "${SMOKE_BINARY}" "${LIB_FASTRTPS}" "${LIB_FASTCDR}"; do
  if [[ ! -f "${artifact}" ]]; then
    echo "Missing artifact: ${artifact}" >&2
    exit 1
  fi
done

run_hdc_capture() {
  local device_id="$1"
  local timeout_value="$2"
  shift 2

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

  if [[ "${output}" == *"FileTransfer finish"* || "${output}" == *"subscriber_started"* || "${output}" == *"SENT"* || "${output}" == *"RECEIVED"* ]]; then
    return 0
  fi

  return "${status}"
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
  run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SHELL}" shell "rm -rf ${REMOTE_DIR} && mkdir -p ${REMOTE_DIR}"
  run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SEND}" file send "${SMOKE_BINARY}" "${REMOTE_DIR}/BasicConfigurationExample"
  run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SEND}" file send "${LIB_FASTRTPS}" "${REMOTE_DIR}/libfastrtps.so.2.14.6"
  run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SEND}" file send "${LIB_FASTCDR}" "${REMOTE_DIR}/libfastcdr.so.2.2.7"
  wait_for_device_connected "${device_id}"
  run_hdc_capture "${device_id}" "${HDC_TIMEOUT_SHELL}" shell \
    "cd ${REMOTE_DIR} && chmod 755 BasicConfigurationExample libfastrtps.so.2.14.6 libfastcdr.so.2.2.7 && ln -sf libfastrtps.so.2.14.6 libfastrtps.so.2.14 && ln -sf libfastrtps.so.2.14 libfastrtps.so && ln -sf libfastcdr.so.2.2.7 libfastcdr.so.2 && ln -sf libfastcdr.so.2 libfastcdr.so && rm -f subscriber.log publisher.log"
}

configure_ip "${SUB_DEVICE_ID}" "${SUB_IP}"
configure_ip "${PUB_DEVICE_ID}" "${PUB_IP}"

ping_peer "${SUB_DEVICE_ID}" "${PUB_IP}"
ping_peer "${PUB_DEVICE_ID}" "${SUB_IP}"

push_bundle "${SUB_DEVICE_ID}"
push_bundle "${PUB_DEVICE_ID}"

run_hdc_capture "${SUB_DEVICE_ID}" "${HDC_TIMEOUT_SHELL}" shell \
  "cd ${REMOTE_DIR} && nohup sh -c 'LD_LIBRARY_PATH=${REMOTE_DIR} ./BasicConfigurationExample subscriber --samples=1 --domain=${DOMAIN_ID} --topic=${TOPIC_NAME} --transport=${TRANSPORT} > subscriber.log 2>&1' >/dev/null 2>&1 & echo subscriber_started"

publisher_output="$(
  run_hdc_capture "${PUB_DEVICE_ID}" "${HDC_TIMEOUT_SHELL}" shell \
    "cd ${REMOTE_DIR} && sleep 2 && LD_LIBRARY_PATH=${REMOTE_DIR} ./BasicConfigurationExample publisher --samples=3 --wait=1 --domain=${DOMAIN_ID} --topic=${TOPIC_NAME} --transport=${TRANSPORT}"
)"
printf '%s\n' "${publisher_output}"

subscriber_output="$(
  run_hdc_capture "${SUB_DEVICE_ID}" "${HDC_TIMEOUT_SHELL}" shell \
    "cd ${REMOTE_DIR} && i=0; while [ \$i -lt 30 ]; do grep RECEIVED subscriber.log >/dev/null 2>&1 && break; sleep 1; i=\$((i+1)); done; cat subscriber.log"
)"
printf '%s\n' "${subscriber_output}"

if [[ "${publisher_output}" != *"SENT"* ]]; then
  echo "Publisher log missing SENT marker." >&2
  exit 1
fi

if [[ "${subscriber_output}" != *"RECEIVED"* ]]; then
  echo "Subscriber log missing RECEIVED marker." >&2
  exit 1
fi

echo "cross_board_fastdds_ok"
