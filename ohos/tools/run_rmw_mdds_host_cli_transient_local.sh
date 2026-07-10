#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_host_cli_transient_local.sh

Runs host-side ros2 topic pub/echo through rmw_mdds_cpp and verifies a
transient-local publisher replays one retained std_msgs/String sample to a
late transient-local subscription.
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
DOMAIN_ID="${RMW_MDDS_HOST_CLI_TRANSIENT_DOMAIN_ID:-$((($$ % 80) + 150))}"
TOPIC_NAME="${RMW_MDDS_HOST_CLI_TRANSIENT_TOPIC:-/rmw_mdds_host_cli_transient_${DOMAIN_ID}_$$}"
PAYLOAD="rmw_mdds_host_cli_transient_ok_${DOMAIN_ID}_$$"
LOG_DIR="${TMPDIR:-/tmp}/rmw_mdds_host_cli_transient_${DOMAIN_ID}_$$"
PUB_LOG="${LOG_DIR}/pub.log"
ECHO_LOG="${LOG_DIR}/echo.log"

require_path() {
  local path="$1"
  [[ -e "${path}" ]] || {
    echo "missing required path: ${path}" >&2
    exit 1
  }
}

dump_logs() {
  echo "--- transient-local pub log ---" >&2
  sed -n '1,180p' "${PUB_LOG}" >&2 || true
  echo "--- transient-local echo log ---" >&2
  sed -n '1,180p' "${ECHO_LOG}" >&2 || true
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

timeout 25s ros2 topic pub --times 1 -r 1 -w 0 --keep-alive 15 \
  --qos-reliability reliable \
  --qos-durability transient_local \
  --qos-history keep_last \
  --qos-depth 1 \
  "${TOPIC_NAME}" std_msgs/msg/String \
  "{data: '${PAYLOAD}'}" >"${PUB_LOG}" 2>&1 &
pub_pid=$!

cleanup() {
  kill "${pub_pid}" >/dev/null 2>&1 || true
  wait "${pub_pid}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

for _ in $(seq 1 80); do
  if grep -q "${PAYLOAD}" "${PUB_LOG}"; then
    break
  fi
  if ! kill -0 "${pub_pid}" 2>/dev/null; then
    echo "transient-local publisher exited before publishing" >&2
    dump_logs
    exit 1
  fi
  sleep 0.1
done

if ! grep -q "${PAYLOAD}" "${PUB_LOG}"; then
  echo "transient-local publisher did not publish the expected payload" >&2
  dump_logs
  exit 1
fi

if ! timeout 20s ros2 topic echo \
    --qos-reliability reliable \
    --qos-durability transient_local \
    --qos-history keep_last \
    --qos-depth 1 \
    "${TOPIC_NAME}" std_msgs/msg/String --once --no-daemon >"${ECHO_LOG}" 2>&1; then
  echo "late transient-local echo did not receive retained payload" >&2
  dump_logs
  exit 1
fi

if ! grep -q "${PAYLOAD}" "${ECHO_LOG}"; then
  echo "retained transient-local payload not observed by ros2 topic echo" >&2
  dump_logs
  exit 1
fi

echo "RESULT|rmw_mdds_host_cli_transient_local|PASS|${TOPIC_NAME}"
echo "rmw_mdds_host_cli_transient_local_ok"
