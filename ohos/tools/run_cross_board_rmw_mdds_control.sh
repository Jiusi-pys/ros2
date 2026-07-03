#!/usr/bin/env bash
set -euo pipefail

# "An external (non-OpenHarmony) device controls the OpenHarmony device" — the
# control direction of the project goal. Full chain, fastdds as the DDS rep:
#
#   other(rmw_fastrtps) -> ros2 -> DDS-UDP -> mdds_dds_gateway
#                       -> MDDS-DSoftBus -> rmw_mdds -> ros2 -> OpenHarmony
#
#   board A = OpenHarmony  : rmw_mdds node SUBSCRIBES /rt/mx_chatter   [controllable device]
#   board B = other device : rmw_fastrtps node PUBLISHES /mx_chatter   [controlling device]
#                            + mdds_dds_gateway bridging
#                              /mx_chatter (DDS) <-> rt/mx_chatter (MDDS, DSoftBus)
#
# Split ROS_DOMAIN_IDs (mdds = DDS_DOMAIN+1) so the rmw_mdds node and the
# rmw_fastrtps node cannot see each other directly — every command MUST traverse
# gateway -> MDDS -> DSoftBus. Proven by the gateway 'toMdds' counter and by the
# OpenHarmony node receiving commands that have no other route.
#
# Prereq: cross-board DSoftBus LNN is up (the gateway's MDDS bridge must discover
# the OpenHarmony node's bridge). After a board reboot, re-apply the eth1 IPs and
# restart softbus_server so the LNN re-forms (see run_cross_board_rmw_mdds_m2m.sh
# notes); confirm via the bridge 'MatchTriggerVisitor Endpoint matched' hilog.

