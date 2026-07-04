#!/usr/bin/env bash
set -euo pipefail

# Cross-board pub/sub-class lane matrix for rmw_mdds <-> rmw_fastrtps over the
# MDDS DSoftBus gateway. Generalizes run_cross_board_rmw_mdds_fastdds.sh to
# multiple message types/QoS and captures gateway toDds/toMdds counters so the
# DDS->MDDS direction is proven to traverse MDDS/DSoftBus, not rmw_mdds' RTPS.
#
#   A (mdds device):  RMW_IMPLEMENTATION=rmw_mdds_cpp via broker-owned MDDS bridge
#   B (fastdds dev):  RMW_IMPLEMENTATION=rmw_fastrtps_cpp + mdds_dds_gateway
#
# IMPORTANT (toybox): these boards have no awk and `pkill -f` does NOT match full
# cmdlines reliably. Stray continuous publishers from a prior lane corrupt MDDS
# discovery for the next one, so cleanup MUST use ps|grep|while-read PID kills and
# MUST run between every direction. Cross-board MDDS discovery is <10s on a clean
# slate; the "60-90s warmup" seen earlier was stale-process contamination.

usage() { cat >&2 <<'EOF'
Usage: run_cross_board_rmw_mdds_matrix.sh <mdds-device-id> <fastdds-device-id> [domain-id]
EOF
}
if [[ $# -lt 2 || $# -gt 3 ]]; then usage; exit 2; fi

MDDS_DEVICE_ID="$1"; FASTDDS_DEVICE_ID="$2"
# Split domains: the rmw_mdds node and the fastrtps node + gateway run on DIFFERENT
# ROS_DOMAIN_IDs so rmw_mdds' always-on RTPS leg cannot directly discover the
# fastrtps peer. This forces ALL cross-RMW traffic through gateway->MDDS->DSoftBus
# (MDDS bridging is topic-based, domain-independent), making the DSoftBus path the
# only route in both directions. Verified: rmw_mdds(101)<-fastrtps(102) reverse
# delivers only via gateway toMdds.
DDS_DOMAIN="${3:-${ROS_DOMAIN_ID:-101}}"
[[ "${DDS_DOMAIN}" =~ ^[0-9]+$ ]] && (( DDS_DOMAIN <= 231 )) || { echo "dds domain 0..231" >&2; exit 2; }
MDDS_DOMAIN="${MDDS_DOMAIN:-$(( DDS_DOMAIN + 1 ))}"

HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
BRIDGE_LIBRARY="${RMW_MDDS_BRIDGE_LIBRARY:-${REMOTE_PREFIX}/lib/libmdds_bridge_shared.z.so}"
GATEWAY_ENV="${MDDS_GATEWAY_ENV:-/data/local/tmp/device_gateway_env.sh}"
GATEWAY_BIN="${MDDS_GATEWAY_BIN:-/data/local/tmp/mdds_dds_gateway}"
GATEWAY_CONFIG="${MDDS_GATEWAY_CONFIG:-/data/local/tmp/gateway_rmw_mdds_matrix.yaml}"
POLL_TIMEOUT_SECONDS="${RMW_MDDS_POLL_TIMEOUT_SECONDS:-50}"
LOG_DIR="${RMW_MDDS_LOG_DIR:-/data/local/tmp/rmw_mdds_matrix}"
GATEWAY_LOG="${LOG_DIR}/gateway.log"
BRIDGE_ENV="RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BRIDGE_LIBRARY}"

capture_hdc_shell() {
  local device_id="$1" command="$2" output_file status
  output_file="$(mktemp)"; set +e
  timeout 45s "${HDC_BIN}" -t "${device_id}" shell "${command}" >"${output_file}" 2>&1
  status=$?; set -e
  cat "${output_file}"; rm -f "${output_file}"
  [[ ${status} -eq 0 || ${status} -eq 139 ]]
}
require_remote_file() {
  local out; out="$(capture_hdc_shell "$1" "test -e '$2' && echo OK || echo MISSING:$2")"
  grep -q '^OK$' <<< "${out}" || { echo "${out}" >&2; exit 1; }
}
# toybox-safe process kill by PID (no awk, no pkill -f).
kill_ros2() {
  local dev="$1" pat="$2"
  capture_hdc_shell "${dev}" \
    "ps -ef | grep -E \"${pat}\" | grep -v grep | while read -r u pid rest; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null || true
}
kill_topic_clients() {  # kill all ros2 topic pub/echo on both boards (one lane at a time)
  kill_ros2 "${MDDS_DEVICE_ID}" 'topic pub|topic echo|ros2cli'
  kill_ros2 "${FASTDDS_DEVICE_ID}" 'topic pub|topic echo|ros2cli'
}
gateway_counts() {
  local mdds_topic="$1" out
  out="$(capture_hdc_shell "${FASTDDS_DEVICE_ID}" "cat '${GATEWAY_LOG}' 2>/dev/null || true" || true)"
  grep -E "stats mdds=${mdds_topic} " <<< "${out}" | tail -1 | grep -oE 'toDds=[0-9]+ toMdds=[0-9]+' || echo "toDds=? toMdds=?"
}
gateway_counter_value() {
  local mdds_topic="$1" counter="$2" counts value
  counts="$(gateway_counts "${mdds_topic}")"
  value="$(grep -oE "${counter}=[0-9]+" <<< "${counts}" | head -1 | cut -d= -f2 || true)"
  [[ "${value}" =~ ^[0-9]+$ ]] || value=0
  printf '%s' "${value}"
}
wait_gateway_counter() {
  local mdds_topic="$1" counter="$2" before="$3" deadline counts value
  deadline=$(( $(date +%s) + 20 ))
  while (( $(date +%s) < deadline )); do
    counts="$(gateway_counts "${mdds_topic}")"
    value="$(grep -oE "${counter}=[0-9]+" <<< "${counts}" | head -1 | cut -d= -f2 || true)"
    if [[ "${value}" =~ ^[0-9]+$ ]] && (( value > before )); then
      printf '%s' "${counts}"
      return 0
    fi
    sleep 1
  done
  gateway_counts "${mdds_topic}"
  return 1
}
msg_for_type() {
  case "$1" in
    std_msgs/msg/String)           printf '{data: %s}' "$2" ;;
    geometry_msgs/msg/PoseStamped) printf '{header: {frame_id: %s}, pose: {position: {x: 7.5, y: 8.5, z: 9.5}}}' "$2" ;;
    tf2_msgs/msg/TFMessage)        printf '{transforms: [{header: {frame_id: %s}, child_frame_id: mx_child, transform: {translation: {x: 1.5, y: 2.5, z: 3.5}, rotation: {x: 0.0, y: 0.0, z: 0.0, w: 1.0}}}]}' "$2" ;;
    *) echo "unsupported $1" >&2; return 1 ;;
  esac
}
cleanup() {
  kill_ros2 "${FASTDDS_DEVICE_ID}" 'mdds_dds_gateway'
  kill_topic_clients
}
trap cleanup EXIT

