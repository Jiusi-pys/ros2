#!/usr/bin/env bash
set -euo pipefail

# Cross-board service (RPC) bridge smoke: an rmw_mdds CLIENT calls AddTwoInts; a
# stock rmw_fastrtps SERVER answers it; the mdds_dds_gateway service bridge
# translates the rmw_mdds rq//rr/ envelope to a real typed rclcpp service call.
#
#   A (mdds device,  domain MDDS_DOMAIN): rmw_mdds_cpp `ros2 service call`
#   B (fastdds dev,  domain DDS_DOMAIN) : add_two_ints_server (rmw_fastrtps)
#                                         + mdds_dds_gateway (MDDS_GATEWAY_SERVICES)
#
# Split domains keep rmw_mdds' RTPS leg from reaching fastrtps directly, so the
# call can only complete via gateway->MDDS->DSoftBus.

usage() { echo "Usage: $0 <mdds-device-id> <fastdds-device-id> [dds-domain]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }

A="$1"; B="$2"; DDS_DOMAIN="${3:-101}"; MDDS_DOMAIN="$(( DDS_DOMAIN + 1 ))"
HDC="${HDC_BIN:-hdc}"
PFX=/data/local/tmp/ohos-colcon-rk3588a
BR="${RMW_MDDS_BRIDGE_LIBRARY:-${PFX}/lib/libmdds_bridge_shared.z.so}"
GWENV=/data/local/tmp/device_gateway_env.sh
GW=/data/local/tmp/mdds_dds_gateway
CFG=/data/local/tmp/gateway_rmw_mdds_matrix.yaml
SVC=/add_two_ints
SRVTYPE=example_interfaces/srv/AddTwoInts
LOG=/data/local/tmp/svc_gw
A_VAL=41
B_VAL=1  # expect sum 42

sh_cap() { timeout 45s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
gateway_service_stat() {
  local field="$1" line value
  line="$(sh_cap "$B" "grep 'stats service mdds=add_two_ints' ${LOG}/gw.log 2>/dev/null | tail -1")"
  value="$(grep -oE "${field}=[0-9]+" <<< "${line}" | head -1 | cut -d= -f2 || true)"
  [[ "${value}" =~ ^[0-9]+$ ]] || value=0
  printf '%s' "${value}"
}
wait_gateway_service_stats() {
  local before_requests="$1" before_replies="$2" deadline line requests replies
  deadline=$(( $(date +%s) + 20 ))
  while (( $(date +%s) < deadline )); do
    line="$(sh_cap "$B" "grep 'stats service mdds=add_two_ints' ${LOG}/gw.log 2>/dev/null | tail -1")"
    requests="$(grep -oE 'requests=[0-9]+' <<< "${line}" | head -1 | cut -d= -f2 || true)"
    replies="$(grep -oE 'replies=[0-9]+' <<< "${line}" | head -1 | cut -d= -f2 || true)"
    [[ "${requests}" =~ ^[0-9]+$ ]] || requests=0
    [[ "${replies}" =~ ^[0-9]+$ ]] || replies=0
    if (( requests > before_requests && replies > before_replies )); then
      printf '%s' "${line}"
      return 0
    fi
    sleep 1
  done
  sh_cap "$B" "grep 'stats service mdds=add_two_ints' ${LOG}/gw.log 2>/dev/null | tail -1"
  return 1
}
kill_all() {
  local pat='/bin/ros2|ros2cli|topic pub|topic echo|service call|add_two_ints_server|mdds_dds_gateway|rmw_mdds_broker'
  for d in "$A" "$B"; do
    sh_cap "$d" "ps -ef | grep -E \"${pat}\" | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
  done
}
trap kill_all EXIT

kill_all; sleep 2

# 1) fastrtps AddTwoInts server on B (domain DDS_DOMAIN)
sh_cap "$B" "mkdir -p ${LOG}; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${GWENV} ${PFX}/bin/ros2 run demo_nodes_cpp add_two_ints_server > ${LOG}/server.log 2>&1' >/dev/null 2>&1 & echo srv" >/dev/null

# 2) gateway on B with the service bridge (pub/sub config still required to boot)
sh_cap "$B" "rm -f ${LOG}/gw.log; old=\$(pidof mdds_dds_gateway); [ -z \"\$old\" ]||kill -9 \$old; nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp MDDS_GATEWAY_SERVICES=add_two_ints:${SVC}:${SRVTYPE} ${GWENV} ${GW} ${CFG} > ${LOG}/gw.log 2>&1' >/dev/null 2>&1 & echo gw" >/dev/null

# wait for gateway service bridge ready
for i in $(seq 1 30); do
  out="$(sh_cap "$B" "cat ${LOG}/gw.log 2>/dev/null")"
  grep -q "service bridge ready" <<< "$out" && break
  sleep 1
done

# 3) rmw_mdds client call on A (domain MDDS_DOMAIN). Give discovery a moment.
sleep 8
before_requests="$(gateway_service_stat requests)"
before_replies="$(gateway_service_stat replies)"
out="$(sh_cap "$A" "ROS_DOMAIN_ID=${MDDS_DOMAIN} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR} ${PFX}/bin/ros2 service call ${SVC} ${SRVTYPE} '{a: ${A_VAL}, b: ${B_VAL}}'")"
echo "--- client output ---"; echo "$out"
echo "--- gateway service stats ---"; sh_cap "$B" "grep 'stats service' ${LOG}/gw.log 2>/dev/null | tail -2"

if grep -qE "sum=42|sum: 42" <<< "$out" && stats="$(wait_gateway_service_stats "${before_requests}" "${before_replies}")"; then
  echo "RESULT|service_addints_mdds_client_to_fastrtps_server|PASS|sum=42 ${stats}"
else
  echo "RESULT|service_addints_mdds_client_to_fastrtps_server|FAIL"
  echo "--- gateway log tail ---"; sh_cap "$B" "tail -20 ${LOG}/gw.log 2>/dev/null"
  echo "--- server log tail ---"; sh_cap "$B" "tail -10 ${LOG}/server.log 2>/dev/null"
  exit 1
fi
