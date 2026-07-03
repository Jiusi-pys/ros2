#!/usr/bin/env bash
set -euo pipefail

# Cross-board lifecycle lane: an rmw_mdds client drives a stock rmw_fastrtps
# lifecycle node (lifecycle_talker -> node lc_talker) through the gateway, which
# bridges the lifecycle services. Validated via direct `ros2 service call` (by
# service name) to avoid the cross-board node-resolution that `ros2 lifecycle`
# requires (a separate graph-sync concern).

usage() { echo "Usage: $0 <mdds-device-id> <fastdds-device-id> [dds-domain]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }
A="$1"; B="$2"; DDS_DOMAIN="${3:-101}"; MDDS_DOMAIN="$(( DDS_DOMAIN + 1 ))"
HDC="${HDC_BIN:-hdc}"
PFX=/data/local/tmp/ohos-colcon-rk3588a
BR=/data/local/tmp/libmdds_bridge_shared.z.so
GWENV=/data/local/tmp/device_gateway_env.sh
GW=/data/local/tmp/mdds_dds_gateway
CFG=/data/local/tmp/gateway_rmw_mdds_lifecycle.yaml
NODE=lc_talker
LOG=/data/local/tmp/lifecycle_gw
L=lifecycle_msgs/srv
SERVICES="${NODE}/get_state:/${NODE}/get_state:${L}/GetState;${NODE}/change_state:/${NODE}/change_state:${L}/ChangeState;${NODE}/get_available_states:/${NODE}/get_available_states:${L}/GetAvailableStates;${NODE}/get_available_transitions:/${NODE}/get_available_transitions:${L}/GetAvailableTransitions"

sh_cap() { timeout 60s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
kill_all() {
  local pat='/bin/ros2|ros2cli|service call|lifecycle_talker|mdds_dds_gateway|rmw_mdds_broker'
  for d in "$A" "$B"; do
    sh_cap "$d" "ps -ef | grep -E \"${pat}\" | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
  done
}
trap kill_all EXIT
kill_all; sleep 2

sh_cap "$B" "mkdir -p ${LOG}; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${GWENV} ${PFX}/bin/ros2 run lifecycle lifecycle_talker > ${LOG}/node.log 2>&1' >/dev/null 2>&1 & echo n" >/dev/null
sh_cap "$B" "rm -f ${LOG}/gw.log; old=\$(pidof mdds_dds_gateway); [ -z \"\$old\" ]||kill -9 \$old; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp MDDS_GATEWAY_SERVICES=\"${SERVICES}\" ${GWENV} ${GW} ${CFG} > ${LOG}/gw.log 2>&1' >/dev/null 2>&1 & echo g" >/dev/null
for i in $(seq 1 30); do grep -q "gateway started" <<< "$(sh_cap "$B" "cat ${LOG}/gw.log 2>/dev/null")" && break; sleep 1; done
echo "--- gateway services: $(sh_cap "$B" "grep -c 'service bridge ready' ${LOG}/gw.log 2>/dev/null") ---"

sleep 10
mdds="ROS_DOMAIN_ID=${MDDS_DOMAIN} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR}"
echo "--- get_state (initial) ---"
s1="$(sh_cap "$A" "${mdds} ${PFX}/bin/ros2 service call /${NODE}/get_state ${L}/GetState '{}'")"
echo "$s1" | tail -5
echo "--- change_state CONFIGURE (transition id 1) ---"
c1="$(sh_cap "$A" "${mdds} ${PFX}/bin/ros2 service call /${NODE}/change_state ${L}/ChangeState '{transition: {id: 1}}'")"
echo "$c1" | tail -5
echo "--- get_state (after configure) ---"
s2="$(sh_cap "$A" "${mdds} ${PFX}/bin/ros2 service call /${NODE}/get_state ${L}/GetState '{}'")"
echo "$s2" | tail -5

if grep -qiE "success=True|inactive" <<< "${c1}${s2}" && grep -qiE "label=|current_state" <<< "${s1}"; then
  echo "RESULT|lifecycle_mdds_client_to_fastrtps_node|PASS"
else
  echo "RESULT|lifecycle_mdds_client_to_fastrtps_node|FAIL"
  echo "--- gateway stats ---"; sh_cap "$B" "grep -E 'stats service' ${LOG}/gw.log 2>/dev/null | tail -6"
  exit 1
fi
