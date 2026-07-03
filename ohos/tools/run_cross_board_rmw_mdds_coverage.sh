#!/usr/bin/env bash
set -uo pipefail

# rmw_mdds type/QoS/payload coverage matrix over homogeneous rmw_mdds<->rmw_mdds
# (both boards rmw_mdds, no gateway — rmw_mdds carries ANY type natively, so this
# directly tests "can arbitrary ROS 2 messages flow through rmw_mdds").
#
#   board A (rmw_mdds): ros2 topic pub  -> DSoftBus ->  board B (rmw_mdds): ros2 topic echo
#
# Received count is type-agnostic: `ros2 topic echo` prints "---" between samples,
# so grep -c '^---' = samples received. Large-payload lanes also require an END
# marker in the echoed data to prove the payload arrived intact (not truncated).

usage() { echo "Usage: $0 <mdds-device-A> <mdds-device-B> [domain]" >&2; }
[[ $# -ge 2 && $# -le 3 ]] || { usage; exit 2; }
A="$1"; B="$2"; DOM="${3:-86}"
[[ "$A" != "$B" ]] || { echo "ERROR: devices must differ" >&2; exit 2; }
HDC="${HDC_BIN:-hdc}"
PFX=/data/local/tmp/ohos-colcon-rk3588a
BR=/data/local/tmp/libmdds_bridge_shared.z.so
LOG=/data/local/tmp/coverage
LDP="${PFX}/lib:/data/local/tmp/ohos-prefix/lib:/data/local/tmp/ohos-fastdds/lib:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64"
MDDS="LD_LIBRARY_PATH=${LDP} RMW_IMPLEMENTATION=rmw_mdds_cpp RMW_MDDS_BROKER=1 RMW_MDDS_BRIDGE_LIBRARY=${BR}"

cap() { timeout 60s "${HDC}" -t "$1" shell "$2" 2>&1 | grep -v "dumped core" || true; }
kill_clients() {
  for d in "$A" "$B"; do
    cap "$d" "ps -ef | grep -E 'topic pub|topic echo|ros2cli|rmw_mdds_broker' | grep -v grep | while read -r u pid r; do kill -9 \"\${pid}\" 2>/dev/null; done; true" >/dev/null
  done
}
trap kill_clients EXIT
PASS=0; FAIL=0

# Push a device-side big-payload publisher (avoids multi-level inline escaping):
# builds an N-byte String with COVSTART_/COVEND markers and publishes it. The
# MDDS env is exported by the caller before invoking it.
HELPER="$(mktemp)"
cat > "$HELPER" <<'BIGPUB'
#!/system/bin/sh
# $1=size $2=topic $3=times $4=rate
D="COVSTART_$(head -c "$1" /dev/zero | tr '\0' X)COVEND"
exec /data/local/tmp/ohos-colcon-rk3588a/bin/ros2 topic pub --times "$3" -r "$4" -w 0 "$2" std_msgs/msg/String "{data: $D}"
BIGPUB
"${HDC}" -t "$A" file send "$HELPER" /data/local/tmp/cov_bigpub.sh >/dev/null 2>&1
rm -f "$HELPER"

# run_lane <name> <ros_type> <pub_yaml_or_'@gen:SIZE'> <extra_grep|''> <times> <rate> <qos_flags>
run_lane() {
  local name="$1" rtype="$2" body="$3" extra="$4" times="$5" rate="$6" qos="$7"
  kill_clients; cap "$A" "mkdir -p ${LOG}; rm -f ${LOG}/${name}_*.log; true" >/dev/null
  cap "$B" "mkdir -p ${LOG}; rm -f ${LOG}/${name}_*.log; true" >/dev/null
  sleep 2
  local sub_qos="$qos" pub_setup=""
  # subscriber
  cap "$B" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic echo ${qos} /cov_${name} ${rtype} --no-daemon > ${LOG}/${name}_e.log 2>&1' >/dev/null 2>&1 & echo s" >/dev/null
  sleep 14
  # publisher — '@gen:N' builds an N-byte String payload on-device with start/end markers
  if [[ "$body" == @gen:* ]]; then
    local sz="${body#@gen:}"
    cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} sh /data/local/tmp/cov_bigpub.sh ${sz} /cov_${name} ${times} ${rate} > ${LOG}/${name}_p.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
  else
    cap "$A" "nohup sh -c '${MDDS} ROS_DOMAIN_ID=${DOM} ${PFX}/bin/ros2 topic pub ${qos} --times ${times} -r ${rate} -w 0 /cov_${name} ${rtype} \"${body}\" > ${LOG}/${name}_p.log 2>&1' >/dev/null 2>&1 & echo p" >/dev/null
  fi
  sleep 20
  local rx; rx="$(cap "$B" "grep -c '^---' ${LOG}/${name}_e.log 2>/dev/null" | tr -d '[:space:]')"
  local ok=1 detail="received=${rx:-0}"
  [[ "${rx:-0}" =~ ^[0-9]+$ && "${rx:-0}" -gt 0 ]] || ok=0
  if [[ -n "$extra" ]]; then
    local em; em="$(cap "$B" "grep -c '${extra}' ${LOG}/${name}_e.log 2>/dev/null" | tr -d '[:space:]')"
    detail="${detail}|${extra}×${em:-0}"
    [[ "${em:-0}" =~ ^[0-9]+$ && "${em:-0}" -gt 0 ]] || ok=0
  fi
  if [[ "$ok" == 1 ]]; then echo "RESULT|cov_${name}|PASS|${detail}"; PASS=$((PASS+1));
  else echo "RESULT|cov_${name}|FAIL|${detail}"; FAIL=$((FAIL+1));
    cap "$B" "tail -3 ${LOG}/${name}_e.log 2>/dev/null" ; fi
}

# --- complex / nested message types (the codec breadth gap) ---
run_lane twist        geometry_msgs/msg/Twist       "{linear: {x: 1.5}, angular: {z: 0.9}}"                    ""        20 2 ""
run_lane imu          sensor_msgs/msg/Imu           "{orientation: {w: 1.0}, angular_velocity: {x: 0.1}}"     ""        20 2 ""
run_lane odometry     nav_msgs/msg/Odometry         "{pose: {pose: {position: {x: 7.25}}}}"                   ""        20 2 ""
run_lane pointcloud2  sensor_msgs/msg/PointCloud2   "{height: 1, width: 5, point_step: 16, is_dense: true}"   "point_step" 15 2 ""
run_lane image        sensor_msgs/msg/Image         "{width: 64, height: 48, encoding: rgb8, step: 192}"      "rgb8"    15 2 ""
# --- large payloads (fragmentation); COVEND marker proves the payload is intact ---
run_lane large64k     std_msgs/msg/String           "@gen:65536"                                              "COVEND"  6  2 ""
run_lane large256k    std_msgs/msg/String           "@gen:262144"                                             "COVEND"  4  1 ""
# --- QoS variants ---
run_lane qos_translocal std_msgs/msg/String         "{data: tl}"                                              ""        15 2 "--qos-durability transient_local --qos-reliability reliable"
run_lane qos_besteffort std_msgs/msg/String         "{data: be}"                                              ""        20 2 "--qos-reliability best_effort"

echo "COVERAGE_SUMMARY|pass=${PASS}|fail=${FAIL}"
