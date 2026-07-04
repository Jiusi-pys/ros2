#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_cross_board_rmw_mdds_service.sh <server-device-id> <client-device-id> [domain-id]

Runs a dual-RK3588A ROS 2 std_srvs/Trigger native MDDS RMW service smoke:
  A: RMW_IMPLEMENTATION=rmw_mdds_cpp service server
  B: RMW_IMPLEMENTATION=rmw_mdds_cpp service client

Environment:
  HDC_BIN                         HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX         ROS 2 prefix on both devices, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_BRIDGE_LIBRARY         MDDS bridge library, default: /data/local/tmp/libmdds_bridge_shared.z.so
  RMW_MDDS_SERVICE_NAME           Service name, default: /rclpy_mdds_trigger
  RMW_MDDS_SERVICE_WARMUP_SECONDS Client-side spin time before first request, default: 8
  RMW_MDDS_SERVICE_TIMEOUT_SECONDS Client request timeout, default: 25
  RMW_MDDS_SERVICE_COMMAND_TIMEOUT_SECONDS Remote client process timeout, default: same as HDC timeout
  RMW_MDDS_HDC_TIMEOUT_SECONDS    HDC shell timeout, default: 120
  RMW_MDDS_HDC_RETRY_ATTEMPTS     HDC retry attempts, default: 5
EOF
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

SERVER_DEVICE_ID="$1"
CLIENT_DEVICE_ID="$2"
DOMAIN_ID="${3:-${ROS_DOMAIN_ID:-93}}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
BRIDGE_LIBRARY="${RMW_MDDS_BRIDGE_LIBRARY:-${REMOTE_PREFIX}/lib/libmdds_bridge_shared.z.so}"
SERVICE_NAME="${RMW_MDDS_SERVICE_NAME:-/rclpy_mdds_trigger}"
WARMUP_SECONDS="${RMW_MDDS_SERVICE_WARMUP_SECONDS:-8}"
SERVICE_TIMEOUT_SECONDS="${RMW_MDDS_SERVICE_TIMEOUT_SECONDS:-25}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-120}"
HDC_RETRY_ATTEMPTS="${RMW_MDDS_HDC_RETRY_ATTEMPTS:-5}"
HDC_RETRY_DELAY_SECONDS="${RMW_MDDS_HDC_RETRY_DELAY_SECONDS:-1}"
SERVICE_COMMAND_TIMEOUT_SECONDS="${RMW_MDDS_SERVICE_COMMAND_TIMEOUT_SECONDS:-${HDC_TIMEOUT_SECONDS}}"
LOG_DIR="${RMW_MDDS_LOG_DIR:-/data/local/tmp/rmw_mdds_service_cross}"
SERVER_SCRIPT_REMOTE="/data/local/tmp/rmw_mdds_trigger_server.py"
CLIENT_SCRIPT_REMOTE="/data/local/tmp/rmw_mdds_trigger_client.py"
SERVER_LOG="${LOG_DIR}/server.log"
CLIENT_LOG="${LOG_DIR}/client.log"

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

send_file() {
  local device_id="$1"
  local local_path="$2"
  local remote_path="$3"
  local attempt
  local output_file
  local status
  for ((attempt = 1; attempt <= HDC_RETRY_ATTEMPTS; attempt += 1)); do
    output_file="$(mktemp)"
    set +e
    timeout "${HDC_TIMEOUT_SECONDS}s" "${HDC_BIN}" -t "${device_id}" file send "${local_path}" "${remote_path}" >"${output_file}" 2>&1
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
  if ! grep -q '^OK$' <<< "${output}"; then
    echo "${output}" >&2
    exit 1
  fi
}

