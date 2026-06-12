#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/deploy_fastdds_smoke.sh"
# language: "shell"
# summary: "Shell script defining `run_hdc_capture`, and `wait_for_device_connected`."
# symbols: ["run_hdc_capture", "wait_for_device_connected"]
# generated_by: "codebase-frontmatter-summary"
# codex-file-meta: end

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEVICE_ID="${OHOS_DEVICE_ID:-${1:-}}"
REMOTE_DIR="${OHOS_REMOTE_DIR:-/data/local/tmp/fastdds_smoke}"
DEFAULT_WRAPPER="/home/kaihong/.codex/skills/ohos-hdc/scripts/device-control.sh"
HDC_TIMEOUT_SEND="${OHOS_HDC_TIMEOUT_SEND:-45s}"
HDC_TIMEOUT_SHELL="${OHOS_HDC_TIMEOUT_SHELL:-60s}"
HDC_RETRIES="${OHOS_HDC_RETRIES:-3}"
HDC_READY_TIMEOUT_SECONDS="${OHOS_HDC_READY_TIMEOUT_SECONDS:-30}"

STACK_INSTALL_DIR="${ROS2_OHOS_FASTDDS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-fastdds}"
SMOKE_INSTALL_DIR="${ROS2_OHOS_FASTDDS_SMOKE_INSTALL_DIR:-${ROOT_DIR}/install/ohos-fastdds-smoke}"

SMOKE_BINARY="${SMOKE_INSTALL_DIR}/examples/cpp/dds/BasicConfigurationExample/BasicConfigurationExample"
LIB_FASTRTPS="${STACK_INSTALL_DIR}/lib/libfastrtps.so.2.14.6"
LIB_FASTCDR="${STACK_INSTALL_DIR}/lib/libfastcdr.so.2.2.7"
TOPIC_NAME="${ROS2_OHOS_FASTDDS_TOPIC:-KaihongFastDDS}"
DOMAIN_ID="${ROS2_OHOS_FASTDDS_DOMAIN:-42}"
TRANSPORT="${ROS2_OHOS_FASTDDS_TRANSPORT:-udp}"
RUN_SUFFIX="$(date +%s)"

if [[ -v ROS2_OHOS_FASTDDS_TOPIC ]]; then
  TOPIC_NAME="${ROS2_OHOS_FASTDDS_TOPIC}"
else
  TOPIC_NAME="KaihongFastDDS_${RUN_SUFFIX}"
fi

if [[ -z "${DEVICE_ID}" ]]; then
  echo "Set OHOS_DEVICE_ID or pass the device id as the first argument." >&2
  exit 1
fi

if [[ -n "${OHOS_HDC_WRAPPER:-}" ]]; then
  HDC_BASE=("${OHOS_HDC_WRAPPER}" -t "${DEVICE_ID}")
elif [[ -x "${DEFAULT_WRAPPER}" ]]; then
  HDC_BASE=("${DEFAULT_WRAPPER}" -t "${DEVICE_ID}")
elif command -v hdc >/dev/null 2>&1; then
  HDC_BASE=(hdc -t "${DEVICE_ID}")
else
  echo "No HDC wrapper or hdc binary found." >&2
  exit 1
fi

run_hdc_capture() {
  local timeout_value="$1"
  shift

  local attempt
  for ((attempt = 1; attempt <= HDC_RETRIES; ++attempt)); do
    local output_file
    output_file="$(mktemp)"

    set +e
    timeout "${timeout_value}" "${HDC_BASE[@]}" "$@" >"${output_file}" 2>&1
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

    if [[ "${output}" == *"FileTransfer finish"* ]]; then
      return 0
    fi

    if [[ "${output}" == *"subscriber_started"* ]]; then
      return 0
    fi

    if [[ "${output}" == *"SENT"* || "${output}" == *"RECEIVED"* ]]; then
      return 0
    fi

    if [[ ${attempt} -lt ${HDC_RETRIES} ]] && \
       [[ "${output}" == *"Device not founded or connected"* || "${output}" == *"Connect server failed"* ]]; then
      sleep 1
      continue
    fi

    return "${status}"
  done
}

