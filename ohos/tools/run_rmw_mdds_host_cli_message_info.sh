#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_host_cli_message_info.sh

Runs host-side ros2 topic echo/pub through rmw_mdds_cpp with
--include-message-info and verifies sequence-number metadata is delivered.
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
DOMAIN_ID="${RMW_MDDS_HOST_CLI_MESSAGE_INFO_DOMAIN_ID:-$((($$ % 80) + 150))}"
TOPIC_NAME="${RMW_MDDS_HOST_CLI_MESSAGE_INFO_TOPIC:-/rmw_mdds_host_cli_message_info_${DOMAIN_ID}_$$}"
PAYLOAD="rmw_mdds_host_cli_message_info_ok_${DOMAIN_ID}_$$"
LOG_DIR="${TMPDIR:-/tmp}/rmw_mdds_host_cli_message_info_${DOMAIN_ID}_$$"
ECHO_LOG="${LOG_DIR}/echo.log"
PUB_LOG="${LOG_DIR}/pub.log"

require_path() {
  local path="$1"
  [[ -e "${path}" ]] || {
    echo "missing required path: ${path}" >&2
    exit 1
  }
}

dump_logs() {
  echo "--- message-info echo log ---" >&2
  sed -n '1,220p' "${ECHO_LOG}" >&2 || true
  echo "--- message-info pub log ---" >&2
  sed -n '1,120p' "${PUB_LOG}" >&2 || true
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

timeout 20s ros2 topic echo \
  --include-message-info \
  --once \
  --no-daemon \
  "${TOPIC_NAME}" std_msgs/msg/String >"${ECHO_LOG}" 2>&1 &
echo_pid=$!

cleanup() {
  kill "${echo_pid}" >/dev/null 2>&1 || true
  wait "${echo_pid}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

sleep 2

if ! timeout 20s ros2 topic pub --times 1 -r 5 -w 1 \
    "${TOPIC_NAME}" std_msgs/msg/String \
    "{data: '${PAYLOAD}'}" >"${PUB_LOG}" 2>&1; then
  echo "ros2 topic pub for message-info probe failed" >&2
  dump_logs
  exit 1
fi

if ! timeout 20s bash -c "while kill -0 '${echo_pid}' 2>/dev/null; do sleep 0.2; done"; then
  echo "message-info echo did not finish after publish" >&2
  dump_logs
  exit 1
fi

if ! grep -q "${PAYLOAD}" "${ECHO_LOG}"; then
  echo "message-info payload not observed by ros2 topic echo" >&2
  dump_logs
  exit 1
fi

if ! grep -Eq '^publication_sequence_number: [1-9][0-9]*$' "${ECHO_LOG}"; then
  echo "publication_sequence_number was not reported as a non-zero value" >&2
  dump_logs
  exit 1
fi

if ! grep -Eq '^reception_sequence_number: [1-9][0-9]*$' "${ECHO_LOG}"; then
  echo "reception_sequence_number was not reported as a non-zero value" >&2
  dump_logs
  exit 1
fi

echo "RESULT|rmw_mdds_host_cli_message_info|PASS|sequence_numbers"
echo "rmw_mdds_host_cli_message_info_ok"
