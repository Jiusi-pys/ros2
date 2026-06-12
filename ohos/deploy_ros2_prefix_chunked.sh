#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/deploy_ros2_prefix_chunked.sh"
# language: "shell"
# summary: "Shell script defining `run_hdc_capture`, `wait_for_device_connected`, and `wait_for_remote_file` to deploy the standalone ROS 2 prefix as a chunked gzip bundle over unstable HDC links."
# symbols: ["run_hdc_capture", "wait_for_device_connected", "wait_for_remote_file"]
# generated_by: "codex"
# codex-file-meta: end

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX_DIR="${ROS2_OHOS_PREFIX_DIR:-${ROOT_DIR}/install/ohos-ros2}"
DEVICE_ID="${OHOS_DEVICE_ID:-${1:-}}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-prefix}"
REMOTE_BASE="${OHOS_REMOTE_BASE:-/data/local/tmp/ohos-prefix-chunked}"
REMOTE_PARTS_DIR="${OHOS_REMOTE_PARTS_DIR:-${REMOTE_BASE}.parts}"
REMOTE_ARCHIVE_GZ="${OHOS_REMOTE_ARCHIVE_GZ:-${REMOTE_BASE}.tar.gz}"
REMOTE_REASSEMBLE_LOG="${OHOS_REMOTE_REASSEMBLE_LOG:-/data/local/tmp/ohos-prefix-reassemble.log}"
REMOTE_EXTRACT_LOG="${OHOS_REMOTE_EXTRACT_LOG:-/data/local/tmp/ohos-prefix-extract.log}"
REMOTE_MARKER="${OHOS_REMOTE_MARKER:-${REMOTE_BASE}.marker}"
CHUNK_SIZE="${OHOS_CHUNK_SIZE:-4m}"
CHUNK_SEND_DELAY_SECONDS="${OHOS_CHUNK_SEND_DELAY_SECONDS:-5}"
DEFAULT_WRAPPER="/home/kaihong/.codex/skills/ohos-hdc/scripts/device-control.sh"
HDC_SEND_VERIFY="${ROOT_DIR}/ohos/tools/hdc_send_verify.sh"
HDC_TIMEOUT_SEND="${OHOS_HDC_TIMEOUT_SEND:-45s}"
HDC_TIMEOUT_SHELL="${OHOS_HDC_TIMEOUT_SHELL:-60s}"
HDC_TIMEOUT_PROBE="${OHOS_HDC_TIMEOUT_PROBE:-10s}"
HDC_RETRIES="${OHOS_HDC_RETRIES:-3}"
HDC_READY_TIMEOUT_SECONDS="${OHOS_HDC_READY_TIMEOUT_SECONDS:-30}"

if [[ -z "${DEVICE_ID}" ]]; then
  echo "Set OHOS_DEVICE_ID or pass the device id as the first argument." >&2
  exit 1
fi

if [[ ! -d "${PREFIX_DIR}" ]]; then
  echo "Missing prefix directory: ${PREFIX_DIR}" >&2
  exit 1
fi

if [[ -n "${OHOS_HDC_WRAPPER:-}" ]]; then
  HDC_BASE=("${OHOS_HDC_WRAPPER}" -t "${DEVICE_ID}")
elif command -v hdc >/dev/null 2>&1; then
  HDC_BASE=(hdc -t "${DEVICE_ID}")
elif [[ -x "${DEFAULT_WRAPPER}" ]]; then
  HDC_BASE=("${DEFAULT_WRAPPER}" -t "${DEVICE_ID}")
else
  echo "No HDC wrapper or hdc binary found." >&2
  exit 1
fi

shell_quote() {
  local value="$1"
  printf "'%s'" "${value//\'/\'\"\'\"\'}"
}

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

    if [[ "${output}" == *"Connect server failed"* || "${output}" == *"Device not founded or connected"* ]]; then
      if [[ ${attempt} -lt ${HDC_RETRIES} ]]; then
        sleep 1
        continue
      fi
      return 1
    fi

    if [[ ${status} -eq 0 ]]; then
      return 0
    fi

    if [[ "${output}" == *"ready"* ]]; then
      return 0
    fi

    if [[ "${output}" == *"FileTransfer finish"* ]]; then
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