remote_env() {
  cat <<EOF
PREFIX='${REMOTE_PREFIX}'; UNDERLAY_PREFIX='/data/local/tmp/ohos-prefix'; FASTDDS_PREFIX='/data/local/tmp/ohos-fastdds'; BR='${BRIDGE_LIBRARY}'; VENDOR_LIB_PATH=; for dir in \${PREFIX}/opt/*/lib; do [ -d \${dir} ] && VENDOR_LIB_PATH=\${VENDOR_LIB_PATH:+\${VENDOR_LIB_PATH}:}\${dir}; done; UNDERLAY_VENDOR_LIB_PATH=; for dir in \${UNDERLAY_PREFIX}/opt/*/lib; do [ -d \${dir} ] && UNDERLAY_VENDOR_LIB_PATH=\${UNDERLAY_VENDOR_LIB_PATH:+\${UNDERLAY_VENDOR_LIB_PATH}:}\${dir}; done; export LD_PRELOAD=/data/local/release/usr/lib/libpython3.12.so.1.0; export PYTHONHOME='/data/local/release/usr'; export HOME='/data/local/tmp'; export ROS_LOG_DIR='/data/local/tmp/roslogs'; export LD_LIBRARY_PATH=\${PREFIX}/lib:\${UNDERLAY_PREFIX}/lib:\${FASTDDS_PREFIX}/lib\${VENDOR_LIB_PATH:+:\${VENDOR_LIB_PATH}}\${UNDERLAY_VENDOR_LIB_PATH:+:\${UNDERLAY_VENDOR_LIB_PATH}}:/data/local/tmp:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64; export AMENT_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}; export CMAKE_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}:\${FASTDDS_PREFIX}; export COLCON_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}; export PYTHONPATH=\${PREFIX}/lib/python3.12/site-packages:\${UNDERLAY_PREFIX}/lib/python3.12/site-packages:\${UNDERLAY_PREFIX}/lib/python3.11/site-packages; export ROS_DOMAIN_ID='${DOMAIN_ID}'; export RMW_IMPLEMENTATION='rmw_mdds_cpp'; export RMW_MDDS_BROKER=1; export RMW_MDDS_BRIDGE_LIBRARY=\${BR};
EOF
}

kill_remote_pattern() {
  local device_id="$1"
  local pattern="$2"
  capture_hdc_shell "${device_id}" \
    "ps -ef | grep '${pattern}' | grep -v grep | while read -r user pid rest; do [ -z \"\${pid}\" ] || kill -9 \"\${pid}\" 2>/dev/null || true; done" >/dev/null || true
}

cleanup() {
  kill_remote_pattern "${SERVER_DEVICE_ID}" "rmw_mdds_trigger_server.py"
  kill_remote_pattern "${CLIENT_DEVICE_ID}" "rmw_mdds_trigger_client.py"
}
trap cleanup EXIT

require_remote_file "${SERVER_DEVICE_ID}" "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${SERVER_DEVICE_ID}" "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${SERVER_DEVICE_ID}" "${REMOTE_PREFIX}/lib/python3.12/site-packages/rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so"
require_remote_file "${SERVER_DEVICE_ID}" "${BRIDGE_LIBRARY}"
require_remote_file "${CLIENT_DEVICE_ID}" "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${CLIENT_DEVICE_ID}" "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${CLIENT_DEVICE_ID}" "${REMOTE_PREFIX}/lib/python3.12/site-packages/rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so"
require_remote_file "${CLIENT_DEVICE_ID}" "${BRIDGE_LIBRARY}"

SERVER_SCRIPT_LOCAL="$(mktemp /tmp/rmw_mdds_trigger_server.XXXXXX.py)"
CLIENT_SCRIPT_LOCAL="$(mktemp /tmp/rmw_mdds_trigger_client.XXXXXX.py)"
trap 'rm -f "${SERVER_SCRIPT_LOCAL}" "${CLIENT_SCRIPT_LOCAL}"; cleanup' EXIT

cat >"${SERVER_SCRIPT_LOCAL}" <<'PY'
#!/usr/bin/env python3

import os

import rclpy
from rclpy.parameter import Parameter
from std_srvs.srv import Trigger


def main() -> None:
    service_name = os.environ.get("RMW_MDDS_SERVICE_NAME", "/rclpy_mdds_trigger")
    rclpy.init()
    node = rclpy.create_node(
        "rmw_mdds_trigger_server",
        enable_rosout=False,
        start_parameter_services=False,
        parameter_overrides=[
            Parameter("start_type_description_service", Parameter.Type.BOOL, False),
        ],
    )
    count = {"value": 0}

    def handle(_request: Trigger.Request, response: Trigger.Response) -> Trigger.Response:
        count["value"] += 1
        response.success = True
        response.message = f"trigger_count={count['value']}"
        print(response.message, flush=True)
        return response

    node.create_service(Trigger, service_name, handle)
    print("rmw_mdds_trigger_server_started", flush=True)
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
PY

