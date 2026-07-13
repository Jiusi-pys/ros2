#!/usr/bin/env bash
set -euo pipefail

# Dual-board rmw_mdds <-> rmw_mdds (no gateway) over DSoftBus.
#
# Both RK3588A boards run RMW_IMPLEMENTATION=rmw_mdds_cpp in broker mode; the two
# embedded brokers' bridges discover + match each other natively over DSoftBus
# (MDDS topic discovery), so traffic never touches any DDS/gateway. Validates the
# rmw_mdds<->rmw_mdds endpoint contract for pub/sub, service (RPC), and action
# (Fibonacci full protocol = 3 services + 2 topics).
#
#   A (mdds): topic pub  / service call / action send_goal   (client/publisher)
#   B (mdds): topic echo / add_two_ints_server / fibonacci_action_server
#
# --- Prerequisites (operational) -------------------------------------------
# * Both boards share an L2 subnet on eth1 (e.g. A=192.168.77.10, B=.11) and the
#   IPs are re-applied after any reboot (RK3588A eth IPs are not persistent).
# * DSoftBus must be LNN-networked (GetAllNodeDeviceInfo lists the peer). If a
#   board was rebooted, its softbus_server may have started before the IP was up;
#   `kill -9 $(pidof softbus_server)` on each board (samgr respawns it) so it
#   re-binds eth1 and re-forms the LNN. SuperDevice "other node is offline" logs
#   lag and are NOT authoritative -- the MDDS bridge "MatchTriggerVisitor
#   Endpoint matched" hilog line is.