remote_marker_expect() {
  local expected="$1"
  local condition="$2"
  local marker_path="${3:-${REMOTE_MARKER}}"
  local quoted_marker
  quoted_marker="$(shell_quote "${marker_path}")"
  local quoted_expected
  quoted_expected="$(shell_quote "${expected}")"
  local quoted_missing
  quoted_missing="$(shell_quote "__REMOTE_MARKER_MISSING__")"
  local local_marker
  local_marker="$(mktemp /tmp/ohos-hdc-marker.XXXXXX)"

  set +e
  "${HDC_BASE[@]}" shell sh -c \
    "rm -f ${quoted_marker}; if ${condition}; then printf '%s\n' ${quoted_expected} > ${quoted_marker}; else printf '%s\n' ${quoted_missing} > ${quoted_marker}; fi"
  "${HDC_BASE[@]}" file recv "${marker_path}" "${local_marker}"
  set -e

  local output
  if [[ -f "${local_marker}" ]]; then
    output="$(cat "${local_marker}")"
  else
    output=""
  fi
  rm -f "${local_marker}"

  [[ "${output}" == *"${expected}"* ]]
}

remote_marker_run_expect() {
  local expected="$1"
  local command="$2"
  remote_marker_expect "${expected}" "${command}"
}

wait_for_device_connected() {
  local start_ts
  start_ts="$(date +%s)"
  while true; do
    if remote_marker_expect "__DATA_LOCAL_TMP_OK__" "[ -d /data/local/tmp ]"; then
      return 0
    fi

    if (( $(date +%s) - start_ts >= HDC_READY_TIMEOUT_SECONDS )); then
      echo "Timed out waiting for device ${DEVICE_ID} to report Connected." >&2
      return 1
    fi

    sleep 1
  done
}

wait_for_remote_file() {
  local remote_path="$1"
  local timeout_seconds="${2:-60}"
  local quoted_path
  quoted_path="$(shell_quote "${remote_path}")"
  local start_ts
  start_ts="$(date +%s)"
  while true; do
    if remote_marker_expect "__REMOTE_FILE_OK__" "[ -f ${quoted_path} ]"; then
      return 0
    fi

    if (( $(date +%s) - start_ts >= timeout_seconds )); then
      echo "Timed out waiting for remote file ${remote_path}" >&2
      return 1
    fi

    sleep 1
  done
}

ensure_remote_chunk() {
  local local_part="$1"
  local remote_part="$2"
  local attempts="${3:-6}"
  local helper_env=(
    "OHOS_DEVICE_ID=${DEVICE_ID}"
    "OHOS_HDC_TIMEOUT_SEND=${HDC_TIMEOUT_SEND}"
    "OHOS_HDC_TIMEOUT_SHELL=${HDC_TIMEOUT_SHELL}"
    "OHOS_HDC_RETRIES=${attempts}"
    "OHOS_HDC_VERIFY_TIMEOUT_SECONDS=${HDC_READY_TIMEOUT_SECONDS}"
  )
  if [[ -n "${OHOS_HDC_WRAPPER:-}" ]]; then
    helper_env+=("OHOS_HDC_WRAPPER=${OHOS_HDC_WRAPPER}")
  else
    helper_env+=("OHOS_HDC_BIN=${HDC_BASE[0]}")
  fi
  env "${helper_env[@]}" "${HDC_SEND_VERIFY}" "${local_part}" "${remote_part}"
}

wait_for_remote_dir() {
  local remote_path="$1"
  local timeout_seconds="${2:-60}"
  local quoted_path
  quoted_path="$(shell_quote "${remote_path}")"
  local start_ts
  start_ts="$(date +%s)"
  while true; do
    if remote_marker_expect "__REMOTE_DIR_OK__" "[ -d ${quoted_path} ]"; then
      return 0
    fi

    if (( $(date +%s) - start_ts >= timeout_seconds )); then
      echo "Timed out waiting for remote directory ${remote_path}" >&2
      return 1
    fi

    sleep 1
  done
}

