#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_broker_service.sh <device-id> [domain-id]

Runs a same-board ROS 2 std_srvs/Trigger smoke through one rmw_mdds_broker
process and two independent rmw_mdds_cpp client processes.

Environment:
  HDC_BIN                               HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX               ROS 2 prefix on device, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_BROKER_SOCKET                Broker socket path, default: /data/local/tmp/rmw_mdds_cpp.sock
  RMW_MDDS_BROKER_MANAGED               Reuse an already-running broker when set to 1/true/yes/on
  RMW_MDDS_SERVICE_NAME                 Service name, default: /rmw_mdds_broker_trigger
  RMW_MDDS_SERVICE_WAIT_SECONDS         Client wait_for_service timeout, default: 20
  RMW_MDDS_SERVICE_TIMEOUT_SECONDS      Client request timeout, default: 20
  RMW_MDDS_HDC_TIMEOUT_SECONDS          HDC shell timeout, default: 120
EOF
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage
  exit 2
fi

DEVICE_ID="$1"
DOMAIN_ID="${2:-${ROS_DOMAIN_ID:-200}}"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
BROKER_BIN="${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker"
BROKER_SOCKET="${RMW_MDDS_BROKER_SOCKET:-/data/local/tmp/rmw_mdds_cpp.sock}"
BROKER_MANAGED="${RMW_MDDS_BROKER_MANAGED:-0}"
SERVICE_NAME="${RMW_MDDS_SERVICE_NAME:-/rmw_mdds_broker_trigger}"
SERVICE_WAIT_SECONDS="${RMW_MDDS_SERVICE_WAIT_SECONDS:-20}"
SERVICE_TIMEOUT_SECONDS="${RMW_MDDS_SERVICE_TIMEOUT_SECONDS:-20}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-120}"
LOG_DIR="${RMW_MDDS_LOG_DIR:-/data/local/tmp/rmw_mdds_broker_service}"
BROKER_LOG="${LOG_DIR}/broker.log"
SERVER_LOG="${LOG_DIR}/server.log"
CLIENT_LOG="${LOG_DIR}/client.log"
ROS_LOG_DIR_REMOTE="${LOG_DIR}/roslog"
SERVER_SCRIPT_REMOTE="/data/local/tmp/rmw_mdds_broker_trigger_server.py"
CLIENT_SCRIPT_REMOTE="/data/local/tmp/rmw_mdds_broker_trigger_client.py"
DEFAULT_RMW_MDDS_BRIDGE_LIBRARY="/no/such/libmdds_bridge_shared.z.so"
INVALID_BRIDGE_LIBRARY="${RMW_MDDS_INVALID_BRIDGE_LIBRARY:-${DEFAULT_RMW_MDDS_BRIDGE_LIBRARY}}"

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

send_file() {
  local device_id="$1"
  local local_path="$2"
  local remote_path="$3"
  local output_file
  output_file="$(mktemp)"
  set +e
  timeout "${HDC_TIMEOUT_SECONDS}s" "${HDC_BIN}" -t "${device_id}" file send "${local_path}" "${remote_path}" >"${output_file}" 2>&1
  local status=$?
  set -e
  cat "${output_file}"
  rm -f "${output_file}"
  [[ ${status} -eq 0 || ${status} -eq 139 ]]
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

remote_env() {
  cat <<EOF
PREFIX='${REMOTE_PREFIX}'; UNDERLAY_PREFIX='/data/local/tmp/ohos-prefix'; FASTDDS_PREFIX='/data/local/tmp/ohos-fastdds'; VENDOR_LIB_PATH=; for dir in \${PREFIX}/opt/*/lib; do [ -d \${dir} ] && VENDOR_LIB_PATH=\${VENDOR_LIB_PATH:+\${VENDOR_LIB_PATH}:}\${dir}; done; UNDERLAY_VENDOR_LIB_PATH=; for dir in \${UNDERLAY_PREFIX}/opt/*/lib; do [ -d \${dir} ] && UNDERLAY_VENDOR_LIB_PATH=\${UNDERLAY_VENDOR_LIB_PATH:+\${UNDERLAY_VENDOR_LIB_PATH}:}\${dir}; done; export LD_PRELOAD='/data/local/release/usr/lib/libpython3.12.so.1.0'; export PYTHONHOME='/data/local/release/usr'; export HOME='/data/local/tmp'; export ROS_LOG_DIR='${ROS_LOG_DIR_REMOTE}'; export LD_LIBRARY_PATH=\${PREFIX}/lib:\${PREFIX}/lib/rmw_mdds_cpp:\${UNDERLAY_PREFIX}/lib:\${FASTDDS_PREFIX}/lib\${VENDOR_LIB_PATH:+:\${VENDOR_LIB_PATH}}\${UNDERLAY_VENDOR_LIB_PATH:+:\${UNDERLAY_VENDOR_LIB_PATH}}:/data/local/tmp:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64; export AMENT_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}; export CMAKE_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}:\${FASTDDS_PREFIX}; export COLCON_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}; export PYTHONPATH=\${PREFIX}/lib/python3.12/site-packages:\${UNDERLAY_PREFIX}/lib/python3.12/site-packages:\${UNDERLAY_PREFIX}/lib/python3.11/site-packages; export ROS_DOMAIN_ID='${DOMAIN_ID}'; export RMW_IMPLEMENTATION=rmw_mdds_cpp; export RMW_MDDS_BROKER=1; export RMW_MDDS_BROKER_SOCKET='${BROKER_SOCKET}'; export RMW_MDDS_BRIDGE_LIBRARY='${INVALID_BRIDGE_LIBRARY}';
EOF
}

