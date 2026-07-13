#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_host_cli_action.sh

Runs host-side ros2 action send_goal through rmw_mdds_cpp and verifies the
action_tutorials_cpp Fibonacci action server accepts and completes the goal.
EOF
}

if [[ $# -gt 0 ]]; then
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage
      exit 2
      ;;
  esac
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DOMAIN_ID="${RMW_MDDS_HOST_CLI_ACTION_DOMAIN_ID:-$((($$ % 80) + 150))}"
ACTION_NAME="${RMW_MDDS_HOST_CLI_ACTION_NAME:-/fibonacci}"
LOG_DIR="${TMPDIR:-/tmp}/rmw_mdds_host_cli_action_${DOMAIN_ID}_$$"
SERVER_LOG="${LOG_DIR}/server.log"
GOAL_LOG="${LOG_DIR}/goal.log"

require_path() {
  local path="$1"
  [[ -e "${path}" ]] || {
    echo "missing required path: ${path}" >&2
    exit 1
  }
}

dump_logs() {
  echo "--- action goal log ---" >&2
  sed -n '1,200p' "${GOAL_LOG}" >&2 || true
  echo "--- action server log ---" >&2
  sed -n '1,200p' "${SERVER_LOG}" >&2 || true
}

require_path "${ROOT_DIR}/install/setup.bash"
mkdir -p "${LOG_DIR}"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

export LD_LIBRARY_PATH="${ROOT_DIR}/build/rmw_mdds_cpp:${LD_LIBRARY_PATH:-}"
export RMW_IMPLEMENTATION=rmw_mdds_cpp
export RMW_MDDS_BROKER_SOCKET="${LOG_DIR}/broker.sock"
export ROS_DOMAIN_ID="${DOMAIN_ID}"
export ROS_LOG_DIR="${LOG_DIR}"

stop_ros2_daemon() {
  timeout 10s ros2 daemon stop >/dev/null 2>&1 || true
}

# ros2 action list is daemon-backed. A daemon from an earlier run can have the
# same domain/RMW key but a different broker socket, so replace it before the
# server and query establish their graph connections.
stop_ros2_daemon

timeout 30s ros2 run action_tutorials_cpp fibonacci_action_server >"${SERVER_LOG}" 2>&1 &
server_pid=$!

cleanup() {
  kill "${server_pid}" >/dev/null 2>&1 || true
  wait "${server_pid}" >/dev/null 2>&1 || true
  stop_ros2_daemon
}
trap cleanup EXIT

action_ready=0
for _ in $(seq 1 100); do
  if timeout 5s ros2 action list 2>/dev/null | grep -qx "${ACTION_NAME}"; then
    action_ready=1
    break
  fi
  kill -0 "${server_pid}" >/dev/null 2>&1 || break
  sleep 0.25
done

if [[ "${action_ready}" != "1" ]]; then
  echo "action did not appear: ${ACTION_NAME}" >&2
  dump_logs
  exit 1
fi

if ! timeout 40s ros2 action send_goal --feedback --timeout 25 \
    "${ACTION_NAME}" action_tutorials_interfaces/action/Fibonacci \
    "{order: 5}" >"${GOAL_LOG}" 2>&1; then
  echo "ros2 action send_goal failed" >&2
  dump_logs
  exit 1
fi

if ! grep -Eq 'Goal accepted|accepted' "${GOAL_LOG}"; then
  echo "action goal was not accepted" >&2
  dump_logs
  exit 1
fi

if ! grep -Eq 'Goal finished with status:[[:space:]]+SUCCEEDED|status:[[:space:]]+SUCCEEDED' "${GOAL_LOG}"; then
  echo "action goal did not finish with SUCCEEDED" >&2
  dump_logs
  exit 1
fi

if ! grep -Eq 'Result:|sequence:' "${GOAL_LOG}"; then
  echo "action result did not contain a Fibonacci sequence" >&2
  dump_logs
  exit 1
fi

echo "RESULT|rmw_mdds_host_cli_action|PASS|order=5"
echo "rmw_mdds_host_cli_action_ok"