ensure_remote_setup() {
  local remote_path="$1"
  local timeout_seconds="${2:-60}"
  local start_ts
  start_ts="$(date +%s)"
  while true; do
    set +e
    "${HDC_BASE[@]}" shell rm -rf "${remote_path}"
    "${HDC_BASE[@]}" shell mkdir -p "${remote_path}"
    "${HDC_BASE[@]}" shell rm -f "${REMOTE_ARCHIVE_GZ}" "${REMOTE_REASSEMBLE_LOG}" "${REMOTE_EXTRACT_LOG}" "${REMOTE_MARKER}"
    set -e

    if wait_for_remote_dir "${remote_path}" 5; then
      return 0
    fi

    if (( $(date +%s) - start_ts >= timeout_seconds )); then
      echo "Timed out ensuring remote setup under ${remote_path}" >&2
      return 1
    fi

    sleep "${CHUNK_SEND_DELAY_SECONDS}"
  done
}

bundle_gz="$(mktemp /tmp/ohos-prefix.XXXXXX.tar.gz)"
parts_dir="$(mktemp -d /tmp/ohos-prefix-parts.XXXXXX)"
cleanup() {
  rm -f "${bundle_gz}"
  rm -rf "${parts_dir}"
}
trap cleanup EXIT

tar_args=()
for subdir in bin lib share opt; do
  if [[ -e "${PREFIX_DIR}/${subdir}" ]]; then
    tar_args+=("${subdir}")
  fi
done

if [[ ${#tar_args[@]} -eq 0 ]]; then
  echo "Nothing to deploy from ${PREFIX_DIR}; expected at least one of bin/, lib/, share/, or opt/." >&2
  exit 1
fi

tar -C "${PREFIX_DIR}" -czf "${bundle_gz}" "${tar_args[@]}"
split -b "${CHUNK_SIZE}" -d -a 3 "${bundle_gz}" "${parts_dir}/part_"
mapfile -t part_paths < <(find "${parts_dir}" -maxdepth 1 -type f | sort)

echo "chunked_prefix_prepare_ok"
echo "device_id=${DEVICE_ID}"
echo "remote_prefix=${REMOTE_PREFIX}"
echo "parts_dir=${parts_dir}"
echo "part_count=${#part_paths[@]}"

wait_for_device_connected
ensure_remote_setup "${REMOTE_PARTS_DIR}" "${HDC_READY_TIMEOUT_SECONDS}"
sleep "${CHUNK_SEND_DELAY_SECONDS}"

for part_path in "${part_paths[@]}"; do
  remote_part="${REMOTE_PARTS_DIR}/$(basename "${part_path}")"
  ensure_remote_chunk "${part_path}" "${remote_part}"
done

echo "reassembling_archive=${REMOTE_ARCHIVE_GZ}"
if ! remote_marker_run_expect "__REASSEMBLE_OK__" \
  "cat $(shell_quote "${REMOTE_PARTS_DIR}")/part_* > $(shell_quote "${REMOTE_ARCHIVE_GZ}")"; then
  echo "Failed to reassemble archive on device ${DEVICE_ID}" >&2
  exit 1
fi
wait_for_remote_file "${REMOTE_ARCHIVE_GZ}" 120

echo "extracting_prefix=${REMOTE_PREFIX}"
if ! remote_marker_run_expect "__EXTRACT_OK__" \
  "mkdir -p $(shell_quote "${REMOTE_PREFIX}") && cd $(shell_quote "${REMOTE_PREFIX}") && tar -xzf $(shell_quote "${REMOTE_ARCHIVE_GZ}")"; then
  echo "Failed to extract prefix on device ${DEVICE_ID}" >&2
  exit 1
fi
wait_for_remote_file "${REMOTE_PREFIX}/bin/ros2" 180

echo "ros2_prefix_chunked_deploy_ok"
echo "device_id=${DEVICE_ID}"
echo "remote_prefix=${REMOTE_PREFIX}"
echo "bundle_gz_size=$(stat -c %s "${bundle_gz}")"
