#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ACTION_NAME="${RMW_MDDS_ACTION_BAG_NAME:-/rmw_mdds_action_bag}"
DOMAIN_ID="${RMW_MDDS_ACTION_BAG_DOMAIN_ID:-$((($$ % 50) + 180))}"
PLAY_DOMAIN_ID="$((DOMAIN_ID + 1))"
LOG_DIR="${RMW_MDDS_ACTION_BAG_LOG_DIR:-${TMPDIR:-/tmp}/rmw_mdds_action_bag_${DOMAIN_ID}_$$}"
BAG_DIR="${RMW_MDDS_ACTION_BAG_URI:-${LOG_DIR}/action_bag}"
PROBE_BIN="${ROOT_DIR}/install/lib/rmw_mdds_action_bag_probe/rmw_mdds_action_bag_probe"

SERVER_LOG="${LOG_DIR}/record_server.log"
CLIENT_LOG="${LOG_DIR}/record_client.log"
RECORD_LOG="${LOG_DIR}/record.log"
INFO_LOG="${LOG_DIR}/info.log"
PLAY_SERVER_LOG="${LOG_DIR}/play_server.log"
PLAY_LOG="${LOG_DIR}/play.log"

server_pid=""
recorder_pid=""

require_path() {
  local path="$1"
  [[ -e "${path}" ]] || {
    echo "missing required path: ${path}" >&2
    exit 1
  }
}

stop_process() {
  local pid="$1"
  [[ -n "${pid}" ]] || return 0
  if ! kill -0 "${pid}" 2>/dev/null; then
    wait "${pid}" 2>/dev/null || true
    return 0
  fi

  kill -TERM "${pid}" 2>/dev/null || true
  for _ in $(seq 1 100); do
    if ! kill -0 "${pid}" 2>/dev/null; then
      wait "${pid}" 2>/dev/null || true
      return 0
    fi
    sleep 0.1
  done

  kill -KILL "${pid}" 2>/dev/null || true
  wait "${pid}" 2>/dev/null || true
  echo "process required SIGKILL: ${pid}" >&2
  return 1
}

wait_for_marker() {
  local path="$1"
  local marker="$2"
  local pid="$3"
  for _ in $(seq 1 150); do
    if grep -q -- "${marker}" "${path}" 2>/dev/null; then
      return 0
    fi
    if ! kill -0 "${pid}" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  echo "marker not found: ${marker}" >&2
  sed -n '1,200p' "${path}" >&2 || true
  return 1
}

dump_logs() {
  for path in \
    "${SERVER_LOG}" "${CLIENT_LOG}" "${RECORD_LOG}" \
    "${INFO_LOG}" "${PLAY_SERVER_LOG}" "${PLAY_LOG}"
  do
    echo "--- ${path} ---" >&2
    sed -n '1,240p' "${path}" >&2 || true
  done
}

cleanup() {
  if [[ -n "${recorder_pid}" ]]; then
    stop_process "${recorder_pid}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${server_pid}" ]]; then
    stop_process "${server_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

require_path "${ROOT_DIR}/install/setup.bash"
require_path "${ROOT_DIR}/build/rmw_mdds_cpp"
require_path "${PROBE_BIN}"

mkdir -p "${LOG_DIR}"
rm -rf "${BAG_DIR}"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

export LD_LIBRARY_PATH="${ROOT_DIR}/build/rmw_mdds_cpp:${LD_LIBRARY_PATH:-}"
export RMW_IMPLEMENTATION=rmw_mdds_cpp
export ROS_LOG_DIR="${LOG_DIR}/ros"
mkdir -p "${ROS_LOG_DIR}"

export ROS_DOMAIN_ID="${DOMAIN_ID}"
export RMW_MDDS_BROKER_SOCKET="${LOG_DIR}/record_broker.sock"
rm -f "${RMW_MDDS_BROKER_SOCKET}"

"${PROBE_BIN}" server >"${SERVER_LOG}" 2>&1 &
server_pid=$!
wait_for_marker "${SERVER_LOG}" "ACTION_BAG_SERVER_READY" "${server_pid}"

ros2 bag record --actions "${ACTION_NAME}" -s sqlite3 -o "${BAG_DIR}" \
  --disable-keyboard-controls >"${RECORD_LOG}" 2>&1 &
recorder_pid=$!
wait_for_marker "${RECORD_LOG}" "Recording..." "${recorder_pid}"

timeout 35s "${PROBE_BIN}" client >"${CLIENT_LOG}" 2>&1
grep -q 'ACTION_BAG_CLIENT_PASS' "${CLIENT_LOG}"
sleep 1
stop_process "${recorder_pid}"
recorder_pid=""

ros2 bag info --verbose "${BAG_DIR}" >"${INFO_LOG}" 2>&1
grep -q 'Actions:[[:space:]]*1' "${INFO_LOG}"
grep -Fq "Action: ${ACTION_NAME} | Type: action_tutorials_interfaces/action/Fibonacci" "${INFO_LOG}"
grep -Eq 'Topic: feedback \| Count: [1-9][0-9]*' "${INFO_LOG}"
grep -Fq 'Service: send_goal | Request Count: 2 | Response Count: 2' "${INFO_LOG}"
grep -Fq 'Service: cancel_goal | Request Count: 1 | Response Count: 1' "${INFO_LOG}"
grep -Fq 'Service: get_result | Request Count: 2 | Response Count: 2' "${INFO_LOG}"

stop_process "${server_pid}"
server_pid=""

export ROS_DOMAIN_ID="${PLAY_DOMAIN_ID}"
export RMW_MDDS_BROKER_SOCKET="${LOG_DIR}/play_broker.sock"
rm -f "${RMW_MDDS_BROKER_SOCKET}"

"${PROBE_BIN}" server >"${PLAY_SERVER_LOG}" 2>&1 &
server_pid=$!
wait_for_marker "${PLAY_SERVER_LOG}" "ACTION_BAG_SERVER_READY" "${server_pid}"

timeout 30s ros2 bag play "${BAG_DIR}" --send-actions-as-client \
  --disable-keyboard-controls >"${PLAY_LOG}" 2>&1

for _ in $(seq 1 100); do
  if grep -q 'ACTION_BAG_SERVER_GOAL|count=2' "${PLAY_SERVER_LOG}" && \
      grep -q 'ACTION_BAG_SERVER_CANCEL|count=1' "${PLAY_SERVER_LOG}"; then
    break
  fi
  sleep 0.1
done
grep -q 'ACTION_BAG_SERVER_GOAL|count=2' "${PLAY_SERVER_LOG}"
grep -q 'ACTION_BAG_SERVER_CANCEL|count=1' "${PLAY_SERVER_LOG}"
if grep -Eq '\[(WARN|ERROR|FATAL)\]|terminate called|Aborted' "${PLAY_LOG}"; then
  echo "action playback emitted an error" >&2
  dump_logs
  exit 1
fi

stop_process "${server_pid}"
server_pid=""

echo "RESULT|rmw_mdds_host_action_bag|PASS|actions=1|send_goal=2|cancel_goal=1|get_result=2"
echo "ACTION_BAG_EVIDENCE_DIR=${LOG_DIR}"
echo "ACTION_BAG_URI=${BAG_DIR}"
echo "rmw_mdds_host_action_bag_ok"
