#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_host_cli_params.sh

Runs host-side ros2 param set/get through rmw_mdds_cpp against the standard
demo_nodes_cpp parameter_blackboard node and verifies the written value.
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
DOMAIN_ID="${RMW_MDDS_HOST_CLI_PARAMS_DOMAIN_ID:-$((($$ % 80) + 150))}"
NODE_NAME="${RMW_MDDS_HOST_CLI_PARAMS_NODE:-/parameter_blackboard}"
PARAM_NAME="${RMW_MDDS_HOST_CLI_PARAMS_NAME:-rmw_mdds_host_param}"
PARAM_VALUE="${RMW_MDDS_HOST_CLI_PARAMS_VALUE:-4242}"
LOG_DIR="${TMPDIR:-/tmp}/rmw_mdds_host_cli_params_${DOMAIN_ID}_$$"
NODE_LOG="${LOG_DIR}/parameter_blackboard.log"
SET_LOG="${LOG_DIR}/param_set.log"
GET_LOG="${LOG_DIR}/param_get.log"

require_path() {
  local path="$1"
  [[ -e "${path}" ]] || {
    echo "missing required path: ${path}" >&2
    exit 1
  }
}

dump_logs() {
  echo "--- ros2 param set log ---" >&2
  sed -n '1,160p' "${SET_LOG}" >&2 || true
  echo "--- ros2 param get log ---" >&2
  sed -n '1,160p' "${GET_LOG}" >&2 || true
  echo "--- parameter_blackboard log ---" >&2
  sed -n '1,160p' "${NODE_LOG}" >&2 || true
}

require_path "${ROOT_DIR}/install/setup.bash"
mkdir -p "${LOG_DIR}"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

export LD_LIBRARY_PATH="${ROOT_DIR}/build/rmw_mdds_cpp:${LD_LIBRARY_PATH:-}"
export RMW_IMPLEMENTATION=rmw_mdds_cpp
export ROS_DOMAIN_ID="${DOMAIN_ID}"
export ROS_LOG_DIR="${LOG_DIR}"

timeout 30s ros2 run demo_nodes_cpp parameter_blackboard >"${NODE_LOG}" 2>&1 &
node_pid=$!

cleanup() {
  kill "${node_pid}" >/dev/null 2>&1 || true
  wait "${node_pid}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

node_ready=0
for _ in $(seq 1 100); do
  if ros2 service list --no-daemon --spin-time 1 2>/dev/null |
      grep -qx "${NODE_NAME}/set_parameters"; then
    node_ready=1
    break
  fi
  sleep 0.25
done

if [[ "${node_ready}" != "1" ]]; then
  echo "parameter node did not expose set_parameters: ${NODE_NAME}" >&2
  dump_logs
  exit 1
fi

if ! timeout 20s ros2 param set --no-daemon --spin-time 2 --timeout 10 \
    "${NODE_NAME}" "${PARAM_NAME}" "${PARAM_VALUE}" >"${SET_LOG}" 2>&1; then
  echo "ros2 param set failed" >&2
  dump_logs
  exit 1
fi

if ! grep -q "Set parameter successful" "${SET_LOG}"; then
  echo "ros2 param set did not report success" >&2
  dump_logs
  exit 1
fi

if ! timeout 20s ros2 param get --no-daemon --spin-time 2 --timeout 10 \
    "${NODE_NAME}" "${PARAM_NAME}" >"${GET_LOG}" 2>&1; then
  echo "ros2 param get failed" >&2
  dump_logs
  exit 1
fi

if ! grep -Eq "Integer value is:[[:space:]]+${PARAM_VALUE}|${PARAM_VALUE}" "${GET_LOG}"; then
  echo "ros2 param get did not return ${PARAM_VALUE}" >&2
  dump_logs
  exit 1
fi

echo "RESULT|rmw_mdds_host_cli_params|PASS|${PARAM_NAME}=${PARAM_VALUE}"
echo "rmw_mdds_host_cli_params_ok"
