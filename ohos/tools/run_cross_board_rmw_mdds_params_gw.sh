#!/usr/bin/env bash
set -euo pipefail

# Cross-board parameter lane: an rmw_mdds client runs `ros2 param set/get` against
# a stock rmw_fastrtps parameter_blackboard node through the mdds_dds_gateway,
# which bridges the 6 rcl_interfaces parameter services (client-side bridges).
#   A (mdds, domain MDDS_DOMAIN): ros2 param set/get
#   B (fastdds, domain DDS_DOMAIN): parameter_blackboard + mdds_dds_gateway

usage() { echo "Usage: $0 <mdds-device-id> <fastdds-device-id> [dds-domain]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }
A="$1"; B="$2"; DDS_DOMAIN="${3:-101}"; MDDS_DOMAIN="$(( DDS_DOMAIN + 1 ))"
HDC="${HDC_BIN:-hdc}"
PFX=/data/local/tmp/ohos-colcon-rk3588a
BR=/data/local/tmp/libmdds_bridge_shared.z.so
GWENV=/data/local/tmp/device_gateway_env.sh
GW=/data/local/tmp/mdds_dds_gateway
CFG=/data/local/tmp/gateway_rmw_mdds_params.yaml
NODE=parameter_blackboard
LOG=/data/local/tmp/params_gw
P=rcl_interfaces/srv
SERVICES="${NODE}/get_parameters:/${NODE}/get_parameters:${P}/GetParameters;${NODE}/set_parameters:/${NODE}/set_parameters:${P}/SetParameters;${NODE}/list_parameters:/${NODE}/list_parameters:${P}/ListParameters;${NODE}/describe_parameters:/${NODE}/describe_parameters:${P}/DescribeParameters;${NODE}/get_parameter_types:/${NODE}/get_parameter_types:${P}/GetParameterTypes;${NODE}/set_parameters_atomically:/${NODE}/set_parameters_atomically:${P}/SetParametersAtomically"

sh_cap() { timeout 60s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
kill_all() {
  local pat='/bin/ros2|ros2cli|param set|param get|parameter_blackboard|mdds_dds_gateway|rmw_mdds_broker'
  for d in "$A" "$B"; do
    sh_cap "$d" "ps -ef | grep -E \"${pat}\" | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
  done
}
trap kill_all EXIT
kill_all; sleep 2

# 1) parameter_blackboard on B (allow undeclared params)
sh_cap "$B" "mkdir -p ${LOG}; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${GWENV} ${PFX}/bin/ros2 run demo_nodes_cpp parameter_blackboard > ${LOG}/node.log 2>&1' >/dev/null 2>&1 & echo n" >/dev/null
# 2) gateway on B (param services via env)
sh_cap "$B" "rm -f ${LOG}/gw.log; old=\$(pidof mdds_dds_gateway); [ -z \"\$old\" ]||kill -9 \$old; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp MDDS_GATEWAY_SERVICES=\"${SERVICES}\" ${GWENV} ${GW} ${CFG} > ${LOG}/gw.log 2>&1' >/dev/null 2>&1 & echo g" >/dev/null
for i in $(seq 1 30); do grep -q "gateway started" <<< "$(sh_cap "$B" "cat ${LOG}/gw.log 2>/dev/null")" && break; sleep 1; done
echo "--- gateway services ---"; sh_cap "$B" "grep -c 'service bridge ready' ${LOG}/gw.log 2>/dev/null"

mdds="ROS_DOMAIN_ID=${MDDS_DOMAIN} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR}"
# Keep a persistent rmw_mdds process alive so the SHARED embedded broker (socket
# /data/local/tmp/rmw_mdds_cpp.sock) stays up with the bridge enabled and its
# node-sync subscriber has time to discover the gateway and accumulate the remote
# node list. Short-lived `ros2 param` CLIs then connect to this warm broker.
sh_cap "$A" "nohup sh -c '${mdds} ${PFX}/bin/ros2 topic echo /mdds_keepalive std_msgs/msg/String --no-daemon > ${LOG}/keepalive.log 2>&1' >/dev/null 2>&1 & echo k" >/dev/null
sleep 18  # let the keeper's broker discover the gateway node-sync publisher + accumulate
# Use the real `ros2 param` CLI, which resolves the node in the graph first
# (wait_for_node) — exercises cross-board node discovery (gateway -> broker
# node-sync) on top of the parameter services.
echo "--- ros2 node list (should show ${NODE}) ---"
sh_cap "$A" "${mdds} ${PFX}/bin/ros2 node list" | head -8
echo "--- ros2 param set (CLI, --timeout 10) ---"
sh_cap "$A" "${mdds} ${PFX}/bin/ros2 param set --timeout 10 /${NODE} mdds_test_param 4242" | tail -4
echo "--- ros2 param get (CLI) ---"
out="$(sh_cap "$A" "${mdds} ${PFX}/bin/ros2 param get --timeout 10 /${NODE} mdds_test_param")"
echo "$out" | tail -4

if grep -qE "4242" <<< "$out"; then
  echo "RESULT|params_cli_mdds_client_to_fastrtps_node|PASS|4242"
else
  echo "RESULT|params_cli_mdds_client_to_fastrtps_node|FAIL"
  echo "--- gateway log tail ---"; sh_cap "$B" "grep -E 'stats service|service bridge' ${LOG}/gw.log 2>/dev/null | tail -8"
  exit 1
fi
