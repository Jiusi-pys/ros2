#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/deploy_ros2_prefix.sh"
# language: "shell"
# summary: "Shell script defining `run_hdc_capture`, and `wait_for_device_connected` to sync the standalone ROS 2 prefix onto a board."
# symbols: ["run_hdc_capture", "wait_for_device_connected"]
# generated_by: "codex"
# codex-file-meta: end

set -euo pipefail

on_error() {
  local status=$?
  echo "deploy_ros2_prefix_failed status=${status} line=${BASH_LINENO[0]} command=${BASH_COMMAND}" >&2
  exit "${status}"
}
trap on_error ERR

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX_DIR="${ROS2_OHOS_PREFIX_DIR:-${ROOT_DIR}/install/ohos-ros2}"
DEVICE_ID="${OHOS_DEVICE_ID:-${1:-}}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-prefix}"
REMOTE_BUNDLE="${OHOS_REMOTE_BUNDLE:-/data/local/tmp/ohos-ros2-prefix.tar.gz}"
DEFAULT_WRAPPER="/home/kaihong/.codex/skills/ohos-hdc/scripts/device-control.sh"
HDC_TIMEOUT_SEND="${OHOS_HDC_TIMEOUT_SEND:-45s}"
HDC_TIMEOUT_SHELL="${OHOS_HDC_TIMEOUT_SHELL:-60s}"
HDC_RETRIES="${OHOS_HDC_RETRIES:-3}"
HDC_READY_TIMEOUT_SECONDS="${OHOS_HDC_READY_TIMEOUT_SECONDS:-30}"
REMOTE_PREFIX_CLEAN="${ROS2_OHOS_REMOTE_PREFIX_CLEAN:-1}"
STRICT_DEVICE_READY="${OHOS_STRICT_DEVICE_READY:-0}"
TARGET_PYTHON_VERSION="${ROS2_OHOS_TARGET_PYTHON_VERSION:-3.12}"
PACKAGE_RESOURCE_REL="share/ament_index/resource_index/packages"
PACKAGE_RESOURCE_DIR="${PREFIX_DIR}/${PACKAGE_RESOURCE_REL}"

if [[ -z "${DEVICE_ID}" ]]; then
  echo "Set OHOS_DEVICE_ID or pass the device id as the first argument." >&2
  exit 1
fi

if [[ ! -d "${PREFIX_DIR}" ]]; then
  echo "Missing prefix directory: ${PREFIX_DIR}" >&2
  exit 1
fi

if [[ ! -d "${PACKAGE_RESOURCE_DIR}" ]]; then
  echo "Missing ROS 2 package resource index: ${PACKAGE_RESOURCE_DIR}" >&2
  exit 1
fi

expected_package_count="$(find "${PACKAGE_RESOURCE_DIR}" -mindepth 1 -maxdepth 1 -type f | wc -l | tr -d '[:space:]')"
if [[ -z "${expected_package_count}" || "${expected_package_count}" -le 0 ]]; then
  echo "Package resource index is empty: ${PACKAGE_RESOURCE_DIR}" >&2
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

    if [[ "${output}" == *"Device not founded or connected"* || "${output}" == *"Connect server failed"* ]]; then
      if [[ ${attempt} -lt ${HDC_RETRIES} ]]; then
        sleep 1
        continue
      fi
      return 1
    fi

    if [[ "${output}" == *"__ohos_prefix_remote_ok_"* ]]; then
      return 0
    fi

    if [[ ${status} -eq 0 ]]; then
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

run_remote_capture_checked() {
  local timeout_value="$1"
  local remote_script="$2"
  local marker="__ohos_prefix_remote_ok_${RANDOM}_${RANDOM}__"
  local output_file
  local wrapped_script
  output_file="$(mktemp)"
  wrapped_script="( ${remote_script} ) && printf '%s\n' $(shell_quote "${marker}")"

  set +e
  run_hdc_capture "${timeout_value}" shell "${wrapped_script}" >"${output_file}"
  local status=$?
  set -e

  if ! grep -Fqx "${marker}" "${output_file}"; then
    echo "Remote command did not report success marker: ${remote_script}" >&2
    cat "${output_file}" >&2
    rm -f "${output_file}"
    if [[ ${status} -ne 0 ]]; then
      return "${status}"
    fi
    return 1
  fi

  grep -Fvx "${marker}" "${output_file}" || true
  rm -f "${output_file}"
}

