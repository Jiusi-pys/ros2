#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_host_cli_service.sh

Runs host-side ros2 service call through rmw_mdds_cpp and verifies the
demo_nodes_cpp AddTwoInts service returns the expected sum.
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
DOMAIN_ID="${RMW_MDDS_HOST_CLI_SERVICE_DOMAIN_ID:-$((($$ % 80) + 170))}"
SERVICE_NAME="${RMW_MDDS_HOST_CLI_SERVICE_NAME:-/add_two_ints}"
LOG_DIR="${TMPDIR:-/tmp}/rmw_mdds_host_cli_service_${DOMAIN_ID}_$$"
SERVER_LOG="${LOG_DIR}/server.log"
CALL_LOG="${LOG_DIR}/call.log"

require_path() {
  local path="$1"
  [[ -e "${path}" ]] || {
    echo "missing required path: ${path}" >&2
    exit 1
  }
}

dump_logs() {
  echo "--- service call log ---" >&2
  sed -n '1,160p' "${CALL_LOG}" >&2 || true
  echo "--- server log ---" >&2
  sed -n '1,160p' "${SERVER_LOG}" >&2 || true
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

timeout 30s ros2 run demo_nodes_cpp add_two_ints_server >"${SERVER_LOG}" 2>&1 &
server_pid=$!

cleanup() {
  kill "${server_pid}" >/dev/null 2>&1 || true
  wait "${server_pid}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

service_ready=0
for _ in $(seq 1 80); do
  if ros2 service list 2>/dev/null | grep -qx "${SERVICE_NAME}"; then
    service_ready=1
    break
  fi
  sleep 0.25
done

if [[ "${service_ready}" != "1" ]]; then
  echo "service did not appear: ${SERVICE_NAME}" >&2
  dump_logs
  exit 1
fi

if ! timeout 20s ros2 service call "${SERVICE_NAME}" example_interfaces/srv/AddTwoInts \
    "{a: 19, b: 23}" >"${CALL_LOG}" 2>&1; then
  echo "ros2 service call failed" >&2
  dump_logs
  exit 1
fi

if ! grep -Eq 'sum[=:][[:space:]]*42' "${CALL_LOG}"; then
  echo "service response did not contain sum=42" >&2
  dump_logs
  exit 1
fi

echo "RESULT|rmw_mdds_host_cli_service|PASS|sum=42"
echo "rmw_mdds_host_cli_service_ok"
