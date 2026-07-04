#!/usr/bin/env bash
set -euo pipefail

# Cross-board action smoke: an rmw_mdds action CLIENT drives a stock rmw_fastrtps
# Fibonacci action SERVER through the mdds_dds_gateway. An action is 3 services
# (send_goal/cancel_goal/get_result) + 2 topics (feedback/status); the gateway
# bridges the services (client-side IntrospectClientServiceBridge) and the
# feedback/status topics (TO_MDDS), all via the generic introspection codec.
#
#   A (mdds device,  domain MDDS_DOMAIN): rmw_mdds_cpp `ros2 action send_goal`
#   B (fastdds dev,  domain DDS_DOMAIN) : fibonacci_action_server (rmw_fastrtps)
#                                         + mdds_dds_gateway
# Split domains force the path through gateway->MDDS->DSoftBus.

usage() { echo "Usage: $0 <mdds-device-id> <fastdds-device-id> [dds-domain]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }

A="$1"; B="$2"; DDS_DOMAIN="${3:-101}"; MDDS_DOMAIN="$(( DDS_DOMAIN + 1 ))"
HDC="${HDC_BIN:-hdc}"
PFX=/data/local/tmp/ohos-colcon-rk3588a
BR="${RMW_MDDS_BRIDGE_LIBRARY:-${PFX}/lib/libmdds_bridge_shared.z.so}"
GWENV=/data/local/tmp/device_gateway_env.sh
GW=/data/local/tmp/mdds_dds_gateway
CFG=/data/local/tmp/gateway_rmw_mdds_action.yaml
ACT=/fibonacci
ACT_TYPE=action_tutorials_interfaces/action/Fibonacci
LOG=/data/local/tmp/action_gw
SERVICES="fibonacci/_action/send_goal:/fibonacci/_action/send_goal:action_tutorials_interfaces/action/Fibonacci_SendGoal;fibonacci/_action/cancel_goal:/fibonacci/_action/cancel_goal:action_msgs/srv/CancelGoal;fibonacci/_action/get_result:/fibonacci/_action/get_result:action_tutorials_interfaces/action/Fibonacci_GetResult"

sh_cap() { timeout 60s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
gateway_service_stat() {
  local name="$1" field="$2" line value
  line="$(sh_cap "$B" "grep 'stats service mdds=${name}' ${LOG}/gw.log 2>/dev/null | tail -1")"
  value="$(grep -oE "${field}=[0-9]+" <<< "${line}" | head -1 | cut -d= -f2 || true)"
  [[ "${value}" =~ ^[0-9]+$ ]] || value=0
  printf '%s' "${value}"
}
gateway_topic_stat() {
  local name="$1" field="$2" line value
  line="$(sh_cap "$B" "grep 'stats mdds=${name}' ${LOG}/gw.log 2>/dev/null | tail -1")"
  value="$(grep -oE "${field}=[0-9]+" <<< "${line}" | head -1 | cut -d= -f2 || true)"
  [[ "${value}" =~ ^[0-9]+$ ]] || value=0
  printf '%s' "${value}"
}
wait_gateway_counter() {
  local kind="$1" name="$2" field="$3" before="$4" deadline value
  deadline=$(( $(date +%s) + 25 ))
  while (( $(date +%s) < deadline )); do
    if [[ "${kind}" == "service" ]]; then
      value="$(gateway_service_stat "${name}" "${field}")"
    else
      value="$(gateway_topic_stat "${name}" "${field}")"
    fi
    if (( value > before )); then
      return 0
    fi
    sleep 1
  done
  return 1
}
wait_gateway_action_stats() {
  local sg_req_before="$1" sg_rep_before="$2" gr_req_before="$3" gr_rep_before="$4" feedback_before="$5" status_before="$6"
  wait_gateway_counter service "fibonacci/_action/send_goal" requests "${sg_req_before}" &&
    wait_gateway_counter service "fibonacci/_action/send_goal" replies "${sg_rep_before}" &&
    wait_gateway_counter service "fibonacci/_action/get_result" requests "${gr_req_before}" &&
    wait_gateway_counter service "fibonacci/_action/get_result" replies "${gr_rep_before}" &&
    wait_gateway_counter topic "fibonacci/_action/feedback" toMdds "${feedback_before}" &&
    wait_gateway_counter topic "fibonacci/_action/status" toMdds "${status_before}"
}
kill_all() {
  local pat='/bin/ros2|ros2cli|action send_goal|fibonacci_action_server|mdds_dds_gateway|rmw_mdds_broker'
  for d in "$A" "$B"; do
    sh_cap "$d" "ps -ef | grep -E \"${pat}\" | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
  done
}
trap kill_all EXIT

kill_all; sleep 2

# 1) fastrtps Fibonacci action server on B (domain DDS_DOMAIN)
sh_cap "$B" "mkdir -p ${LOG}; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${GWENV} ${PFX}/bin/ros2 run action_tutorials_cpp fibonacci_action_server > ${LOG}/server.log 2>&1' >/dev/null 2>&1 & echo srv" >/dev/null

# 2) gateway on B (services via env + feedback/status topics via config)
sh_cap "$B" "rm -f ${LOG}/gw.log; old=\$(pidof mdds_dds_gateway); [ -z \"\$old\" ]||kill -9 \$old; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp MDDS_GATEWAY_SERVICES=\"${SERVICES}\" ${GWENV} ${GW} ${CFG} > ${LOG}/gw.log 2>&1' >/dev/null 2>&1 & echo gw" >/dev/null

for i in $(seq 1 30); do
  grep -q "gateway started" <<< "$(sh_cap "$B" "cat ${LOG}/gw.log 2>/dev/null")" && break
  sleep 1
done
echo "--- gateway boot ---"; sh_cap "$B" "grep -E 'service bridge ready|mapping ready|gateway started' ${LOG}/gw.log 2>/dev/null"

# 3) rmw_mdds action client on A (domain MDDS_DOMAIN). Allow discovery + warmup.
sleep 10
sg_req_before="$(gateway_service_stat "fibonacci/_action/send_goal" requests)"
sg_rep_before="$(gateway_service_stat "fibonacci/_action/send_goal" replies)"
gr_req_before="$(gateway_service_stat "fibonacci/_action/get_result" requests)"
gr_rep_before="$(gateway_service_stat "fibonacci/_action/get_result" replies)"
feedback_before="$(gateway_topic_stat "fibonacci/_action/feedback" toMdds)"
status_before="$(gateway_topic_stat "fibonacci/_action/status" toMdds)"
out="$(sh_cap "$A" "ROS_DOMAIN_ID=${MDDS_DOMAIN} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR} ${PFX}/bin/ros2 action send_goal ${ACT} ${ACT_TYPE} '{order: 7}' --feedback")"
echo "--- action client output ---"; echo "$out"
echo "--- gateway service stats ---"; sh_cap "$B" "grep -E 'stats service|stats mdds=fibonacci' ${LOG}/gw.log 2>/dev/null | tail -6"

seq_raw="$(sed -n '/^Result:/,/Goal finished/p' <<< "${out}" | grep -oE '^[[:space:]]*- [0-9]+' | grep -oE '[0-9]+' || true)"
sequence="$(printf '%s' "${seq_raw}" | tr '\n' ',' | sed 's/,$//')"
if grep -qi "Goal accepted" <<< "$out" &&
    grep -q "Goal finished with status: SUCCEEDED" <<< "$out" &&
    [[ "${sequence}" == "0,1,1,2,3,5,8,13" ]] &&
    wait_gateway_action_stats "${sg_req_before}" "${sg_rep_before}" "${gr_req_before}" "${gr_rep_before}" \
      "${feedback_before}" "${status_before}"; then
  echo "--- gateway action stats after wait ---"
  sh_cap "$B" "grep -E 'stats service mdds=fibonacci/_action/(send_goal|get_result)|stats mdds=fibonacci/_action/(feedback|status)' ${LOG}/gw.log 2>/dev/null | tail -8"
  echo "RESULT|action_fibonacci_mdds_client_to_fastrtps_server|PASS|sequence=${sequence}"
else
  echo "RESULT|action_fibonacci_mdds_client_to_fastrtps_server|FAIL|sequence=${sequence:-none}"
  echo "--- gateway log tail ---"; sh_cap "$B" "tail -25 ${LOG}/gw.log 2>/dev/null"
  echo "--- server log tail ---"; sh_cap "$B" "tail -15 ${LOG}/server.log 2>/dev/null"
  exit 1
fi