remote_broker_env() {
  cat <<EOF
PREFIX='${REMOTE_PREFIX}'; export LD_LIBRARY_PATH=\${PREFIX}/lib:\${PREFIX}/lib/rmw_mdds_cpp:/data/local/tmp:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64;
EOF
}

cleanup_remote_processes() {
  local broker_cleanup=""
  if ! broker_managed_enabled; then
    broker_cleanup="pkill -f 'rmw_mdds_broker --socket ${BROKER_SOCKET}' 2>/dev/null || true;"
  fi
  capture_hdc_shell "${DEVICE_ID}" \
    "${broker_cleanup} pkill -f '${SERVER_SCRIPT_REMOTE}' 2>/dev/null || true; pkill -f '${CLIENT_SCRIPT_REMOTE}' 2>/dev/null || true" >/dev/null || true
}

broker_managed_enabled() {
  case "${BROKER_MANAGED}" in
    1|true|TRUE|yes|YES|on|ON)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

wait_for_socket() {
  local start_ts
  start_ts="$(date +%s)"
  while (( $(date +%s) - start_ts < SERVICE_WAIT_SECONDS )); do
    local output
    output="$(capture_hdc_shell "${DEVICE_ID}" "test -e '${BROKER_SOCKET}' && echo OK || true")"
    if grep -q '^OK$' <<< "${output}"; then
      return 0
    fi
    sleep 1
  done
  echo "RESULT|rmw_mdds_broker_service|FAIL|broker_socket_missing" >&2
  capture_hdc_shell "${DEVICE_ID}" "cat '${BROKER_LOG}' 2>/dev/null || true" >&2 || true
  return 1
}

wait_for_server_started() {
  local start_ts
  start_ts="$(date +%s)"
  while (( $(date +%s) - start_ts < SERVICE_WAIT_SECONDS )); do
    local output
    output="$(capture_hdc_shell "${DEVICE_ID}" "cat '${SERVER_LOG}' 2>/dev/null || true")"
    if grep -q 'rmw_mdds_broker_trigger_server_started' <<< "${output}"; then
      return 0
    fi
    sleep 1
  done
  echo "RESULT|rmw_mdds_broker_service|FAIL|server_not_ready" >&2
  capture_hdc_shell "${DEVICE_ID}" "cat '${SERVER_LOG}' 2>/dev/null || true" >&2 || true
  return 1
}

trap cleanup_remote_processes EXIT

require_remote_file "${REMOTE_PREFIX}/bin/ros2"
require_remote_file "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${REMOTE_PREFIX}/lib/python3.12/site-packages/rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so"
require_remote_file "${BROKER_BIN}"

SERVER_SCRIPT_LOCAL="$(mktemp /tmp/rmw_mdds_broker_trigger_server.XXXXXX.py)"
CLIENT_SCRIPT_LOCAL="$(mktemp /tmp/rmw_mdds_broker_trigger_client.XXXXXX.py)"
trap 'rm -f "${SERVER_SCRIPT_LOCAL}" "${CLIENT_SCRIPT_LOCAL}"; cleanup_remote_processes' EXIT

cat >"${SERVER_SCRIPT_LOCAL}" <<'PY'
#!/usr/bin/env python3

import os

import rclpy
from rclpy.parameter import Parameter
from std_srvs.srv import Trigger