run_direction() {
  local lane="$1" dir="$2" sub_device="$3" sub_rmw="$4" sub_topic="$5" \
        pub_device="$6" pub_rmw="$7" pub_topic="$8" ros_type="$9" payload="${10}" mdds_topic="${11}"
  local tag="${lane}_${dir}"
  local echo_log="${LOG_DIR}/${tag}_echo.log" pub_log="${LOG_DIR}/${tag}_pub.log"
  local sub_env="" pub_env="" echo_once=" --once" sub_dom pub_dom
  [[ "${sub_rmw}" == "rmw_mdds_cpp" ]] && { sub_env=" ${BRIDGE_ENV}"; echo_once=""; sub_dom="${MDDS_DOMAIN}"; } || sub_dom="${DDS_DOMAIN}"
  [[ "${pub_rmw}" == "rmw_mdds_cpp" ]] && { pub_env=" ${BRIDGE_ENV}"; pub_dom="${MDDS_DOMAIN}"; } || pub_dom="${DDS_DOMAIN}"
  local msg; msg="$(msg_for_type "${ros_type}" "${payload}")"
  local expected_counter="toDds"
  [[ "${dir}" == "f2m" ]] && expected_counter="toMdds"
  local before before_value
  before="$(gateway_counts "${mdds_topic}")"
  before_value="$(gateway_counter_value "${mdds_topic}" "${expected_counter}")"

  kill_topic_clients; sleep 2
  capture_hdc_shell "${sub_device}" \
    "mkdir -p '${LOG_DIR}'; rm -f '${echo_log}'; nohup sh -c 'ROS_DOMAIN_ID=${sub_dom} RMW_IMPLEMENTATION=${sub_rmw}${sub_env} ${REMOTE_PREFIX}/bin/ros2 topic echo ${sub_topic} ${ros_type}${echo_once} --no-daemon > ${echo_log} 2>&1' >/dev/null 2>&1 & echo started" >/dev/null
  sleep 4
  capture_hdc_shell "${pub_device}" \
    "mkdir -p '${LOG_DIR}'; rm -f '${pub_log}'; nohup sh -c 'ROS_DOMAIN_ID=${pub_dom} RMW_IMPLEMENTATION=${pub_rmw}${pub_env} ${REMOTE_PREFIX}/bin/ros2 topic pub -r 5 -w 0 ${pub_topic} ${ros_type} '\\''${msg}'\\'' > ${pub_log} 2>&1' >/dev/null 2>&1 & echo started" >/dev/null

  local start_ts; start_ts="$(date +%s)" ; local result="FAIL"
  while (( $(date +%s) - start_ts < POLL_TIMEOUT_SECONDS )); do
    local out; out="$(capture_hdc_shell "${sub_device}" "cat '${echo_log}' 2>/dev/null || true" || true)"
    if grep -qF "${payload}" <<< "${out}"; then result="PASS"; break; fi
    sleep 2
  done
  local after counter_status="PASS"
  if [[ "${result}" == "PASS" ]]; then
    if ! after="$(wait_gateway_counter "${mdds_topic}" "${expected_counter}" "${before_value}")"; then
      result="FAIL"
      counter_status="FAIL:${expected_counter}_not_advanced"
    fi
  else
    after="$(gateway_counts "${mdds_topic}")"
  fi
  if [[ "${result}" == "PASS" ]]; then
    echo "RESULT|${tag}|PASS|${payload} | gw_expect=${expected_counter} gw_status=${counter_status} gw_before[${before}] gw_after[${after}]"
  else
    echo "RESULT|${tag}|FAIL|${payload} | gw_expect=${expected_counter} gw_status=${counter_status} gw_before[${before}] gw_after[${after}]"
  fi
  if [[ "${result}" == "FAIL" ]]; then
    echo "--- ${tag} echo log ---" >&2
    capture_hdc_shell "${sub_device}" "cat '${echo_log}' 2>/dev/null || true" >&2 || true
  fi
  kill_topic_clients
  [[ "${result}" == "PASS" ]]
}

