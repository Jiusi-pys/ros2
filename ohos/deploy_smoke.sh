#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/deploy_smoke.sh"
# language: "shell"
# summary: "Shell script defining `run_hdc_capture`, and `wait_for_device_connected`."
# symbols: ["run_hdc_capture", "wait_for_device_connected"]
# generated_by: "codebase-frontmatter-summary"
# codex-file-meta: end

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_DIR="${ROS2_OHOS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-arm64}"
DEVICE_ID="${OHOS_DEVICE_ID:-${1:-}}"
REMOTE_DIR="${OHOS_REMOTE_DIR:-/data/local/tmp}"
DEFAULT_WRAPPER="/home/kaihong/.codex/skills/ohos-hdc/scripts/device-control.sh"
HDC_TIMEOUT_SEND="${OHOS_HDC_TIMEOUT_SEND:-45s}"
HDC_TIMEOUT_SHELL="${OHOS_HDC_TIMEOUT_SHELL:-60s}"
HDC_RETRIES="${OHOS_HDC_RETRIES:-3}"
HDC_READY_TIMEOUT_SECONDS="${OHOS_HDC_READY_TIMEOUT_SECONDS:-30}"
SMOKE_BINARY="${ROS2_OHOS_SMOKE_BINARY:-ros2_ohos_smoke}"
if [[ -v ROS2_OHOS_SMOKE_ARGS ]]; then
  SMOKE_ARGS="${ROS2_OHOS_SMOKE_ARGS}"
else
  SMOKE_ARGS="${REMOTE_DIR}/libros2_ohos_dummy.so"
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

RUNTIME_LIBS=()
if [[ -n "${ROS2_OHOS_RUNTIME_LIBS:-}" ]]; then
  IFS=':' read -r -a RUNTIME_LIBS <<< "${ROS2_OHOS_RUNTIME_LIBS}"
else
  RUNTIME_LIBS=(
    "${INSTALL_DIR}/lib/libros2_ohos_dummy.so"
    "${INSTALL_DIR}/lib/libros2_rcpputils.so"
    "${INSTALL_DIR}/lib/libros2_rcutils.so"
  )
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

    if [[ "${output}" == *"smoke_ok"* ]]; then
      return 0
    fi

    if [[ "${output}" == *"pubsub_smoke_ok"* ]]; then
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
    local status=$?
    set -e
    local output
    output="$(cat "${output_file}")"
    rm -f "${output_file}"

    if [[ -n "${output}" ]]; then
      printf '%s\n' "${output}" >/dev/null
    fi

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

for artifact in "${INSTALL_DIR}/bin/${SMOKE_BINARY}" "${RUNTIME_LIBS[@]}"; do
  if [[ ! -f "${artifact}" ]]; then
    echo "Missing artifact: ${artifact}" >&2
    exit 1
  fi
done

run_hdc_capture "${HDC_TIMEOUT_SEND}" file send "${INSTALL_DIR}/bin/${SMOKE_BINARY}" "${REMOTE_DIR}/${SMOKE_BINARY}"
for artifact in "${RUNTIME_LIBS[@]}"; do
  remote_artifact="${REMOTE_DIR}/$(basename "${artifact}")"
  run_hdc_capture "${HDC_TIMEOUT_SEND}" file send "${artifact}" "${remote_artifact}"
done

wait_for_device_connected

run_hdc_capture "${HDC_TIMEOUT_SHELL}" shell \
  "chmod 755 ${REMOTE_DIR}/${SMOKE_BINARY} ${REMOTE_DIR}/*.so ${REMOTE_DIR}/*.so.* >/dev/null 2>&1"

wait_for_device_connected

run_hdc_capture "${HDC_TIMEOUT_SHELL}" shell \
  "LD_LIBRARY_PATH=${REMOTE_DIR} ${REMOTE_DIR}/${SMOKE_BINARY}${SMOKE_ARGS:+ ${SMOKE_ARGS}}"