usage() { echo "Usage: $0 <mdds-device-A> <mdds-device-B> [domain-id]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }
A="$1"; B="$2"; DOM="${3:-93}"
# This harness validates CROSS-board DSoftBus traffic; the same id twice would
# run both ends on one board (intra-host) and PASS without exercising the
# cross-device path at all, so reject it.
[[ "$A" != "$B" ]] || { echo "ERROR: device A and B must be distinct boards (got '$A' twice)" >&2; exit 2; }
HDC="${HDC_BIN:-hdc}"
PFX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
BR="${RMW_MDDS_BRIDGE_LIBRARY:-${PFX}/lib/libmdds_bridge_shared.z.so}"
LOG=/data/local/tmp/rmw_mdds_m2m

LDP="${PFX}/lib:/data/local/tmp/ohos-prefix/lib:/data/local/tmp/ohos-fastdds/lib:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64"
MDDS="HOME=/data/local/tmp ROS_LOG_DIR=${LOG} LD_LIBRARY_PATH=${LDP} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR}"

sh_cap() { timeout 60s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
# include fibonacci|action_tutorials — a leftover fibonacci_action_server from a prior
# lane/run otherwise survives kill_all and poisons the action lane (partial feedback / no
# SUCCEEDED). Documented residue in MDDS_RMW_NATIVE_DEFAULTON §4.
PAT='ros2|python3.12|topic |add_two|service call|rmw_mdds_broker|fibonacci|action_tutorials'
LANE_PAT='ros2|python3.12|topic |add_two|service call|fibonacci|action_tutorials'
stop_matching_processes() {
  local device="$1"
  local pattern="$2"
  sh_cap "$device" "pids=\$(ps -ef | grep -E \"${pattern}\" | grep -v grep | sed -E 's/^ *[^ ]+ +([0-9]+).*/\\1/'); for pid in \${pids}; do kill \"\${pid}\" 2>/dev/null || true; done; for attempt in 1 2 3 4 5; do alive=0; for pid in \${pids}; do kill -0 \"\${pid}\" 2>/dev/null && alive=1; done; [ \${alive} -eq 0 ] && break; sleep 1; done; for pid in \${pids}; do kill -0 \"\${pid}\" 2>/dev/null && kill -9 \"\${pid}\" 2>/dev/null || true; done; true" >/dev/null
}
kill_all() {
  for d in "$A" "$B"; do
    # Graceful broker shutdown releases MDDS/DSoftBus resources; SIGKILL is fallback only.
    stop_matching_processes "$d" "$PAT"
  done
}
kill_lane_processes() {
  for d in "$A" "$B"; do
    stop_matching_processes "$d" "$LANE_PAT"
  done
}
trap kill_all EXIT
kill_all; sh_cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/*.log; true" >/dev/null
sh_cap "$B" "mkdir -p ${LOG}; rm -f ${LOG}/*.log; true" >/dev/null
sleep 2

PASS_COUNT=0
FAIL_COUNT=0

# ---- Lane 1: pub/sub (std_msgs/String), A pub -> B echo --------------------
# `-w 0` skips the CLI wait-for-matching-subscribers gate; the actual match is
# handled by the bridge (MatchTriggerVisitor) and data flows once matched.
sh_cap "$B" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic echo /m2m_chatter std_msgs/msg/String --no-daemon > ${LOG}/echo.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
sleep 15
sh_cap "$A" "rm -f ${LOG}/pub.done; nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic pub --times 40 -r 2 -w 0 /m2m_chatter std_msgs/msg/String \"{data: dualmdds_ok}\" > ${LOG}/pub.log 2>&1; rc=\$?; echo RMW_MDDS_PUBLISH_DONE rc=\${rc} > ${LOG}/pub.done' >/dev/null 2>&1 & echo p" >/dev/null
PUB_DONE=""
for _ in {1..75}; do
  PUB_DONE="$(sh_cap "$A" "cat ${LOG}/pub.done 2>/dev/null")"
  [[ "${PUB_DONE}" == "RMW_MDDS_PUBLISH_DONE rc=0" ]] && break
  [[ "${PUB_DONE}" == RMW_MDDS_PUBLISH_DONE\ rc=* ]] && break
  sleep 1
done
sleep 3
# STRICT: count only after the publisher's board-side completion marker. The
# default QoS is RELIABLE, so all 40 completed publishes must arrive exactly once.
RX="$(sh_cap "$B" "grep -c dualmdds_ok ${LOG}/echo.log 2>/dev/null")"
RX="${RX:-0}"; [[ "$RX" =~ ^[0-9]+$ ]] || RX=0
PUB_SENT="$(sh_cap "$A" "grep -c '^publishing #' ${LOG}/pub.log 2>/dev/null")"
PUB_SENT="${PUB_SENT:-0}"; [[ "$PUB_SENT" =~ ^[0-9]+$ ]] || PUB_SENT=0
if [[ "${PUB_DONE}" != "RMW_MDDS_PUBLISH_DONE rc=0" ]]; then
  echo "RESULT|m2m_pubsub_std_msgs_string|FAIL|stage=publisher_completion;marker=${PUB_DONE:-missing};published=${PUB_SENT}/40;received=${RX}/40"
  FAIL_COUNT=$((FAIL_COUNT + 1))
elif [[ "$RX" -eq 40 && "$PUB_SENT" -eq 40 ]]; then
  echo "RESULT|m2m_pubsub_std_msgs_string|PASS|received=${RX}/40 exact(RELIABLE zero-loss)"
  PASS_COUNT=$((PASS_COUNT + 1))
else
  echo "RESULT|m2m_pubsub_std_msgs_string|FAIL|published=${PUB_SENT}/40;received=${RX}/40 (want exact 40/40)"
  FAIL_COUNT=$((FAIL_COUNT + 1))
fi
# Reuse the broad cleanup (matches python3.12/ros2/topic on both boards): toybox
# `ps -ef` can truncate the COMMAND column so a narrow 'topic pub'/'topic echo'
# substring may miss the process and leak the lane-1 subscriber into lane 2.
kill_lane_processes
sleep 2

# ---- Lane 2: service (AddTwoInts), A client -> B server --------------------
# Capture client output to a file (NOT a pipe): `... | head` triggers SIGPIPE
# and leaves the CLI hung after the response, masking the PASS.
sh_cap "$B" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/lib/demo_nodes_cpp/add_two_ints_server > ${LOG}/srv.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
sleep 18
sh_cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 service call /add_two_ints example_interfaces/srv/AddTwoInts \"{a: 41, b: 1}\" > ${LOG}/call.log 2>&1' >/dev/null 2>&1 & echo c" >/dev/null
sleep 16
# Numeric guard, NOT a non-empty-string test: an unreachable device makes `hdc`
# print its `[Fail]…` banner to stdout, which would satisfy `[[ -n … ]]` and
# report a false PASS. Requiring a non-zero integer (same as the pub/sub lane)
# rejects that banner and any other non-count output.
SUM="$(sh_cap "$A" "grep -c 'sum=42' ${LOG}/call.log 2>/dev/null")"
if [[ "${SUM:-0}" =~ ^[0-9]+$ && "${SUM:-0}" -gt 0 ]]; then
  echo "RESULT|m2m_service_add_two_ints|PASS|sum=42"
  PASS_COUNT=$((PASS_COUNT + 1))
else
  echo "RESULT|m2m_service_add_two_ints|FAIL"
  FAIL_COUNT=$((FAIL_COUNT + 1))
  echo "--- client call.log ---"; sh_cap "$A" "cat ${LOG}/call.log 2>/dev/null | tail -6"
  echo "--- server srv.log ---";  sh_cap "$B" "cat ${LOG}/srv.log 2>/dev/null | tail -4"
fi
kill_lane_processes
sleep 2

# ---- Lane 3: action (Fibonacci, full protocol), A send_goal -> B server -----
# Exercises the 3 action services (send_goal/get_result/cancel) + 2 topics
# (feedback/status) at once, all over DSoftBus. PASS iff the goal reaches the
# terminal SUCCEEDED state (numeric guard, same rationale as lane 2).
ACT=action_tutorials_interfaces/action/Fibonacci
sh_cap "$B" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/lib/action_tutorials_cpp/fibonacci_action_server > ${LOG}/asrv.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
sleep 20
sh_cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 action send_goal /fibonacci ${ACT} \"{order: 5}\" --feedback > ${LOG}/goal.log 2>&1' >/dev/null 2>&1 & echo g" >/dev/null
sleep 28
# STRICT: not just terminal SUCCEEDED, but the result payload. Fibonacci order=5 ->
# result.sequence=[0,1,1,2,3,5]. toybox has no `tr` — device emits one digit per line
# (sed range excludes the Goal-ID hex + feedback段; grep -oE normalises block/flow YAML),
# the host joins. $() strips the trailing newline -> compare without trailing comma.
SUCC="$(sh_cap "$A" "grep -c 'Goal finished with status: SUCCEEDED' ${LOG}/goal.log 2>/dev/null")"
SUCC="${SUCC:-0}"; [[ "$SUCC" =~ ^[0-9]+$ ]] || SUCC=0
SEQ_RAW="$(sh_cap "$A" "sed -n '/^Result:/,/Goal finished/p' ${LOG}/goal.log | grep -oE '[0-9]+'")"
SEQ="$(printf '%s' "$SEQ_RAW" | tr '\n' ',' | sed 's/,$//')"
FB="$(sh_cap "$A" "grep -c '^Feedback:' ${LOG}/goal.log 2>/dev/null")"; FB="${FB:-0}"
if [[ "$SUCC" -ge 1 && "$SEQ" == "0,1,1,2,3,5" ]]; then
  echo "RESULT|m2m_action_fibonacci|PASS|status=SUCCEEDED;sequence=0,1,1,2,3,5;feedback_msgs=${FB}"
  PASS_COUNT=$((PASS_COUNT + 1))
else
  echo "RESULT|m2m_action_fibonacci|FAIL|succeeded=${SUCC};sequence=${SEQ:-none};feedback=${FB}"
  FAIL_COUNT=$((FAIL_COUNT + 1))
  echo "--- client goal.log ---"; sh_cap "$A" "cat ${LOG}/goal.log 2>/dev/null | tail -14"
  echo "--- server asrv.log ---"; sh_cap "$B" "cat ${LOG}/asrv.log 2>/dev/null | tail -4"
fi

echo "M2M_SUMMARY pass=${PASS_COUNT} fail=${FAIL_COUNT}"
if [[ "${FAIL_COUNT}" -ne 0 ]]; then
  exit 1
fi
echo "cross_board_rmw_mdds_m2m_ok"