# Preflight
require_remote_file "${MDDS_DEVICE_ID}" "${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so"
require_remote_file "${MDDS_DEVICE_ID}" "${BRIDGE_LIBRARY}"
require_remote_file "${FASTDDS_DEVICE_ID}" "${REMOTE_PREFIX}/lib/librmw_fastrtps_cpp.so"
require_remote_file "${FASTDDS_DEVICE_ID}" "${GATEWAY_BIN}"
require_remote_file "${FASTDDS_DEVICE_ID}" "${GATEWAY_CONFIG}"

# Clean slate on both boards (critical), then start gateway on the fastdds device.
kill_ros2 "${MDDS_DEVICE_ID}" '/bin/ros2|ros2cli|topic pub|topic echo'
kill_ros2 "${FASTDDS_DEVICE_ID}" '/bin/ros2|ros2cli|topic pub|topic echo|mdds_dds_gateway'
sleep 2
capture_hdc_shell "${FASTDDS_DEVICE_ID}" \
  "mkdir -p '${LOG_DIR}' && rm -f '${GATEWAY_LOG}'; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${GATEWAY_ENV} ${GATEWAY_BIN} ${GATEWAY_CONFIG} > ${GATEWAY_LOG} 2>&1' >/dev/null 2>&1 &" >/dev/null
gw_start="$(date +%s)"
while (( $(date +%s) - gw_start < 60 )); do
  out="$(capture_hdc_shell "${FASTDDS_DEVICE_ID}" "cat '${GATEWAY_LOG}' 2>/dev/null || true" || true)"
  grep -q "gateway started" <<< "${out}" && break
  sleep 1
done

PASS=0; FAIL=0
# spec: name|ros_type|mdds_node_topic|dds_node_topic|mdds_gw_topic
LANES=(
  "pubsub_string|std_msgs/msg/String|/rt/mx_chatter|/mx_chatter|rt/mx_chatter"
  "complex_pose|geometry_msgs/msg/PoseStamped|/rt/mx_pose|/mx_pose|rt/mx_pose"
  "qos_best_effort|std_msgs/msg/String|/rt/mx_be|/mx_be|rt/mx_be"
  "tf2_message|tf2_msgs/msg/TFMessage|/rt/mx_tf|/mx_tf|rt/mx_tf"
)
ts="$(date +%s)"
for spec in "${LANES[@]}"; do
  IFS='|' read -r name rtype mtopic dtopic gwtopic <<< "${spec}"
  if run_direction "${name}" "m2f" \
      "${FASTDDS_DEVICE_ID}" "rmw_fastrtps_cpp" "${dtopic}" \
      "${MDDS_DEVICE_ID}" "rmw_mdds_cpp" "${mtopic}" \
      "${rtype}" "${name}_m2f_${DDS_DOMAIN}_${ts}" "${gwtopic}"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
  if run_direction "${name}" "f2m" \
      "${MDDS_DEVICE_ID}" "rmw_mdds_cpp" "${mtopic}" \
      "${FASTDDS_DEVICE_ID}" "rmw_fastrtps_cpp" "${dtopic}" \
      "${rtype}" "${name}_f2m_${DDS_DOMAIN}_${ts}" "${gwtopic}"; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); fi
done
echo "MATRIX_SUMMARY pass=${PASS} fail=${FAIL}"
if [[ "${FAIL}" -ne 0 ]]; then
  exit 1
fi
echo "cross_board_rmw_mdds_matrix_ok"
