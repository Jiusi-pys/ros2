#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_doctor.sh <device-id> [domain-id]

Runs a board-side ROS 2 RMW selection smoke with RMW_IMPLEMENTATION=rmw_mdds_cpp.

Environment:
  HDC_BIN                         HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX         ROS 2 prefix on device, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_HDC_TIMEOUT_SECONDS    HDC shell timeout, default: 20
  RMW_MDDS_DOCTOR_TIMEOUT_SECONDS Board-side doctor timeout, default: 8
EOF
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage
  exit 2
fi

DEVICE_ID="$1"
DOMAIN_ID="${2:-${ROS_DOMAIN_ID:-87}}"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-20}"
DOCTOR_TIMEOUT_SECONDS="${RMW_MDDS_DOCTOR_TIMEOUT_SECONDS:-8}"
UNDERLAY_PREFIX="/data/local/tmp/ohos-prefix"
FASTDDS_PREFIX="/data/local/tmp/ohos-fastdds"

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

capture_hdc_shell_allow_timeout() {
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
  [[ ${status} -eq 0 || ${status} -eq 139 || ${status} -eq 124 ]]
}

require_remote_file() {
  local path="$1"
  local output
  output="$(capture_hdc_shell "${DEVICE_ID}" "test -e '${path}' && echo OK || echo MISSING:${path}")"
  if ! grep -q '^OK$' <<< "${output}"; then
    echo "${output}" >&2
    exit 1
  fi
}

run_rmw_command() {
  local command="$1"
  capture_hdc_shell "${DEVICE_ID}" \
    "$(remote_env) ${command}"
}

run_rmw_command_allow_timeout() {
  local command="$1"
  capture_hdc_shell_allow_timeout "${DEVICE_ID}" \
    "$(remote_env) ${command}"
}

remote_env() {
  cat <<EOF
PREFIX='${REMOTE_PREFIX}'; UNDERLAY_PREFIX='${UNDERLAY_PREFIX}'; FASTDDS_PREFIX='${FASTDDS_PREFIX}'; VENDOR_LIB_PATH=; for dir in \${PREFIX}/opt/*/lib; do [ -d \${dir} ] && VENDOR_LIB_PATH=\${VENDOR_LIB_PATH:+\${VENDOR_LIB_PATH}:}\${dir}; done; UNDERLAY_VENDOR_LIB_PATH=; for dir in \${UNDERLAY_PREFIX}/opt/*/lib; do [ -d \${dir} ] && UNDERLAY_VENDOR_LIB_PATH=\${UNDERLAY_VENDOR_LIB_PATH:+\${UNDERLAY_VENDOR_LIB_PATH}:}\${dir}; done; export LD_LIBRARY_PATH=\${PREFIX}/lib:\${UNDERLAY_PREFIX}/lib:\${FASTDDS_PREFIX}/lib\${VENDOR_LIB_PATH:+:\${VENDOR_LIB_PATH}}\${UNDERLAY_VENDOR_LIB_PATH:+:\${UNDERLAY_VENDOR_LIB_PATH}}:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64; unset LD_PRELOAD; export HOME=/data/local/tmp; export ROS_LOG_DIR=/data/local/tmp/roslogs; export ROS_DISTRO=jazzy; export ROSDISTRO_INDEX_URL=file://\${PREFIX}/share/rosdistro/index-v4.yaml; export AMENT_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}; export CMAKE_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}:\${FASTDDS_PREFIX}; export COLCON_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}; export PYTHONPATH=\${PREFIX}/lib/python3.12/site-packages:\${UNDERLAY_PREFIX}/lib/python3.12/site-packages:\${UNDERLAY_PREFIX}/lib/python3.11/site-packages; export ROS_DOMAIN_ID='${DOMAIN_ID}'; export RMW_IMPLEMENTATION=rmw_mdds_cpp;
EOF
}

cleanup_rmw_processes() {
  capture_hdc_shell "${DEVICE_ID}" \
    "ps -ef | grep -E '[r]os2 doctor|[r]os2 topic list|[r]os2-daemon.*rmw_mdds_cpp|[r]mw_mdds_broker_trigger_server.py|[r]mw_mdds_broker' | while read -r user pid rest; do kill -9 \"\${pid}\" 2>/dev/null || true; done; rm -f /data/local/tmp/rmw_mdds_cpp.sock" >/dev/null || true
}

require_remote_file "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${REMOTE_PREFIX}/share/rosdistro/index-v4.yaml"
require_remote_file "${REMOTE_PREFIX}/share/rosdistro/jazzy/distribution.yaml"

cleanup_rmw_processes
trap cleanup_rmw_processes EXIT

doctor_output="$(run_rmw_command_allow_timeout "timeout ${DOCTOR_TIMEOUT_SECONDS}s ${REMOTE_PREFIX}/bin/ros2 doctor --report 2>&1")"
printf '%s\n' "${doctor_output}"
if grep -Eq "Report entry point .* fails to load|No module named 'rosdistro'" <<< "${doctor_output}"; then
  echo "RESULT|rmw_mdds_doctor|FAIL|doctor_report_plugin_load_failed" >&2
  exit 1
fi
if grep -q "Fail to call NetworkReport class functions" <<< "${doctor_output}"; then
  echo "RESULT|rmw_mdds_doctor|FAIL|doctor_network_report_failed" >&2
  exit 1
fi
if grep -q "Fail to call PackageReport class functions" <<< "${doctor_output}"; then
  echo "RESULT|rmw_mdds_doctor|FAIL|doctor_package_report_failed" >&2
  exit 1
fi
if grep -q "Fail to call RosdistroReport class functions" <<< "${doctor_output}"; then
  echo "RESULT|rmw_mdds_doctor|FAIL|doctor_rosdistro_report_failed" >&2
  exit 1
fi
if grep -q "Expected RMW implementation identifier" <<< "${doctor_output}"; then
  echo "RESULT|rmw_mdds_doctor|FAIL|doctor_loaded_wrong_rmw" >&2
  exit 1
fi

topic_output="$(run_rmw_command_allow_timeout "timeout ${DOCTOR_TIMEOUT_SECONDS}s ${REMOTE_PREFIX}/bin/ros2 topic list 2>&1")"
printf '%s\n' "${topic_output}"
if grep -q "Expected RMW implementation identifier" <<< "${topic_output}"; then
  echo "RESULT|rmw_mdds_doctor|FAIL|topic_list_loaded_wrong_rmw" >&2
  exit 1
fi
if ! grep -q "/parameter_events" <<< "${topic_output}"; then
  echo "RESULT|rmw_mdds_doctor|FAIL|topic_list_missing_parameter_events" >&2
  exit 1
fi

echo "RESULT|rmw_mdds_doctor|PASS|domain=${DOMAIN_ID}"
echo "rmw_mdds_doctor_ok"