wait_for_device_connected() {
  local start_ts
  start_ts="$(date +%s)"
  while true; do
    local output_file
    output_file="$(mktemp)"
    set +e
    timeout 10s "${HDC_BASE[0]}" list targets -v >"${output_file}" 2>&1
    set -e
    local output
    output="$(cat "${output_file}")"
    rm -f "${output_file}"

    if grep -Eq "^${DEVICE_ID}[[:space:]]+USB[[:space:]]+Connected" <<< "${output}"; then
      return 0
    fi

    if (( $(date +%s) - start_ts >= HDC_READY_TIMEOUT_SECONDS )); then
      echo "Timed out waiting for device ${DEVICE_ID} to report Connected." >&2
      return 1
    fi

    sleep 1
  done
}

for artifact in "${SMOKE_BINARY}" "${LIB_FASTRTPS}" "${LIB_FASTCDR}"; do
  if [[ ! -f "${artifact}" ]]; then
    echo "Missing artifact: ${artifact}" >&2
    exit 1
  fi
done

run_hdc_capture "${HDC_TIMEOUT_SHELL}" shell \
  "rm -rf ${REMOTE_DIR} && mkdir -p ${REMOTE_DIR}"

run_hdc_capture "${HDC_TIMEOUT_SEND}" file send "${SMOKE_BINARY}" "${REMOTE_DIR}/BasicConfigurationExample"
run_hdc_capture "${HDC_TIMEOUT_SEND}" file send "${LIB_FASTRTPS}" "${REMOTE_DIR}/libfastrtps.so.2.14.6"
run_hdc_capture "${HDC_TIMEOUT_SEND}" file send "${LIB_FASTCDR}" "${REMOTE_DIR}/libfastcdr.so.2.2.7"

wait_for_device_connected

run_hdc_capture "${HDC_TIMEOUT_SHELL}" shell \
  "cd ${REMOTE_DIR} && chmod 755 BasicConfigurationExample libfastrtps.so.2.14.6 libfastcdr.so.2.2.7 && ln -sf libfastrtps.so.2.14.6 libfastrtps.so.2.14 && ln -sf libfastrtps.so.2.14 libfastrtps.so && ln -sf libfastcdr.so.2.2.7 libfastcdr.so.2 && ln -sf libfastcdr.so.2 libfastcdr.so && rm -f subscriber.log publisher.log subscriber.exit"

wait_for_device_connected

run_hdc_capture "${HDC_TIMEOUT_SHELL}" shell \
  "cd ${REMOTE_DIR} && nohup sh -c 'LD_LIBRARY_PATH=${REMOTE_DIR} ./BasicConfigurationExample subscriber --samples=1 --domain=${DOMAIN_ID} --topic=${TOPIC_NAME} --transport=${TRANSPORT} > subscriber.log 2>&1' >/dev/null 2>&1 & echo subscriber_started"

publisher_output="$(
  run_hdc_capture "${HDC_TIMEOUT_SHELL}" shell \
    "cd ${REMOTE_DIR} && sleep 1 && LD_LIBRARY_PATH=${REMOTE_DIR} ./BasicConfigurationExample publisher --samples=1 --wait=1 --domain=${DOMAIN_ID} --topic=${TOPIC_NAME} --transport=${TRANSPORT}"
)"

subscriber_output="$(
  run_hdc_capture "${HDC_TIMEOUT_SHELL}" shell \
    "cd ${REMOTE_DIR} && i=0; while [ \$i -lt 20 ]; do grep RECEIVED subscriber.log >/dev/null 2>&1 && break; sleep 1; i=\$((i+1)); done; cat subscriber.log"
)"

printf '%s\n' "${publisher_output}"
printf '%s\n' "${subscriber_output}"

if [[ "${publisher_output}" != *"SENT"* ]]; then
  echo "Publisher log did not contain SENT marker." >&2
  exit 1
fi

if [[ "${subscriber_output}" != *"RECEIVED"* ]]; then
  echo "Subscriber log did not contain RECEIVED marker." >&2
  exit 1
fi

echo "fastdds_e2e_ok"
