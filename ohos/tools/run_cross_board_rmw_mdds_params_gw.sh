#!/usr/bin/env bash
set -euo pipefail

# Cross-board parameter lane: an rmw_mdds client runs `ros2 param set/get` against
# a stock rmw_fastrtps parameter_blackboard node through the mdds_dds_gateway,
# which bridges the 6 rcl_interfaces parameter services (client-side bridges).
#   A (mdds, domain MDDS_DOMAIN): ros2 param set/get
#   B (fastdds, domain DDS_DOMAIN): parameter_blackboard + mdds_dds_gateway

usage() { echo "Usage: $0 <mdds-device-id> <fastdds-device-id> [dds-domain]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }
A="$1"; B="$2"; DDS_DOMAIN="${3:-101}"
[[ "${DDS_DOMAIN}" =~ ^[0-9]+$ ]] && (( DDS_DOMAIN <= 231 )) || { echo "dds domain 0..231" >&2; exit 2; }
MDDS_DOMAIN="${MDDS_DOMAIN:-$(( DDS_DOMAIN + 1 ))}"
[[ "${MDDS_DOMAIN}" =~ ^[0-9]+$ ]] && (( MDDS_DOMAIN <= 232 )) || { echo "mdds domain 0..232" >&2; exit 2; }
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HDC="${HDC_BIN:-hdc}"
PFX=/data/local/tmp/ohos-colcon-rk3588a
BR="${RMW_MDDS_BRIDGE_LIBRARY:-${PFX}/lib/libmdds_bridge_shared.z.so}"
GWENV=/data/local/tmp/device_gateway_env.sh
GW=/data/local/tmp/mdds_dds_gateway
CFG=/data/local/tmp/gateway_rmw_mdds_params.yaml
CFG_TEMPLATE="${MDDS_GATEWAY_CONFIG_TEMPLATE:-${ROOT_DIR}/ohos/tools/gateway_rmw_mdds_params.yaml}"
GATEWAY_DOMAIN_RENDERER="${ROOT_DIR}/ohos/tools/render_rmw_mdds_gateway_config.py"
HDC_SEND_VERIFY="${ROOT_DIR}/ohos/tools/hdc_send_verify.sh"
NODE=parameter_blackboard
LOG=/data/local/tmp/params_gw
P=rcl_interfaces/srv
LOCAL_RENDERED_GATEWAY_CONFIG=""

sh_cap() { timeout 60s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
gateway_name() {
  local name="$1"
  if (( MDDS_DOMAIN == 0 )); then printf '%s' "${name}"; else printf 'd%s/%s' "${MDDS_DOMAIN}" "${name}"; fi
}
NODE_SYNC_TOPIC="$(gateway_name mdds_node_sync)"
SERVICES="$(gateway_name "${NODE}/get_parameters"):/${NODE}/get_parameters:${P}/GetParameters;$(gateway_name "${NODE}/set_parameters"):/${NODE}/set_parameters:${P}/SetParameters;$(gateway_name "${NODE}/list_parameters"):/${NODE}/list_parameters:${P}/ListParameters;$(gateway_name "${NODE}/describe_parameters"):/${NODE}/describe_parameters:${P}/DescribeParameters;$(gateway_name "${NODE}/get_parameter_types"):/${NODE}/get_parameter_types:${P}/GetParameterTypes;$(gateway_name "${NODE}/set_parameters_atomically"):/${NODE}/set_parameters_atomically:${P}/SetParametersAtomically"
kill_all() {
  local pat='/bin/ros2|ros2cli|param set|param get|parameter_blackboard|mdds_dds_gateway|rmw_mdds_broker'
  for d in "$A" "$B"; do
    sh_cap "$d" "ps -ef | grep -E \"${pat}\" | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
  done
}
cleanup() {
  kill_all
  if [[ -n "${LOCAL_RENDERED_GATEWAY_CONFIG}" ]]; then rm -f "${LOCAL_RENDERED_GATEWAY_CONFIG}"; fi
}
trap cleanup EXIT

[[ -f "${CFG_TEMPLATE}" ]] || { echo "missing gateway config template: ${CFG_TEMPLATE}" >&2; exit 1; }
[[ -f "${GATEWAY_DOMAIN_RENDERER}" ]] || { echo "missing gateway domain renderer: ${GATEWAY_DOMAIN_RENDERER}" >&2; exit 1; }
[[ -x "${HDC_SEND_VERIFY}" ]] || { echo "missing HDC verified-send helper: ${HDC_SEND_VERIFY}" >&2; exit 1; }
LOCAL_RENDERED_GATEWAY_CONFIG="$(mktemp /tmp/rmw_mdds_gateway_params.XXXXXX.yaml)"
python3 "${GATEWAY_DOMAIN_RENDERER}" --template "${CFG_TEMPLATE}" \
  --output "${LOCAL_RENDERED_GATEWAY_CONFIG}" --domain "${MDDS_DOMAIN}"
OHOS_HDC_BIN="${HDC}" "${HDC_SEND_VERIFY}" "$B" "${LOCAL_RENDERED_GATEWAY_CONFIG}" "${CFG}" >/dev/null
echo "RESULT|gateway_domain_config|PASS|lane=params|mdds_domain=${MDDS_DOMAIN}"
kill_all; sleep 2

# 1) parameter_blackboard on B (allow undeclared params)
sh_cap "$B" "mkdir -p ${LOG}; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${GWENV} ${PFX}/bin/ros2 run demo_nodes_cpp parameter_blackboard > ${LOG}/node.log 2>&1' >/dev/null 2>&1 & echo n" >/dev/null
# 2) gateway on B (param services via env)
sh_cap "$B" "rm -f ${LOG}/gw.log; old=\$(pidof mdds_dds_gateway); [ -z \"\$old\" ]||kill -9 \$old; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp RMW_MDDS_NODE_SYNC_TOPIC=${NODE_SYNC_TOPIC} MDDS_GATEWAY_SERVICES=\"${SERVICES}\" ${GWENV} ${GW} ${CFG} > ${LOG}/gw.log 2>&1' >/dev/null 2>&1 & echo g" >/dev/null
for i in $(seq 1 30); do grep -q "gateway started" <<< "$(sh_cap "$B" "cat ${LOG}/gw.log 2>/dev/null")" && break; sleep 1; done
echo "--- gateway services ---"; sh_cap "$B" "grep -c 'service bridge ready' ${LOG}/gw.log 2>/dev/null"

mdds="ROS_DOMAIN_ID=${MDDS_DOMAIN} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR} RMW_MDDS_NODE_SYNC_TOPIC=${NODE_SYNC_TOPIC} RMW_MDDS_BROKER_SOCKET=${LOG}/broker.sock RMW_MDDS_BROKER_LOG=${LOG}/broker.log RMW_MDDS_GRAPH_DEBUG=1"
# Keep a persistent rmw_mdds process alive so the dedicated embedded broker
# stays up with the bridge enabled and its
# node-sync subscriber has time to discover the gateway and accumulate the remote
# node list. Short-lived `ros2 param` CLIs then connect to this warm broker.
sh_cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/broker.sock ${LOG}/broker.log; nohup sh -c '${mdds} ${PFX}/bin/ros2 topic echo /mdds_keepalive std_msgs/msg/String --no-daemon > ${LOG}/keepalive.log 2>&1' >/dev/null 2>&1 & echo k" >/dev/null
sleep 18  # let the keeper's broker discover the gateway node-sync publisher + accumulate
# Use the real `ros2 param` CLI, which resolves the node in the graph first
# (wait_for_node) — exercises cross-board node discovery (gateway -> broker
# node-sync) on top of the parameter services.
echo "--- ros2 node list (should show ${NODE}) ---"
sh_cap "$A" "${mdds} ${PFX}/bin/ros2 node list --no-daemon" | head -8
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
