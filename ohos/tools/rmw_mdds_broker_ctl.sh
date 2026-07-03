#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: rmw_mdds_broker_ctl.sh <device-id> <start|stop|restart|status> [socket-path]

Manages a board-side rmw_mdds_broker process for rmw_mdds_cpp broker mode.

Environment:
  HDC_BIN                         HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX         ROS 2 prefix on device, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_BROKER_SOCKET          Broker socket path, default: /data/local/tmp/rmw_mdds_cpp.sock
  RMW_MDDS_BROKER_LOG_DIR         Broker log/pid directory, default: /data/local/tmp/rmw_mdds_broker
  RMW_MDDS_HDC_TIMEOUT_SECONDS    HDC shell timeout, default: 120
EOF
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

DEVICE_ID="$1"
ACTION="$2"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
BROKER_SOCKET="${3:-${RMW_MDDS_BROKER_SOCKET:-/data/local/tmp/rmw_mdds_cpp.sock}}"
LOG_DIR="${RMW_MDDS_BROKER_LOG_DIR:-/data/local/tmp/rmw_mdds_broker}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-120}"
BROKER_BIN="${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker"
BROKER_LOG="${LOG_DIR}/broker.log"
PID_FILE="${LOG_DIR}/broker.pid"

case "${ACTION}" in
  start|stop|restart|status)
    ;;
  *)
    usage
    exit 2
    ;;
esac

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

remote_defs() {
  cat <<EOF
PREFIX='${REMOTE_PREFIX}'; BROKER_BIN='${BROKER_BIN}'; BROKER_SOCKET='${BROKER_SOCKET}'; LOG_DIR='${LOG_DIR}'; BROKER_LOG='${BROKER_LOG}'; PID_FILE='${PID_FILE}'; export LD_LIBRARY_PATH=\${PREFIX}/lib:\${PREFIX}/lib/rmw_mdds_cpp:/data/local/tmp:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64;
EOF
}

remote_status_command() {
  cat <<'EOF'
if [ -f "${PID_FILE}" ]; then pid=$(cat "${PID_FILE}" 2>/dev/null || true); if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then echo "RUNNING:${pid}"; exit 0; fi; fi; if ps -ef | grep "rmw_mdds_broker --socket ${BROKER_SOCKET}" | grep -v grep >/dev/null 2>&1; then echo "RUNNING:unknown"; else echo "STOPPED"; fi
EOF
}

wait_for_socket() {
  local start_ts
  start_ts="$(date +%s)"
  while (( $(date +%s) - start_ts < 20 )); do
    local output
    output="$(capture_hdc_shell "${DEVICE_ID}" "$(remote_defs) test -e \"\${BROKER_SOCKET}\" && echo OK || true")"
    if grep -q '^OK$' <<< "${output}"; then
      return 0
    fi
    sleep 1
  done
  echo "RESULT|rmw_mdds_broker_ctl|FAIL|action=start|socket=${BROKER_SOCKET}|reason=socket_missing" >&2
  capture_hdc_shell "${DEVICE_ID}" "$(remote_defs) cat \"\${BROKER_LOG}\" 2>/dev/null || true" >&2 || true
  return 1
}

require_broker_bin() {
  local output
  output="$(capture_hdc_shell "${DEVICE_ID}" "$(remote_defs) test -e \"\${BROKER_BIN}\" && echo OK || echo MISSING:\"\${BROKER_BIN}\"")"
  if ! grep -q '^OK$' <<< "${output}"; then
    echo "${output}" >&2
    exit 1
  fi
}

broker_status() {
  local output
  output="$(capture_hdc_shell "${DEVICE_ID}" "$(remote_defs) $(remote_status_command)")"
  if grep -q '^RUNNING:' <<< "${output}"; then
    echo "${output}"
    echo "RESULT|rmw_mdds_broker_ctl|PASS|action=status|state=running|socket=${BROKER_SOCKET}"
  else
    echo "${output}"
    echo "RESULT|rmw_mdds_broker_ctl|PASS|action=status|state=stopped|socket=${BROKER_SOCKET}"
  fi
}

broker_start() {
  require_broker_bin
  local output
  output="$(capture_hdc_shell "${DEVICE_ID}" "$(remote_defs) mkdir -p \"\${LOG_DIR}\"; chmod +x \"\${BROKER_BIN}\"; $(remote_status_command)")"
  if grep -q '^RUNNING:' <<< "${output}"; then
    echo "${output}"
    echo "RESULT|rmw_mdds_broker_ctl|PASS|action=start|state=running|socket=${BROKER_SOCKET}"
    return 0
  fi

  capture_hdc_shell "${DEVICE_ID}" \
    "$(remote_defs) mkdir -p \"\${LOG_DIR}\"; rm -f \"\${BROKER_SOCKET}\" \"\${BROKER_LOG}\"; nohup sh -c 'LD_LIBRARY_PATH=\"'\"\${LD_LIBRARY_PATH}\"'\" exec \"'\"\${BROKER_BIN}\"'\" --socket \"'\"\${BROKER_SOCKET}\"'\" > \"'\"\${BROKER_LOG}\"'\" 2>&1' >/dev/null 2>&1 & echo \$! > \"\${PID_FILE}\"" >/dev/null
  wait_for_socket
  broker_status >/dev/null
  echo "RESULT|rmw_mdds_broker_ctl|PASS|action=start|state=running|socket=${BROKER_SOCKET}"
}

broker_stop() {
  capture_hdc_shell "${DEVICE_ID}" \
    "$(remote_defs) if [ -f \"\${PID_FILE}\" ]; then pid=\$(cat \"\${PID_FILE}\" 2>/dev/null || true); if [ -n \"\${pid}\" ]; then kill \"\${pid}\" 2>/dev/null || true; sleep 1; kill -9 \"\${pid}\" 2>/dev/null || true; fi; fi; ps -ef | grep \"rmw_mdds_broker --socket \${BROKER_SOCKET}\" | grep -v grep | while read -r user pid rest; do [ -z \"\${pid}\" ] || kill -9 \"\${pid}\" 2>/dev/null || true; done; rm -f \"\${PID_FILE}\" \"\${BROKER_SOCKET}\"" >/dev/null || true
  echo "RESULT|rmw_mdds_broker_ctl|PASS|action=stop|state=stopped|socket=${BROKER_SOCKET}"
}

case "${ACTION}" in
  start)
    broker_start
    ;;
  stop)
    broker_stop
    ;;
  restart)
    broker_stop
    broker_start
    echo "RESULT|rmw_mdds_broker_ctl|PASS|action=restart|state=running|socket=${BROKER_SOCKET}"
    ;;
  status)
    broker_status
    ;;
esac