run_remote_checked() {
  run_remote_capture_checked "$@" >/dev/null
}

ensure_remote_file_uploaded() {
  local local_path="$1"
  local remote_path="$2"
  local expected_size
  expected_size="$(wc -c < "${local_path}" | tr -d '[:space:]')"

  local attempt
  for ((attempt = 1; attempt <= HDC_RETRIES; ++attempt)); do
    set +e
    run_hdc_capture "${HDC_TIMEOUT_SEND}" file send "${local_path}" "${remote_path}"
    local send_status=$?
    set -e

    wait_for_device_connected_or_warn

    local size_output
    set +e
    size_output="$(run_remote_capture_checked "${HDC_TIMEOUT_SHELL}" "test -f $(shell_quote "${remote_path}") && wc -c < $(shell_quote "${remote_path}")" 2>&1)"
    local verify_status=$?
    set -e

    local remote_size
    remote_size="$(printf '%s\n' "${size_output}" | awk '/^[[:space:]]*[0-9]+[[:space:]]*$/ { count = $1 } END { if (count != "") print count }')"
    if [[ ${send_status} -eq 0 && ${verify_status} -eq 0 && "${remote_size}" == "${expected_size}" ]]; then
      return 0
    fi

    if [[ ${attempt} -lt ${HDC_RETRIES} ]]; then
      echo "warning: upload verification failed for ${remote_path} on attempt ${attempt}; retrying" >&2
      sleep 1
      continue
    fi

    echo "Failed to upload ${local_path} to ${remote_path}: expected ${expected_size} bytes, got ${remote_size:-unreadable}" >&2
    if [[ -n "${size_output}" ]]; then
      printf '%s\n' "${size_output}" >&2
    fi
    return 1
  done
}

wait_for_device_connected() {
  local start_ts
  start_ts="$(date +%s)"
  while true; do
    local output_file
    output_file="$(mktemp)"
    set +e
    timeout 10s "${HDC_BASE[@]}" shell "echo ready" >"${output_file}" 2>&1
    set -e
    local output
    output="$(cat "${output_file}")"
    rm -f "${output_file}"

    if [[ -n "${output}" ]]; then
      printf '%s\n' "${output}" >/dev/null
    fi

    if [[ "${output}" == *"ready"* ]]; then
      return 0
    fi

    if (( $(date +%s) - start_ts >= HDC_READY_TIMEOUT_SECONDS )); then
      echo "Timed out waiting for device ${DEVICE_ID} to report Connected." >&2
      return 1
    fi

    sleep 1
  done
}

wait_for_device_connected_or_warn() {
  if wait_for_device_connected; then
    return 0
  fi

  if [[ "${STRICT_DEVICE_READY}" == "1" ]]; then
    return 1
  fi

  echo "warning: device readiness probe timed out after transfer/extract; continuing because deployment steps already completed" >&2
}

