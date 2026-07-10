#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_host_cli_graph.sh

Runs host-side ROS 2 graph CLI commands through rmw_mdds_cpp against live
demo_nodes_cpp talker/listener nodes and verifies verbose topic endpoint info.
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
DOMAIN_ID="${RMW_MDDS_HOST_CLI_GRAPH_DOMAIN_ID:-$((($$ % 80) + 150))}"
TOPIC_NAME="${RMW_MDDS_HOST_CLI_GRAPH_TOPIC:-/chatter}"
LOG_DIR="${TMPDIR:-/tmp}/rmw_mdds_host_cli_graph_${DOMAIN_ID}_$$"
TALKER_LOG="${LOG_DIR}/talker.log"
LISTENER_LOG="${LOG_DIR}/listener.log"
NODE_LIST_LOG="${LOG_DIR}/node_list.log"
TOPIC_LIST_LOG="${LOG_DIR}/topic_list.log"
TOPIC_INFO_LOG="${LOG_DIR}/topic_info_verbose.log"

require_path() {
  local path="$1"
  [[ -e "${path}" ]] || {
    echo "missing required path: ${path}" >&2
    exit 1
  }
}

dump_logs() {
  echo "--- ros2 node list log ---" >&2
  sed -n '1,120p' "${NODE_LIST_LOG}" >&2 || true
  echo "--- ros2 topic list log ---" >&2
  sed -n '1,120p' "${TOPIC_LIST_LOG}" >&2 || true
  echo "--- ros2 topic info verbose log ---" >&2
  sed -n '1,220p' "${TOPIC_INFO_LOG}" >&2 || true
  echo "--- talker log ---" >&2
  sed -n '1,120p' "${TALKER_LOG}" >&2 || true
  echo "--- listener log ---" >&2
  sed -n '1,120p' "${LISTENER_LOG}" >&2 || true
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

timeout 30s ros2 run demo_nodes_cpp talker >"${TALKER_LOG}" 2>&1 &
talker_pid=$!
timeout 30s ros2 run demo_nodes_cpp listener >"${LISTENER_LOG}" 2>&1 &
listener_pid=$!

cleanup() {
  kill "${talker_pid}" "${listener_pid}" >/dev/null 2>&1 || true
  wait "${talker_pid}" >/dev/null 2>&1 || true
  wait "${listener_pid}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

graph_ready=0
for _ in $(seq 1 100); do
  timeout 10s ros2 node list --no-daemon >"${NODE_LIST_LOG}" 2>&1 || true
  timeout 10s ros2 topic list --no-daemon >"${TOPIC_LIST_LOG}" 2>&1 || true
  if grep -qx '/talker' "${NODE_LIST_LOG}" &&
      grep -qx '/listener' "${NODE_LIST_LOG}" &&
      grep -qx "${TOPIC_NAME}" "${TOPIC_LIST_LOG}"; then
    graph_ready=1
    break
  fi
  sleep 0.25
done

if [[ "${graph_ready}" != "1" ]]; then
  echo "ROS graph did not expose talker/listener/${TOPIC_NAME}" >&2
  dump_logs
  exit 1
fi

if ! timeout 20s ros2 topic info --verbose --no-daemon "${TOPIC_NAME}" >"${TOPIC_INFO_LOG}" 2>&1; then
  echo "ros2 topic info --verbose failed" >&2
  dump_logs
  exit 1
fi

if ! grep -q 'Type: std_msgs/msg/String' "${TOPIC_INFO_LOG}"; then
  echo "topic info did not report std_msgs/msg/String" >&2
  dump_logs
  exit 1
fi

if ! grep -q 'Publisher count: 1' "${TOPIC_INFO_LOG}"; then
  echo "topic info did not report one publisher" >&2
  dump_logs
  exit 1
fi

if ! grep -q 'Subscription count: 1' "${TOPIC_INFO_LOG}"; then
  echo "topic info did not report one subscription" >&2
  dump_logs
  exit 1
fi

if ! grep -q 'Node name: talker' "${TOPIC_INFO_LOG}" ||
    ! grep -q 'Endpoint type: PUBLISHER' "${TOPIC_INFO_LOG}"; then
  echo "topic info did not include talker publisher endpoint" >&2
  dump_logs
  exit 1
fi

if ! grep -q 'Node name: listener' "${TOPIC_INFO_LOG}" ||
    ! grep -q 'Endpoint type: SUBSCRIPTION' "${TOPIC_INFO_LOG}"; then
  echo "topic info did not include listener subscription endpoint" >&2
  dump_logs
  exit 1
fi

if ! grep -q 'Topic type hash: RIHS01_' "${TOPIC_INFO_LOG}"; then
  echo "topic info did not include RIHS01 type hash" >&2
  dump_logs
  exit 1
fi

echo "RESULT|rmw_mdds_host_cli_graph|PASS|${TOPIC_NAME}"
echo "rmw_mdds_host_cli_graph_ok"
