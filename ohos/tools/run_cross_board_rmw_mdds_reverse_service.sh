#!/usr/bin/env bash
set -euo pipefail

# Reverse service control: a non-OpenHarmony device CALLS A SERVICE that the
# OpenHarmony device serves (fastrtps client -> gateway -> rmw_mdds server).
#
#   board A = OpenHarmony  : rmw_mdds add_two_ints_server   [service provider]
#   board B = other device : rmw_fastrtps `ros2 service call`  [service caller]
#                            + mdds_dds_gateway with a DDS-side server bridge
#                              (MDDS_GATEWAY_SERVER_SERVICES) that forwards the
#                              request to the rmw_mdds server over MDDS/DSoftBus
#                              and returns the reply as the DDS response.
#
# Split ROS_DOMAIN_ID (mdds = DDS_DOMAIN+1) so the fastrtps client and the
# rmw_mdds server have NO direct route — the RPC can only complete via
# gateway -> MDDS -> DSoftBus. PASS iff the fastrtps client gets sum=42.

usage() { echo "Usage: $0 <openharmony-mdds-device> <other-fastrtps+gateway-device> [dds-domain]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }
A="$1"; B="$2"; DDS_DOMAIN="${3:-101}"; MDDS_DOMAIN="$(( DDS_DOMAIN + 1 ))"
[[ "$A" != "$B" ]] || { echo "ERROR: the two devices must differ" >&2; exit 2; }
HDC="${HDC_BIN:-hdc}"
PFX=/data/local/tmp/ohos-colcon-rk3588a
BR="${RMW_MDDS_BRIDGE_LIBRARY:-${PFX}/lib/libmdds_bridge_shared.z.so}"
GW=/data/local/tmp/mdds_dds_gateway
GWCFG=/data/local/tmp/gateway_rmw_mdds_matrix.yaml   # any valid topic config; gateway also reads the env services
GWENV=/data/local/tmp/device_gateway_env.sh
LOG=/data/local/tmp/reverse_svc
LDP="${PFX}/lib:/data/local/tmp/ohos-prefix/lib:/data/local/tmp/ohos-fastdds/lib:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64"
MDDS="LD_LIBRARY_PATH=${LDP} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR}"
NODE=add_two_ints_server
DDS_SVC=/add_two_ints
MDDS_SVC=add_two_ints
SRVTYPE=example_interfaces/srv/AddTwoInts
# DDS-side server bridge: dds=/add_two_ints  ->  mdds rq/add_two_ints,rr/add_two_ints
SERVER_SERVICES="${MDDS_SVC}:${DDS_SVC}:${SRVTYPE}"

sh_cap() { timeout 60s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
kill_all() {
  sh_cap "$A" "ps -ef | grep -E '/bin/ros2|ros2cli|add_two|service call|rmw_mdds_broker' | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
  sh_cap "$B" "ps -ef | grep -E '/bin/ros2|ros2cli|service call|mdds_dds_gateway' | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
}
trap kill_all EXIT
kill_all
sh_cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/*.log; true" >/dev/null
sh_cap "$B" "mkdir -p ${LOG}; rm -f ${LOG}/*.log; true" >/dev/null
sleep 2

# 1) OpenHarmony service provider: rmw_mdds add_two_ints_server
sh_cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${MDDS_DOMAIN} ${PFX}/bin/ros2 run demo_nodes_cpp ${NODE} > ${LOG}/srv.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
echo "--- OpenHarmony rmw_mdds ${NODE} started (domain ${MDDS_DOMAIN}) ---"

# 2) gateway on the other device with the DDS->MDDS server bridge
sh_cap "$B" "nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp MDDS_GATEWAY_SERVER_SERVICES=\"${SERVER_SERVICES}\" ${GWENV} ${GW} ${GWCFG} > ${LOG}/gw.log 2>&1' >/dev/null 2>&1 & echo g" >/dev/null
for i in $(seq 1 40); do grep -q "gateway started" <<< "$(sh_cap "$B" "cat ${LOG}/gw.log 2>/dev/null")" && break; sleep 1; done
echo "--- gateway started; server bridge: $(sh_cap "$B" "grep -c 'server service bridge ready' ${LOG}/gw.log 2>/dev/null") ---"
echo "--- waiting for cross-board discovery (gateway mdds-client <-> OpenHarmony mdds-server) 18s ---"
sleep 18

# 3) the other device calls the service via rmw_fastrtps
sh_cap "$B" "nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${PFX}/bin/ros2 service call ${DDS_SVC} ${SRVTYPE} \"{a: 41, b: 1}\" > ${LOG}/call.log 2>&1' >/dev/null 2>&1 & echo c" >/dev/null
echo "--- other device (rmw_fastrtps) calling ${DDS_SVC} ; waiting 18s ---"
sleep 18

SUM="$(sh_cap "$B" "grep -c 'sum=42' ${LOG}/call.log 2>/dev/null")"
echo "--- gateway server-bridge stats ---"; sh_cap "$B" "grep -E 'server service bridge|stats service' ${LOG}/gw.log 2>/dev/null | tail -4"
if [[ "${SUM:-0}" =~ ^[0-9]+$ && "${SUM:-0}" -gt 0 ]]; then
  echo "RESULT|other_calls_openharmony_service|PASS|sum=42"
else
  echo "RESULT|other_calls_openharmony_service|FAIL"
  echo "--- caller call.log ---"; sh_cap "$B" "tail -6 ${LOG}/call.log 2>/dev/null"
  echo "--- OpenHarmony server srv.log ---"; sh_cap "$A" "tail -4 ${LOG}/srv.log 2>/dev/null"
  echo "--- gateway log tail ---"; sh_cap "$B" "tail -8 ${LOG}/gw.log 2>/dev/null"
fi
