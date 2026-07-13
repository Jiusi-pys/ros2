#!/bin/sh
#
# Local or board-side P0 smoke for rmw_mdds_cpp. Run after sourcing the target
# overlay, or set ROS2_SETUP to the overlay setup.sh path.

set -eu

ROS2_SETUP="${ROS2_SETUP:-}"
ROS2_BIN="${ROS2_BIN:-ros2}"
DOMAIN_ID="${ROS_DOMAIN_ID:-93}"
TOPIC_NAME="${RMW_MDDS_SMOKE_TOPIC:-/rmw_mdds_cli_smoke}"
SERVICE_NAME="${RMW_MDDS_SMOKE_SERVICE:-/add_two_ints}"
ACTION_NAME="${RMW_MDDS_SMOKE_ACTION:-/fibonacci}"
WORK_DIR="${RMW_MDDS_SMOKE_WORK_DIR:-${TMPDIR:-/tmp}/rmw_mdds_cli_smoke.$$}"
TIMEOUT_SECONDS="${RMW_MDDS_SMOKE_TIMEOUT_SECONDS:-45}"

if [ -n "${ROS2_SETUP}" ]; then
  COLCON_PREFIX_OWNED=0
  if [ -z "${COLCON_CURRENT_PREFIX:-}" ]; then
    COLCON_CURRENT_PREFIX="${ROS2_SETUP%/*}"
    if [ "${COLCON_CURRENT_PREFIX}" = "${ROS2_SETUP}" ]; then
      COLCON_CURRENT_PREFIX=.
    fi
    export COLCON_CURRENT_PREFIX
    COLCON_PREFIX_OWNED=1
  fi
  set +u
  # shellcheck disable=SC1090
  . "${ROS2_SETUP}"
  set -u
  if [ "${COLCON_PREFIX_OWNED}" -eq 1 ]; then
    unset COLCON_CURRENT_PREFIX
  fi
fi

command -v "${ROS2_BIN}" >/dev/null || {
  echo "RESULT|rmw_mdds_cli_smoke|FAIL|ros2_not_found" >&2
  exit 1
}

export ROS_DOMAIN_ID="${DOMAIN_ID}"
export RMW_IMPLEMENTATION="rmw_mdds_cpp"
export RMW_MDDS_BROKER="${RMW_MDDS_BROKER:-1}"

mkdir -p "${WORK_DIR}"
BROKER_SOCKET_OWNED=0
if [ -z "${RMW_MDDS_BROKER_SOCKET:-}" ]; then
  export RMW_MDDS_BROKER_SOCKET="${WORK_DIR}/broker.sock"
  BROKER_SOCKET_OWNED=1
fi
TOPIC_ECHO_PID=""
SERVICE_PID=""
ACTION_PID=""
CHILD_PIDS=""
OWNED_BROKER_PIDS=""

cleanup() {
  PARENT_PIDS=""
  for pid in "${TOPIC_ECHO_PID}" "${SERVICE_PID}" "${ACTION_PID}"; do
    if [ -n "${pid}" ]; then
      PARENT_PIDS="${PARENT_PIDS} ${pid}"
    fi
  done

  CHILD_PIDS="$(
    ps -ef 2>/dev/null | while read -r process_user process_pid process_ppid process_rest; do
      case " ${PARENT_PIDS} " in
        *" ${process_ppid} "*) printf '%s\n' "${process_pid}" ;;
      esac
    done
  )"
  if [ "${BROKER_SOCKET_OWNED}" -eq 1 ]; then
    OWNED_BROKER_PIDS="$(
      ps -ef 2>/dev/null | while read -r process_user process_pid process_ppid process_rest; do
        case "${process_rest}" in
          *rmw_mdds_broker*"${RMW_MDDS_BROKER_SOCKET}"*) printf '%s\n' "${process_pid}" ;;
        esac
      done
    )"
  fi
  for pid in ${CHILD_PIDS} ${PARENT_PIDS} ${OWNED_BROKER_PIDS}; do
    kill "${pid}" 2>/dev/null || true
  done
  if [ -n "${CHILD_PIDS}${PARENT_PIDS}${OWNED_BROKER_PIDS}" ]; then
    sleep 1
  fi
  for pid in ${CHILD_PIDS} ${PARENT_PIDS} ${OWNED_BROKER_PIDS}; do
    kill -9 "${pid}" 2>/dev/null || true
  done
  for pid in ${PARENT_PIDS}; do
    wait "${pid}" 2>/dev/null || true
  done
  rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

wait_for_text() {
  wait_file="$1"
  wait_expected="$2"
  wait_started="$(date +%s)"
  while [ "$(( $(date +%s) - wait_started ))" -lt "${TIMEOUT_SECONDS}" ]; do
    grep -Fq "${wait_expected}" "${wait_file}" 2>/dev/null && return 0
    sleep 1
  done
  return 1
}

echo "RMW=${RMW_IMPLEMENTATION} BROKER=${RMW_MDDS_BROKER} DOMAIN=${ROS_DOMAIN_ID}"
"${ROS2_BIN}" topic list --no-daemon >"${WORK_DIR}/topic_list.log"
grep -Fxq '/rosout' "${WORK_DIR}/topic_list.log"
grep -Fxq '/parameter_events' "${WORK_DIR}/topic_list.log"
echo "RESULT|rmw_mdds_cli_topic_list|PASS"

"${ROS2_BIN}" topic echo "${TOPIC_NAME}" std_msgs/msg/String --no-daemon >"${WORK_DIR}/topic_echo.log" 2>&1 &
TOPIC_ECHO_PID="$!"
sleep 2
PAYLOAD="rmw_mdds_cli_smoke_${DOMAIN_ID}_$$"
timeout "${TIMEOUT_SECONDS}" "${ROS2_BIN}" topic pub --times 5 -r 5 -w 1 \
  "${TOPIC_NAME}" std_msgs/msg/String "{data: ${PAYLOAD}}" >"${WORK_DIR}/topic_pub.log" 2>&1
wait_for_text "${WORK_DIR}/topic_echo.log" "${PAYLOAD}"
echo "RESULT|rmw_mdds_cli_topic|PASS"

"${ROS2_BIN}" run demo_nodes_cpp add_two_ints_server >"${WORK_DIR}/service_server.log" 2>&1 &
SERVICE_PID="$!"
timeout "${TIMEOUT_SECONDS}" "${ROS2_BIN}" service call "${SERVICE_NAME}" \
  example_interfaces/srv/AddTwoInts '{a: 41, b: 1}' >"${WORK_DIR}/service_client.log" 2>&1
grep -Fq 'sum=42' "${WORK_DIR}/service_client.log"
echo "RESULT|rmw_mdds_cli_service|PASS"

"${ROS2_BIN}" run action_tutorials_cpp fibonacci_action_server >"${WORK_DIR}/action_server.log" 2>&1 &
ACTION_PID="$!"
timeout "${TIMEOUT_SECONDS}" "${ROS2_BIN}" action send_goal "${ACTION_NAME}" \
  action_tutorials_interfaces/action/Fibonacci '{order: 5}' --feedback >"${WORK_DIR}/action_client.log" 2>&1
grep -Fq 'Goal finished with status: SUCCEEDED' "${WORK_DIR}/action_client.log"
echo "RESULT|rmw_mdds_cli_action|PASS"

echo "RESULT|rmw_mdds_cli_smoke|PASS"
