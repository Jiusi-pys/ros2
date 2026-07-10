#!/usr/bin/env bash
set -euo pipefail

# Cross-board lifecycle lane: an rmw_mdds client drives a stock rmw_fastrtps
# lifecycle node (lifecycle_talker -> node lc_talker) through the gateway, which
# bridges the lifecycle services. Validated via direct `ros2 service call` (by
# service name) to avoid the cross-board node-resolution that `ros2 lifecycle`
# requires (a separate graph-sync concern).

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
CFG=/data/local/tmp/gateway_rmw_mdds_lifecycle.yaml
CFG_TEMPLATE="${MDDS_GATEWAY_CONFIG_TEMPLATE:-${ROOT_DIR}/ohos/tools/gateway_rmw_mdds_lifecycle.yaml}"
GATEWAY_DOMAIN_RENDERER="${ROOT_DIR}/ohos/tools/render_rmw_mdds_gateway_config.py"
HDC_SEND_VERIFY="${ROOT_DIR}/ohos/tools/hdc_send_verify.sh"
NODE=lc_talker
LOG=/data/local/tmp/lifecycle_gw
L=lifecycle_msgs/srv
LOCAL_RENDERED_GATEWAY_CONFIG=""

sh_cap() { timeout 60s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
gateway_name() {
  local name="$1"
  if (( MDDS_DOMAIN == 0 )); then printf '%s' "${name}"; else printf 'd%s/%s' "${MDDS_DOMAIN}" "${name}"; fi
}
SERVICES="$(gateway_name "${NODE}/get_state"):/${NODE}/get_state:${L}/GetState;$(gateway_name "${NODE}/change_state"):/${NODE}/change_state:${L}/ChangeState;$(gateway_name "${NODE}/get_available_states"):/${NODE}/get_available_states:${L}/GetAvailableStates;$(gateway_name "${NODE}/get_available_transitions"):/${NODE}/get_available_transitions:${L}/GetAvailableTransitions"
kill_all() {
  local pat='/bin/ros2|ros2cli|service call|lifecycle_talker|mdds_dds_gateway|rmw_mdds_broker'
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
LOCAL_RENDERED_GATEWAY_CONFIG="$(mktemp /tmp/rmw_mdds_gateway_lifecycle.XXXXXX.yaml)"
python3 "${GATEWAY_DOMAIN_RENDERER}" --template "${CFG_TEMPLATE}" \
  --output "${LOCAL_RENDERED_GATEWAY_CONFIG}" --domain "${MDDS_DOMAIN}"
OHOS_HDC_BIN="${HDC}" "${HDC_SEND_VERIFY}" "$B" "${LOCAL_RENDERED_GATEWAY_CONFIG}" "${CFG}" >/dev/null
echo "RESULT|gateway_domain_config|PASS|lane=lifecycle|mdds_domain=${MDDS_DOMAIN}"
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
