#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_host_cli_lifecycle.sh

Runs host-side ros2 lifecycle get/set commands through rmw_mdds_cpp against
the standard lifecycle_talker demo node and verifies configure/activate.
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
DOMAIN_ID="${RMW_MDDS_HOST_CLI_LIFECYCLE_DOMAIN_ID:-$((($$ % 80) + 150))}"
NODE_NAME="${RMW_MDDS_HOST_CLI_LIFECYCLE_NODE:-/lc_talker}"
LOG_DIR="${TMPDIR:-/tmp}/rmw_mdds_host_cli_lifecycle_${DOMAIN_ID}_$$"
TALKER_LOG="${LOG_DIR}/lifecycle_talker.log"
GET_INITIAL_LOG="${LOG_DIR}/lifecycle_get_initial.log"
CONFIGURE_LOG="${LOG_DIR}/lifecycle_configure.log"
GET_CONFIGURED_LOG="${LOG_DIR}/lifecycle_get_configured.log"
ACTIVATE_LOG="${LOG_DIR}/lifecycle_activate.log"
GET_ACTIVE_LOG="${LOG_DIR}/lifecycle_get_active.log"

require_path() {
  local path="$1"
  [[ -e "${path}" ]] || {
    echo "missing required path: ${path}" >&2
    exit 1
  }
}

dump_logs() {
  echo "--- lifecycle get initial log ---" >&2
  sed -n '1,120p' "${GET_INITIAL_LOG}" >&2 || true
  echo "--- lifecycle configure log ---" >&2
  sed -n '1,120p' "${CONFIGURE_LOG}" >&2 || true
  echo "--- lifecycle get configured log ---" >&2
  sed -n '1,120p' "${GET_CONFIGURED_LOG}" >&2 || true
  echo "--- lifecycle activate log ---" >&2
  sed -n '1,120p' "${ACTIVATE_LOG}" >&2 || true
  echo "--- lifecycle get active log ---" >&2
  sed -n '1,120p' "${GET_ACTIVE_LOG}" >&2 || true
  echo "--- lifecycle_talker log ---" >&2
  sed -n '1,180p' "${TALKER_LOG}" >&2 || true
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

timeout 30s ros2 run lifecycle lifecycle_talker >"${TALKER_LOG}" 2>&1 &
talker_pid=$!

cleanup() {
  kill "${talker_pid}" >/dev/null 2>&1 || true
  wait "${talker_pid}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

wait_lifecycle_state() {
  local expected_state="$1"
  local output_log="$2"
  local failure_message="$3"
  local deadline=$((SECONDS + 30))

  while (( SECONDS < deadline )); do
    if timeout 20s ros2 lifecycle get --no-daemon --spin-time 2 \
        "${NODE_NAME}" >"${output_log}" 2>&1 &&
        grep -Eq "^${expected_state}([[:space:]]|\\[)" "${output_log}"; then
      return 0
    fi
    sleep 1
  done

  echo "${failure_message}" >&2
  dump_logs
  exit 1
}

node_ready=0
for _ in $(seq 1 100); do
  if ros2 lifecycle nodes --no-daemon --spin-time 1 2>/dev/null |
      grep -qx "${NODE_NAME}"; then
    node_ready=1
    break
  fi
  sleep 0.25
done

if [[ "${node_ready}" != "1" ]]; then
  echo "lifecycle node did not appear: ${NODE_NAME}" >&2
  dump_logs
  exit 1
fi

wait_lifecycle_state "unconfigured" "${GET_INITIAL_LOG}" \
  "initial lifecycle state was not unconfigured"

if ! timeout 20s ros2 lifecycle set --no-daemon --spin-time 2 \
    "${NODE_NAME}" configure >"${CONFIGURE_LOG}" 2>&1; then
  echo "ros2 lifecycle configure failed" >&2
  dump_logs
  exit 1
fi

if ! grep -q 'Transitioning successful' "${CONFIGURE_LOG}"; then
  echo "lifecycle configure did not report success" >&2
  dump_logs
  exit 1
fi

wait_lifecycle_state "inactive" "${GET_CONFIGURED_LOG}" \
  "configured lifecycle state was not inactive"

if ! timeout 20s ros2 lifecycle set --no-daemon --spin-time 2 \
    "${NODE_NAME}" activate >"${ACTIVATE_LOG}" 2>&1; then
  echo "ros2 lifecycle activate failed" >&2
  dump_logs
  exit 1
fi

if ! grep -q 'Transitioning successful' "${ACTIVATE_LOG}"; then
  echo "lifecycle activate did not report success" >&2
  dump_logs
  exit 1
fi

wait_lifecycle_state "active" "${GET_ACTIVE_LOG}" \
  "activated lifecycle state was not active"

echo "RESULT|rmw_mdds_host_cli_lifecycle|PASS|active"
echo "rmw_mdds_host_cli_lifecycle_ok"