bundle_file="$(mktemp /tmp/ohos-ros2-prefix.XXXXXX.tar.gz)"
supplement_bundle="$(mktemp /tmp/ohos-ros2-prefix-supplement.XXXXXX.tar.gz)"
supplement_list="$(mktemp /tmp/ohos-ros2-prefix-supplement.XXXXXX.txt)"
remote_list="$(mktemp /tmp/ohos-ros2-prefix-remote.XXXXXX.txt)"
missing_list="$(mktemp /tmp/ohos-ros2-prefix-missing.XXXXXX.txt)"
cleanup() {
  rm -f "${bundle_file}" "${supplement_bundle}" "${supplement_list}" "${remote_list}" "${missing_list}"
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

tar -C "${PREFIX_DIR}" -czf "${bundle_file}" "${tar_args[@]}"

(
  cd "${PREFIX_DIR}"
  if [[ -d lib ]]; then
    find lib -mindepth 1 -maxdepth 1 \( -type f -o -type l \) -name 'lib*__rosidl*.so*'
    while IFS= read -r site_packages_dir; do
      find "${site_packages_dir}" -mindepth 1 -maxdepth 1 \( -type d -o -type f -o -type l \)
    done < <(find lib -mindepth 2 -maxdepth 2 -type d -path "*/python*/site-packages" | sort)
  fi
) | sort -u > "${supplement_list}"

wait_for_device_connected_or_warn

remote_bundle_dir="$(dirname -- "${REMOTE_BUNDLE}")"
run_remote_checked "${HDC_TIMEOUT_SHELL}" "mkdir -p $(shell_quote "${remote_bundle_dir}")"
ensure_remote_file_uploaded "${bundle_file}" "${REMOTE_BUNDLE}"

if [[ "${REMOTE_PREFIX_CLEAN}" == "1" ]]; then
  cleanup_cmd="rm -rf $(shell_quote "${REMOTE_PREFIX}") && mkdir -p $(shell_quote "${REMOTE_PREFIX}")"
else
  cleanup_cmd="mkdir -p $(shell_quote "${REMOTE_PREFIX}")"
fi

run_remote_checked "${HDC_TIMEOUT_SHELL}" \
  "${cleanup_cmd} && cd $(shell_quote "${REMOTE_PREFIX}") && tar -xzf $(shell_quote "${REMOTE_BUNDLE}") && chmod -R u+rwX,go+rX $(shell_quote "${REMOTE_PREFIX}")"
wait_for_device_connected_or_warn
if [[ -s "${supplement_list}" ]]; then
  collect_remote_runtime_entries() {
    run_remote_capture_checked "${HDC_TIMEOUT_SHELL}" \
      "if [ -d $(shell_quote "${REMOTE_PREFIX}/lib") ]; then cd $(shell_quote "${REMOTE_PREFIX}") && find lib -mindepth 1 -maxdepth 1 \\( -type f -o -type l \\) -name 'lib*__rosidl*.so*' && find lib -mindepth 2 -maxdepth 2 -type d -path '*/python*/site-packages' | while IFS= read -r site_packages_dir; do find \"\$site_packages_dir\" -mindepth 1 -maxdepth 1 \\( -type d -o -type f -o -type l \\); done; fi"
  }

  if ! collect_remote_runtime_entries | sort -u > "${remote_list}"; then
    echo "warning: failed to collect remote runtime supplement entries; uploading host supplement set" >&2
    : > "${remote_list}"
  fi
  comm -23 "${supplement_list}" "${remote_list}" > "${missing_list}" || true

  if [[ -s "${missing_list}" ]]; then
    tar -C "${PREFIX_DIR}" -czf "${supplement_bundle}" -T "${missing_list}"
    ensure_remote_file_uploaded "${supplement_bundle}" "${REMOTE_BUNDLE}.supplement"
    run_remote_checked "${HDC_TIMEOUT_SHELL}" \
      "cd $(shell_quote "${REMOTE_PREFIX}") && tar -xzf $(shell_quote "${REMOTE_BUNDLE}.supplement") && chmod -R u+rwX,go+rX $(shell_quote "${REMOTE_PREFIX}")"
    wait_for_device_connected_or_warn
  fi
fi

remote_package_output="$(run_remote_capture_checked "${HDC_TIMEOUT_SHELL}" \
  "resource_dir=$(shell_quote "${REMOTE_PREFIX}/${PACKAGE_RESOURCE_REL}"); test -d \"\$resource_dir\" && find \"\$resource_dir\" -mindepth 1 -maxdepth 1 -type f | wc -l")"
remote_package_count="$(printf '%s\n' "${remote_package_output}" | awk '/^[[:space:]]*[0-9]+[[:space:]]*$/ { count = $1 } END { if (count != "") print count }')"
if [[ -z "${remote_package_count}" ]]; then
  echo "Failed to read remote package resource count from ${REMOTE_PREFIX}/${PACKAGE_RESOURCE_REL}" >&2
  printf '%s\n' "${remote_package_output}" >&2
  exit 1
fi
if [[ "${remote_package_count}" -ne "${expected_package_count}" ]]; then
  echo "Remote package resource count mismatch: expected ${expected_package_count}, got ${remote_package_count}" >&2
  exit 1
fi

run_remote_checked "${HDC_TIMEOUT_SHELL}" "test -f $(shell_quote "${REMOTE_PREFIX}/bin/ros2")"

echo "ros2_prefix_deploy_ok"
echo "device_id=${DEVICE_ID}"
echo "prefix_dir=${PREFIX_DIR}"
echo "remote_prefix=${REMOTE_PREFIX}"
echo "resource_index_packages=${remote_package_count}"
