#!/usr/bin/env bash
set -uo pipefail

# rmw_mdds coverage round 2 — closes the testable gaps from round 1:
#   - larger payloads (512 KiB / 1 MiB / 1.5 MiB) toward the RELIABLE 2 MiB ceiling
#   - TRANSIENT_LOCAL retained-history replay to a TRUE late joiner (timing-isolated)
#   - LIVELINESS QoS (CLI-exposed; deadline/lifespan are not, would need a node)
#   - rosbag2 record + play over rmw_mdds (also exercises the serialized path)
#
# Homogeneous rmw_mdds<->rmw_mdds (board A pub -> DSoftBus -> board B echo) for the
# cross-board lanes; the rosbag2 lane is single-board (board A) record then play.

usage() { echo "Usage: $0 <mdds-device-A> <mdds-device-B> [domain]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }
A="$1"; B="$2"; DOM="${3:-85}"
[[ "$A" != "$B" ]] || { echo "ERROR: devices must differ" >&2; exit 2; }
HDC="${HDC_BIN:-hdc}"
PFX=/data/local/tmp/ohos-colcon-rk3588a
BR="${RMW_MDDS_BRIDGE_LIBRARY:-${PFX}/lib/libmdds_bridge_shared.z.so}"
LOG=/data/local/tmp/coverage2
LDP="${PFX}/lib:/data/local/tmp/ohos-prefix/lib:/data/local/tmp/ohos-fastdds/lib:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64"
MDDS="LD_LIBRARY_PATH=${LDP} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR}"

cap() { timeout 90s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
kill_all() {
  for d in "$A" "$B"; do
    cap "$d" "ps -ef | grep -E 'topic pub|topic echo|ros2cli|rmw_mdds_broker|bag record|bag play|rosbag2' | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
  done
}
trap kill_all EXIT
PASS=0; FAIL=0

HELPER="$(mktemp)"
cat > "$HELPER" <<'BIGPUB'
#!/system/bin/sh
D="COVSTART_$(head -c "$1" /dev/zero | tr '\0' X)COVEND"
exec /data/local/tmp/ohos-colcon-rk3588a/bin/ros2 topic pub --times "$3" -r "$4" -w 0 "$2" std_msgs/msg/String "{data: $D}"
BIGPUB
"${HDC}" -t "$A" file send "$HELPER" /data/local/tmp/cov_bigpub.sh >/dev/null 2>&1
rm -f "$HELPER"

# ---- large-payload lanes (homogeneous cross-board) ----
big_lane() {
  local name="$1" sz="$2" times="$3"
  kill_all; cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/${name}_*.log; true" >/dev/null
  cap "$B" "mkdir -p ${LOG}; rm -f ${LOG}/${name}_*.log; true" >/dev/null; sleep 2
  cap "$B" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic echo /cov_${name} std_msgs/msg/String --no-daemon > ${LOG}/${name}_e.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
  sleep 14
  cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} sh /data/local/tmp/cov_bigpub.sh ${sz} /cov_${name} ${times} 1 > ${LOG}/${name}_p.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
  sleep 22
  local rx em; rx="$(cap "$B" "grep -c '^---' ${LOG}/${name}_e.log 2>/dev/null" | tr -d '[:space:]')"
  em="$(cap "$B" "grep -c 'COVEND' ${LOG}/${name}_e.log 2>/dev/null" | tr -d '[:space:]')"
  if [[ "${rx:-0}" =~ ^[0-9]+$ && "${rx:-0}" -gt 0 && "${em:-0}" -gt 0 ]]; then
    echo "RESULT|cov2_${name}|PASS|received=${rx}|COVEND×${em}"; PASS=$((PASS+1))
  else
    echo "RESULT|cov2_${name}|FAIL|received=${rx:-0}|COVEND×${em:-0} (payload truncated or over arg/RELIABLE limit)"; FAIL=$((FAIL+1))
    cap "$A" "tail -3 ${LOG}/${name}_p.log 2>/dev/null"
  fi
}
big_lane large512k 524288 4
big_lane large1m   1048576 3
big_lane large1500k 1572864 2

# ---- TRANSIENT_LOCAL retained replay to a TRUE late joiner ----
# Publisher (transient_local, kept alive) publishes ONE sample at t~0 then is
# silent until t~30s (-r 0.033). The subscriber joins LATE (t~12s) and we check
# at t~20s — well before the next real publish — so any sample it has can ONLY be
# the retained history. received>0 => retention works.
kill_all; cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/tl_*.log; true" >/dev/null
cap "$B" "mkdir -p ${LOG}; rm -f ${LOG}/tl_*.log; true" >/dev/null; sleep 2
TLQ="--qos-durability transient_local --qos-reliability reliable --qos-history keep_last --qos-depth 5"
cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic pub ${TLQ} -r 0.033 -w 0 /cov_tlreplay std_msgs/msg/String \"{data: TLRETAIN}\" > ${LOG}/tl_p.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
echo "--- transient_local publisher up; waiting 12s before the LATE subscriber joins ---"
sleep 12
cap "$B" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic echo ${TLQ} /cov_tlreplay std_msgs/msg/String --no-daemon > ${LOG}/tl_e.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
sleep 9   # check at pub+~21s, before the next real publish at pub+30s
TLRX="$(cap "$B" "grep -c TLRETAIN ${LOG}/tl_e.log 2>/dev/null" | tr -d '[:space:]')"
if [[ "${TLRX:-0}" =~ ^[0-9]+$ && "${TLRX:-0}" -gt 0 ]]; then
  echo "RESULT|cov2_transient_local_replay|PASS|late_joiner_got_retained=${TLRX}"; PASS=$((PASS+1))
else
  echo "RESULT|cov2_transient_local_replay|FAIL|late_joiner_got=${TLRX:-0} (no retained history replay)"; FAIL=$((FAIL+1))
fi

# ---- LIVELINESS QoS ----
kill_all; cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/lv_*.log; true" >/dev/null
cap "$B" "mkdir -p ${LOG}; rm -f ${LOG}/lv_*.log; true" >/dev/null; sleep 2
LVQ="--qos-liveliness automatic --qos-reliability reliable"
cap "$B" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic echo ${LVQ} /cov_lv std_msgs/msg/String --no-daemon > ${LOG}/lv_e.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
sleep 14
cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic pub ${LVQ} --times 20 -r 2 -w 0 /cov_lv std_msgs/msg/String \"{data: lv}\" > ${LOG}/lv_p.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
sleep 18
LVRX="$(cap "$B" "grep -c '^---' ${LOG}/lv_e.log 2>/dev/null" | tr -d '[:space:]')"
if [[ "${LVRX:-0}" =~ ^[0-9]+$ && "${LVRX:-0}" -gt 0 ]]; then
  echo "RESULT|cov2_qos_liveliness|PASS|received=${LVRX}"; PASS=$((PASS+1))
else
  echo "RESULT|cov2_qos_liveliness|FAIL|received=${LVRX:-0}"; FAIL=$((FAIL+1))
fi

# ---- rosbag2 record + play over rmw_mdds (single board A; also serialized path) ----
kill_all; cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/bag_*.log; rm -rf /data/local/tmp/cov_bag; true" >/dev/null; sleep 2
cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 bag record -o /data/local/tmp/cov_bag /cov_bagtopic > ${LOG}/bag_rec.log 2>&1' >/dev/null 2>&1 & echo r" >/dev/null
sleep 8
cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic pub --times 10 -r 5 -w 0 /cov_bagtopic std_msgs/msg/String \"{data: BAGDATA}\" > ${LOG}/bag_pub.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
sleep 8
cap "$A" "ps -ef | grep 'bag record' | grep -v grep | while read -r u pid r; do kill -2 \"\${pid}\" 2>/dev/null; done; sleep 2; ps -ef | grep -E 'bag record|topic pub' | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
sleep 2
REC="$(cap "$A" "ls /data/local/tmp/cov_bag/ 2>/dev/null | grep -cE 'metadata|\\.db3|\\.mcap'")"
# play phase: subscriber then bag play
cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic echo /cov_bagtopic std_msgs/msg/String --no-daemon > ${LOG}/bag_echo.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
sleep 6
cap "$A" "${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 bag play /data/local/tmp/cov_bag > ${LOG}/bag_play.log 2>&1; true" >/dev/null
sleep 4
BAGRX="$(cap "$A" "grep -c BAGDATA ${LOG}/bag_echo.log 2>/dev/null" | tr -d '[:space:]')"
if [[ "${REC:-0}" =~ ^[0-9]+$ && "${REC:-0}" -gt 0 && "${BAGRX:-0}" =~ ^[0-9]+$ && "${BAGRX:-0}" -gt 0 ]]; then
  echo "RESULT|cov2_rosbag2_record_play|PASS|recorded_files=${REC}|played_received=${BAGRX}"; PASS=$((PASS+1))
else
  echo "RESULT|cov2_rosbag2_record_play|FAIL|recorded_files=${REC:-0}|played_received=${BAGRX:-0}"; FAIL=$((FAIL+1))
  echo "--- bag record log ---"; cap "$A" "tail -4 ${LOG}/bag_rec.log 2>/dev/null"
  echo "--- bag play log ---";   cap "$A" "tail -4 ${LOG}/bag_play.log 2>/dev/null"
fi

echo "COVERAGE2_SUMMARY|pass=${PASS}|fail=${FAIL}"