def main() -> None:
    service_name = os.environ.get("RMW_MDDS_SERVICE_NAME", "/rmw_mdds_broker_trigger")
    rclpy.init()
    node = rclpy.create_node(
        "rmw_mdds_broker_trigger_server",
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
        response.message = f"broker_trigger_count={count['value']}"
        print(response.message, flush=True)
        return response

    node.create_service(Trigger, service_name, handle)
    print("rmw_mdds_broker_trigger_server_started", flush=True)
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
    service_name = os.environ.get("RMW_MDDS_SERVICE_NAME", "/rmw_mdds_broker_trigger")
    wait_seconds = float(os.environ.get("RMW_MDDS_SERVICE_WAIT_SECONDS", "20"))
    timeout_seconds = float(os.environ.get("RMW_MDDS_SERVICE_TIMEOUT_SECONDS", "20"))
    rclpy.init()
    node = rclpy.create_node(
        "rmw_mdds_broker_trigger_client",
        enable_rosout=False,
        start_parameter_services=False,
        parameter_overrides=[
            Parameter("start_type_description_service", Parameter.Type.BOOL, False),
        ],
    )
    client = node.create_client(Trigger, service_name)
    print("TRIGGER_CLIENT_CREATED", flush=True)
    deadline = time.monotonic() + wait_seconds
    while rclpy.ok() and time.monotonic() < deadline:
        if client.wait_for_service(timeout_sec=0.2):
            break
        rclpy.spin_once(node, timeout_sec=0.0)
    else:
        print("TRIGGER_CLIENT_SERVICE_UNAVAILABLE", flush=True)
        node.destroy_node()
        rclpy.shutdown()
        return 1

    future = client.call_async(Trigger.Request())
    deadline = time.monotonic() + timeout_seconds
    while rclpy.ok() and not future.done() and time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.1)
    if not future.done():
        print("TRIGGER_CLIENT_TIMEOUT", flush=True)
        node.destroy_node()
        rclpy.shutdown()
        return 2
    response = future.result()
    print(f"TRIGGER_RESPONSE success={response.success} message={response.message}", flush=True)
    node.destroy_node()
    rclpy.shutdown()
    return 0 if response.success and "broker_trigger_count=" in response.message else 3


if __name__ == "__main__":
    sys.exit(main())
PY

send_file "${DEVICE_ID}" "${SERVER_SCRIPT_LOCAL}" "${SERVER_SCRIPT_REMOTE}" >/dev/null
send_file "${DEVICE_ID}" "${CLIENT_SCRIPT_LOCAL}" "${CLIENT_SCRIPT_REMOTE}" >/dev/null

cleanup_remote_processes
if broker_managed_enabled; then
  capture_hdc_shell "${DEVICE_ID}" \
    "mkdir -p '${LOG_DIR}' '${ROS_LOG_DIR_REMOTE}'; rm -f '${SERVER_LOG}' '${CLIENT_LOG}'" >/dev/null
else
  capture_hdc_shell "${DEVICE_ID}" \
    "mkdir -p '${LOG_DIR}' '${ROS_LOG_DIR_REMOTE}'; rm -f '${BROKER_SOCKET}' '${BROKER_LOG}' '${SERVER_LOG}' '${CLIENT_LOG}'; chmod +x '${BROKER_BIN}'" >/dev/null
  capture_hdc_shell "${DEVICE_ID}" \
    "$(remote_broker_env) nohup sh -c '${BROKER_BIN} --socket ${BROKER_SOCKET} > ${BROKER_LOG} 2>&1' >/dev/null 2>&1 & echo rmw_mdds_broker_started" >/dev/null
fi
wait_for_socket

capture_hdc_shell "${DEVICE_ID}" \
  "$(remote_env) export RMW_MDDS_SERVICE_NAME='${SERVICE_NAME}'; nohup /data/local/release/usr/bin/python3.12 '${SERVER_SCRIPT_REMOTE}' > '${SERVER_LOG}' 2>&1 & echo rmw_mdds_broker_service_server_started" >/dev/null
wait_for_server_started

set +e
client_output="$(
  capture_hdc_shell "${DEVICE_ID}" \
    "$(remote_env) export RMW_MDDS_SERVICE_NAME='${SERVICE_NAME}'; export RMW_MDDS_SERVICE_WAIT_SECONDS='${SERVICE_WAIT_SECONDS}'; export RMW_MDDS_SERVICE_TIMEOUT_SECONDS='${SERVICE_TIMEOUT_SECONDS}'; timeout '$((SERVICE_WAIT_SECONDS + SERVICE_TIMEOUT_SECONDS + 10))s' /data/local/release/usr/bin/python3.12 '${CLIENT_SCRIPT_REMOTE}' > '${CLIENT_LOG}' 2>&1; RC=\$?; echo CLIENT_RC:\${RC}; cat '${CLIENT_LOG}' 2>/dev/null || true; exit \${RC}"
)"
client_status=$?
set -e
printf '%s\n' "${client_output}"

server_output="$(capture_hdc_shell "${DEVICE_ID}" "cat '${SERVER_LOG}' 2>/dev/null || true")"
printf '%s\n' "${server_output}"

if [[ ${client_status} -eq 0 ]] &&
    grep -q "TRIGGER_RESPONSE success=True" <<< "${client_output}" &&
    grep -q "broker_trigger_count=" <<< "${server_output}"; then
  echo "RESULT|rmw_mdds_broker_service|PASS|domain=${DOMAIN_ID}|service=${SERVICE_NAME}"
  echo "rmw_mdds_broker_service_ok"
  exit 0
fi

echo "RESULT|rmw_mdds_broker_service|FAIL|domain=${DOMAIN_ID}|service=${SERVICE_NAME}" >&2
echo "--- broker log ---" >&2
capture_hdc_shell "${DEVICE_ID}" "cat '${BROKER_LOG}' 2>/dev/null || true" >&2 || true
echo "--- server log ---" >&2
printf '%s\n' "${server_output}" >&2
echo "--- client log ---" >&2
printf '%s\n' "${client_output}" >&2
exit 1