cat >"${CLIENT_SCRIPT_LOCAL}" <<'PY'
#!/usr/bin/env python3

import os
import sys
import time

import rclpy
from rclpy.parameter import Parameter
from std_srvs.srv import Trigger


def main() -> int:
    service_name = os.environ.get("RMW_MDDS_SERVICE_NAME", "/rclpy_mdds_trigger")
    warmup_seconds = float(os.environ.get("RMW_MDDS_SERVICE_WARMUP_SECONDS", "8"))
    timeout_seconds = float(os.environ.get("RMW_MDDS_SERVICE_TIMEOUT_SECONDS", "25"))
    rclpy.init()
    node = rclpy.create_node(
        "rmw_mdds_trigger_client",
        enable_rosout=False,
        start_parameter_services=False,
        parameter_overrides=[
            Parameter("start_type_description_service", Parameter.Type.BOOL, False),
        ],
    )
    client = node.create_client(Trigger, service_name)
    print("TRIGGER_CLIENT_CREATED", flush=True)
    deadline = time.monotonic() + warmup_seconds
    while rclpy.ok() and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)
    future = client.call_async(Trigger.Request())
    deadline = time.monotonic() + timeout_seconds
    while rclpy.ok() and not future.done() and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)
    if not future.done():
        print("TRIGGER_CLIENT_TIMEOUT", flush=True)
        node.destroy_node()
        rclpy.shutdown()
        return 1
    response = future.result()
    print(f"TRIGGER_RESPONSE success={response.success} message={response.message}", flush=True)
    node.destroy_node()
    rclpy.shutdown()
    return 0 if response.success and "trigger_count=" in response.message else 2


if __name__ == "__main__":
    sys.exit(main())
PY

send_file "${SERVER_DEVICE_ID}" "${SERVER_SCRIPT_LOCAL}" "${SERVER_SCRIPT_REMOTE}" >/dev/null
send_file "${CLIENT_DEVICE_ID}" "${CLIENT_SCRIPT_LOCAL}" "${CLIENT_SCRIPT_REMOTE}" >/dev/null

cleanup
capture_hdc_shell "${SERVER_DEVICE_ID}" \
  "$(remote_env) export RMW_MDDS_SERVICE_NAME='${SERVICE_NAME}'; mkdir -p '${LOG_DIR}'; rm -f '${SERVER_LOG}'; nohup /data/local/release/usr/bin/python3.12 '${SERVER_SCRIPT_REMOTE}' > '${SERVER_LOG}' 2>&1 & echo service_server_started" >/dev/null

sleep 4

set +e
client_output="$(
  capture_hdc_shell "${CLIENT_DEVICE_ID}" \
    "$(remote_env) export RMW_MDDS_SERVICE_NAME='${SERVICE_NAME}'; export RMW_MDDS_SERVICE_WARMUP_SECONDS='${WARMUP_SECONDS}'; export RMW_MDDS_SERVICE_TIMEOUT_SECONDS='${SERVICE_TIMEOUT_SECONDS}'; mkdir -p '${LOG_DIR}'; rm -f '${CLIENT_LOG}'; timeout '${SERVICE_COMMAND_TIMEOUT_SECONDS}s' /data/local/release/usr/bin/python3.12 '${CLIENT_SCRIPT_REMOTE}' > '${CLIENT_LOG}' 2>&1; RC=\$?; echo CLIENT_RC:\${RC}; cat '${CLIENT_LOG}' 2>/dev/null || true; exit \${RC}"
)"
client_status=$?
set -e
printf '%s\n' "${client_output}"

server_output="$(capture_hdc_shell "${SERVER_DEVICE_ID}" "cat '${SERVER_LOG}' 2>/dev/null || true")"
printf '%s\n' "${server_output}"

if [[ ${client_status} -eq 0 ]] &&
    grep -q "TRIGGER_RESPONSE success=True" <<< "${client_output}" &&
    grep -q "trigger_count=" <<< "${server_output}"; then
  echo "RESULT|mdds_service_a_to_b|PASS|domain=${DOMAIN_ID}|service=${SERVICE_NAME}"
  echo "cross_board_rmw_mdds_service_ok"
  exit 0
fi

echo "RESULT|mdds_service_a_to_b|FAIL|domain=${DOMAIN_ID}|service=${SERVICE_NAME}" >&2
exit 1