usage() { echo "Usage: $0 <openharmony-mdds-device> <other-fastrtps+gateway-device> [dds-domain]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }
A="$1"; B="$2"; DDS_DOMAIN="${3:-101}"; MDDS_DOMAIN="$(( DDS_DOMAIN + 1 ))"
[[ "$A" != "$B" ]] || { echo "ERROR: the two devices must differ" >&2; exit 2; }
HDC="${HDC_BIN:-hdc}"
PFX=/data/local/tmp/ohos-colcon-rk3588a
BR=/data/local/tmp/libmdds_bridge_shared.z.so
GW=/data/local/tmp/mdds_dds_gateway
GWCFG=/data/local/tmp/gateway_rmw_mdds_matrix.yaml
GWENV=/data/local/tmp/device_gateway_env.sh
LOG=/data/local/tmp/control_demo
LDP="${PFX}/lib:/data/local/tmp/ohos-prefix/lib:/data/local/tmp/ohos-fastdds/lib:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64"
MDDS="LD_LIBRARY_PATH=${LDP} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR}"
# OpenHarmony node ROS topic / DDS-side ROS topic / gateway mddsTopicName, all
# from the validated matrix gateway config (gateway_rmw_mdds_matrix.yaml).
OH_TOPIC=/rt/mx_chatter
DDS_TOPIC=/mx_chatter
GW_MDDS_TOPIC=rt/mx_chatter

sh_cap() { timeout 60s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
kill_all() {
  sh_cap "$A" "ps -ef | grep -E '/bin/ros2|ros2cli|topic |rmw_mdds_broker' | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
  sh_cap "$B" "ps -ef | grep -E '/bin/ros2|ros2cli|topic |mdds_dds_gateway' | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
}
gw_tomdds() { sh_cap "$B" "grep -E 'stats mdds=${GW_MDDS_TOPIC} ' ${LOG}/gw.log 2>/dev/null | tail -1 | grep -oE 'toMdds=[0-9]+'" | grep -oE '[0-9]+' | tail -1; }
trap kill_all EXIT
kill_all
sh_cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/*.log; true" >/dev/null
sh_cap "$B" "mkdir -p ${LOG}; rm -f ${LOG}/*.log; true" >/dev/null
sleep 2

# 1) gateway on the other device (DDS side = rmw_fastrtps, domain DDS_DOMAIN)
sh_cap "$B" "nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${GWENV} ${GW} ${GWCFG} > ${LOG}/gw.log 2>&1' >/dev/null 2>&1 & echo g" >/dev/null
for i in $(seq 1 30); do grep -q "gateway started" <<< "$(sh_cap "$B" "cat ${LOG}/gw.log 2>/dev/null")" && break; sleep 1; done
echo "--- gateway started on other device (board B) ---"

# 2) OpenHarmony controllable node: rmw_mdds SUBSCRIBER on the control topic
sh_cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${MDDS_DOMAIN} ${PFX}/bin/ros2 topic echo ${OH_TOPIC} std_msgs/msg/String --no-daemon > ${LOG}/oh.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
echo "--- OpenHarmony node subscribed (${OH_TOPIC}); waiting for cross-board discovery 18s ---"
sleep 18

BEFORE="$(gw_tomdds)"; BEFORE="${BEFORE:-0}"
# 3) external device issues control commands (rmw_fastrtps PUBLISHER, domain DDS_DOMAIN)
sh_cap "$B" "nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${PFX}/bin/ros2 topic pub --times 30 -r 2 -w 0 ${DDS_TOPIC} std_msgs/msg/String \"{data: CMD_ENGAGE_OPENHARMONY}\" > ${LOG}/cmd.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
echo "--- other device publishing control commands (${DDS_TOPIC}); waiting 22s ---"
sleep 22

RX="$(sh_cap "$A" "grep -c CMD_ENGAGE_OPENHARMONY ${LOG}/oh.log 2>/dev/null")"
AFTER="$(gw_tomdds)"; AFTER="${AFTER:-0}"
echo "--- OpenHarmony node received: ${RX:-0} command(s); gateway toMdds ${BEFORE} -> ${AFTER} ---"
if [[ "${RX:-0}" =~ ^[0-9]+$ && "${RX:-0}" -gt 0 && "${AFTER}" -gt "${BEFORE}" ]]; then
  echo "RESULT|other_controls_openharmony|PASS|received=${RX}|toMdds_delta=$((AFTER-BEFORE))"
else
  echo "RESULT|other_controls_openharmony|FAIL|received=${RX:-0}|toMdds ${BEFORE}->${AFTER}"
  echo "--- gateway log tail ---"; sh_cap "$B" "grep -E 'stats mdds=${GW_MDDS_TOPIC}|service bridge|gateway started' ${LOG}/gw.log 2>/dev/null | tail -6"
  echo "--- OpenHarmony echo tail ---"; sh_cap "$A" "tail -4 ${LOG}/oh.log 2>/dev/null"
fi

# ---- Reverse lane: OpenHarmony -> other (data out), proves the bidirectional <-> ----
gw_todds() { sh_cap "$B" "grep -E 'stats mdds=${GW_MDDS_TOPIC} ' ${LOG}/gw.log 2>/dev/null | tail -1 | grep -oE 'toDds=[0-9]+'" | grep -oE '[0-9]+' | tail -1; }
sh_cap "$A" "ps -ef | grep -E 'topic ' | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
sh_cap "$B" "ps -ef | grep -E 'topic pub|topic echo' | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
sleep 2
# other device subscribes (DDS side); OpenHarmony then publishes sensor data OUT
sh_cap "$B" "nohup sh -c 'ROS_DOMAIN_ID=${DDS_DOMAIN} RMW_IMPLEMENTATION=rmw_fastrtps_cpp ${PFX}/bin/ros2 topic echo ${DDS_TOPIC} std_msgs/msg/String --no-daemon > ${LOG}/other.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
sleep 16
RBEFORE="$(gw_todds)"; RBEFORE="${RBEFORE:-0}"
sh_cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${MDDS_DOMAIN} ${PFX}/bin/ros2 topic pub --times 30 -r 2 -w 0 ${OH_TOPIC} std_msgs/msg/String \"{data: OH_SENSOR_DATA}\" > ${LOG}/oh_pub.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
echo "--- OpenHarmony publishing sensor data OUT (${OH_TOPIC}); waiting 22s ---"
sleep 22
ROX="$(sh_cap "$B" "grep -c OH_SENSOR_DATA ${LOG}/other.log 2>/dev/null")"
RAFTER="$(gw_todds)"; RAFTER="${RAFTER:-0}"
echo "--- other device received: ${ROX:-0} message(s); gateway toDds ${RBEFORE} -> ${RAFTER} ---"
if [[ "${ROX:-0}" =~ ^[0-9]+$ && "${ROX:-0}" -gt 0 && "${RAFTER}" -gt "${RBEFORE}" ]]; then
  echo "RESULT|openharmony_to_other|PASS|received=${ROX}|toDds_delta=$((RAFTER-RBEFORE))"
else
  echo "RESULT|openharmony_to_other|FAIL|received=${ROX:-0}|toDds ${RBEFORE}->${RAFTER}"
fi
