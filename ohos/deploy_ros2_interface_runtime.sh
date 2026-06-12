#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/deploy_ros2_interface_runtime.sh"
# language: "shell"
# summary: "Shell script defining HDC retry helpers to sync selected interface packages, share metadata, Python bindings, and matching rosidl native libraries into the board runtime prefix."
# symbols: ["wait_for_device_connected", "run_hdc_direct", "wait_for_remote_path"]
# generated_by: "codex"
# codex-file-meta: end

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX_DIR="${ROS2_OHOS_PREFIX_DIR:-${ROOT_DIR}/install/ohos-ros2}"
DEVICE_ID="${OHOS_DEVICE_ID:-${1:-}}"
shift || true
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-prefix}"
REMOTE_BUNDLE="${OHOS_REMOTE_BUNDLE:-/data/local/tmp/ohos-interface-runtime.tar}"
DEFAULT_WRAPPER="/home/kaihong/.codex/skills/ohos-hdc/scripts/device-control.sh"
HDC_RETRIES="${OHOS_HDC_RETRIES:-3}"
HDC_READY_TIMEOUT_SECONDS="${OHOS_HDC_READY_TIMEOUT_SECONDS:-30}"
TARGET_PYTHON_VERSION="${ROS2_OHOS_TARGET_PYTHON_VERSION:-3.12}"

if [[ -z "${DEVICE_ID}" || $# -eq 0 ]]; then
  echo "Usage: $0 <device-id> <interface-package> [interface-package...]" >&2
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

wait_for_device_connected() {
  local start_ts
  start_ts="$(date +%s)"
  while true; do
    set +e
    "${HDC_BASE[@]}" shell test -d /data/local/tmp >/dev/null 2>&1
    local status=$?
    set -e

    if [[ ${status} -eq 0 ]]; then
      return 0
    fi

    if (( $(date +%s) - start_ts >= HDC_READY_TIMEOUT_SECONDS )); then
      echo "Timed out waiting for device ${DEVICE_ID} to report Connected." >&2
      return 1
    fi

    sleep 1
  done
}

run_hdc_direct() {
  local attempt
  for ((attempt = 1; attempt <= HDC_RETRIES; ++attempt)); do
    set +e
    "${HDC_BASE[@]}" "$@"
    local status=$?
    set -e

    if [[ ${status} -eq 0 ]]; then
      return 0
    fi

    if [[ ${attempt} -lt ${HDC_RETRIES} ]]; then
      sleep 1
      continue
    fi

    return "${status}"
  done
}

wait_for_remote_path() {
  local remote_path="$1"
  local start_ts
  start_ts="$(date +%s)"

  while true; do
    set +e
    "${HDC_BASE[@]}" shell test -e "${remote_path}" >/dev/null 2>&1
    local status=$?
    set -e

    if [[ ${status} -eq 0 ]]; then
      return 0
    fi

    if (( $(date +%s) - start_ts >= HDC_READY_TIMEOUT_SECONDS )); then
      echo "Timed out waiting for remote path ${remote_path}" >&2
      return 1
    fi

    sleep 1
  done
}

ensure_remote_file_uploaded() {
  local local_path="$1"
  local remote_path="$2"

  run_hdc_direct file send "${local_path}" "${remote_path}"
  wait_for_device_connected || true
  wait_for_remote_path "${remote_path}"
}

bundle_file="$(mktemp /tmp/ohos-interface-runtime.XXXXXX.tar)"
path_list="$(mktemp /tmp/ohos-interface-runtime.XXXXXX.txt)"
cleanup() {
  rm -f "${bundle_file}" "${path_list}"
}
trap cleanup EXIT

for package_name in "$@"; do
  share_dir="${PREFIX_DIR}/share/${package_name}"
  if [[ ! -d "${share_dir}" ]]; then
    echo "Missing package share directory: ${share_dir}" >&2
    exit 1
  fi
  printf 'share/%s\n' "${package_name}" >> "${path_list}"

  for index_name in packages rosidl_interfaces package_run_dependencies parent_prefix_path; do
    index_entry="${PREFIX_DIR}/share/ament_index/resource_index/${index_name}/${package_name}"
    if [[ -e "${index_entry}" ]]; then
      printf 'share/ament_index/resource_index/%s/%s\n' "${index_name}" "${package_name}" >> "${path_list}"
    fi
  done

  package_dir="${PREFIX_DIR}/lib/python${TARGET_PYTHON_VERSION}/site-packages/${package_name}"
  if [[ ! -d "${package_dir}" ]]; then
    echo "Missing target Python package directory: ${package_dir}" >&2
    exit 1
  fi

  printf 'lib/python%s/site-packages/%s\n' "${TARGET_PYTHON_VERSION}" "${package_name}" >> "${path_list}"

  while IFS= read -r egg_info_dir; do
    printf '%s\n' "${egg_info_dir#${PREFIX_DIR}/}" >> "${path_list}"
  done < <(find "${PREFIX_DIR}/lib/python${TARGET_PYTHON_VERSION}/site-packages" -maxdepth 1 -type d -name "${package_name}-*.egg-info" | sort)

  while IFS= read -r runtime_lib; do
    printf '%s\n' "${runtime_lib#${PREFIX_DIR}/}" >> "${path_list}"
  done < <(find "${PREFIX_DIR}/lib" -maxdepth 1 \( -type f -o -type l \) -name "lib${package_name}__rosidl*.so*" | sort)
done

sort -u -o "${path_list}" "${path_list}"

tar -C "${PREFIX_DIR}" -cf "${bundle_file}" -T "${path_list}"

remote_bundle_parent="${REMOTE_BUNDLE%/*}"
if [[ "${remote_bundle_parent}" == "${REMOTE_BUNDLE}" ]]; then
  remote_bundle_parent="."
fi

run_hdc_direct shell "mkdir -p ${remote_bundle_parent} ${REMOTE_PREFIX}"
ensure_remote_file_uploaded "${bundle_file}" "${REMOTE_BUNDLE}"
run_hdc_direct shell \
  "cd ${REMOTE_PREFIX} && tar -xf ${REMOTE_BUNDLE} && chmod -R u+rwX,go+rX ${REMOTE_PREFIX}"
wait_for_device_connected || true

for package_name in "$@"; do
  wait_for_remote_path "${REMOTE_PREFIX}/share/${package_name}"
  wait_for_remote_path "${REMOTE_PREFIX}/lib/python${TARGET_PYTHON_VERSION}/site-packages/${package_name}"
done

echo "ros2_interface_runtime_deploy_ok"
echo "device_id=${DEVICE_ID}"
echo "remote_prefix=${REMOTE_PREFIX}"
printf 'packages=%s\n' "$*"
