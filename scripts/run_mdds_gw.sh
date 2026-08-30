#!/usr/bin/env bash
# mdds_gateway (L3) e2e orchestrator: PC (rmw_cyclonedds_cpp) <-> mdds_gateway
# on board A <-> mdds domain (boards A/B, rmw_mdds). Scenarios GW-01..GW-06 of
# docs/designs/mdds_test_plan.md. The PC side runs generated .bat files under
# C:\pixi_ws (Jazzy binary install, unicast cyclonedds toward board A).
#
#   ./scripts/run_mdds_gw.sh [gw01 ... gw06 | all]
# default/all: gw01 gw02 gw03 gw04 gw05 gw06
set -uo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00
BOARD_B=3e01ff55454d202020104433991c3b00
DEVICE_DIR=/data/local/tmp/ros2
LOGDIR=ohos_test_logs/mdds_gw
PC_WS=/c/pixi_ws
PC_BAT_DIR="$(cygpath -w "$PWD/scripts/mdds_e2e/pc")"
mkdir -p "$LOGDIR"

export MSYS2_ARG_CONV_EXCL='*'

# Remote env prefixes: mdds nodes pin rmw_mdds. The gateway must NOT have
# RMW_IMPLEMENTATION set (it pins rmw_cyclonedds_cpp itself and fails closed
# on a foreign RMW) and needs the unicast cyclone config toward the PC.
RENVS=". $DEVICE_DIR/env.sh; export RMW_IMPLEMENTATION=rmw_mdds;"
GWENVS=". $DEVICE_DIR/env.sh; unset RMW_IMPLEMENTATION; export CYCLONEDDS_URI=$DEVICE_DIR/mdds_e2e/cyclonedds_board_a.xml;"

shell()  { "$HDC" -t "$1" shell "$2" </dev/null; }
rbg()    { shell "$1" "$RENVS nohup $2 > $DEVICE_DIR/$3 2>&1 &" & }
pull()   { shell "$1" "cat $DEVICE_DIR/$2" > "$LOGDIR/$2" 2>/dev/null || true; }

kill_gw() {
  # separate command from any gateway launch: pkill -f would also match an
  # invoking shell whose own command line contains the pattern
  shell "$BOARD_A" "pkill -f 'lib/mdds_gateway' 2>/dev/null; true" >/dev/null 2>&1 || true
  sleep 1
}
stopall() {
  for b in "$BOARD_A" "$BOARD_B"; do
    shell "$b" "pkill -f 'talker|listener|add_two_ints|fibonacci|board_sweep|parameters' 2>/dev/null; true" >/dev/null 2>&1 || true
  done
  sleep 1
}
# MSYS2_ARG_CONV_EXCL='*' (needed for hdc) also stops the //F -> /F escape;
# re-enable conversion for native Windows tools, same as pc_start.
pc_kill() { MSYS2_ARG_CONV_EXCL= taskkill //F //IM "$1" >/dev/null 2>&1 || true; }
pc_stopall() { pc_kill talker.exe; pc_kill listener.exe; pc_kill python.exe; }
gw_reset() { stopall; kill_gw; pc_stopall; }

push_gw_files() {
  for b in "$BOARD_A" "$BOARD_B"; do
    shell "$b" "mkdir -p $DEVICE_DIR/mdds_e2e"
    "$HDC" -t "$b" file send "$(cygpath -w scripts/mdds_e2e/board_sweep.py)" \
      "$DEVICE_DIR/mdds_e2e/board_sweep.py" </dev/null >/dev/null
  done
  for f in cyclonedds_board_a.xml mdds_gateway_test.conf; do
    "$HDC" -t "$BOARD_A" file send "$(cygpath -w "scripts/mdds_e2e/$f")" \
      "$DEVICE_DIR/mdds_e2e/$f" </dev/null >/dev/null
  done
}

# start_gateway <timeout_s> <log>
start_gateway() {
  shell "$BOARD_A" "$GWENVS nohup timeout $1 $DEVICE_DIR/lib/mdds_gateway/mdds_gateway -c $DEVICE_DIR/mdds_e2e/mdds_gateway_test.conf > $DEVICE_DIR/$2 2>&1 &" >/dev/null 2>&1 &
}

# pc_start <bat> <log> [bat-args...]
pc_start() {
  local bat="$1" log="$2"; shift 2
  # Keep MSYS2_ARG_CONV_EXCL='*' (script-global, needed for hdc): with
  # conversion disabled, `cmd /c` reaches cmd literally (the //c escape would
  # NOT be undone), and bat arguments such as `--topic /mdds_sweep` pass
  # through unmangled (enabling conversion rewrote /mdds_sweep into
  # C:/Program Files/Git/mdds_sweep — the GW-04 failure).
  (cd "$PC_WS" && cmd /c "$PC_BAT_DIR\\$bat" "$@") > "$LOGDIR/$log" 2>&1 &
}

pass=0; fail=0; failed_ids=()
verdict() { # verdict <ID> <0|1> [detail]
  if [ "$2" -eq 0 ]; then echo "$1 PASS $3"; pass=$((pass+1));
  else echo "$1 FAIL $3"; fail=$((fail+1)); failed_ids+=("$1"); fi
}
heard_count() { grep -c "I heard" "$LOGDIR/$1" 2>/dev/null || true; }
dup_max() { # highest per-message delivery count in a listener log (0 = none heard)
  local n
  n=$(grep -o "I heard: \[[^]]*\]" "$LOGDIR/$1" 2>/dev/null | sort | uniq -c | awk '{print $1}' | sort -rn | head -1)
  echo "${n:-0}"
}

# --- scenarios ---------------------------------------------------------------

s_gw01() {
  # PC talker -> gateway -> board A rmw_mdds listener (/chatter)
  gw_reset
  rbg "$BOARD_A" "\$ROS2_LISTENER" gw01_listener.log
  start_gateway 120 gw01_gw.log
  sleep 6
  pc_start gw_pc_talker.bat gw01_pc_talker.log
  sleep 30
  pc_kill talker.exe
  stopall; kill_gw
  pull "$BOARD_A" gw01_listener.log
  pull "$BOARD_A" gw01_gw.log
  local n; n=$(heard_count gw01_listener.log)
  [ "$n" -ge 15 ]; verdict "GW-01" $? "PC->A heard=$n (>=15)"
}

s_gw02() {
  # board B rmw_mdds talker -> gateway -> PC listener (/chatter)
  gw_reset
  pc_start gw_pc_listener.bat gw02_pc_listener.log
  start_gateway 120 gw02_gw.log
  sleep 6
  rbg "$BOARD_B" "\$ROS2_TALKER" gw02_talker.log
  sleep 30
  pc_kill listener.exe
  stopall; kill_gw
  pull "$BOARD_A" gw02_gw.log
  local n; n=$(heard_count gw02_pc_listener.log)
  [ "$n" -ge 15 ]; verdict "GW-02" $? "B->PC heard=$n (>=15)"
}

s_gw03() {
  # Bidirectional 60 s: PC talker /chatter -> A listener; B talker on
  # /chatter_back -> PC listener. No duplicates on either side.
  gw_reset
  rbg "$BOARD_A" "\$ROS2_LISTENER" gw03_a_listener.log
  start_gateway 180 gw03_gw.log
  sleep 6
  pc_start gw_pc_talker.bat gw03_pc_talker.log
  rbg "$BOARD_B" "\$ROS2_TALKER --ros-args -r chatter:=chatter_back" gw03_b_talker.log
  pc_start gw_pc_listener.bat gw03_pc_listener.log chatter_back
  sleep 60
  pc_kill talker.exe; pc_kill listener.exe
  stopall; kill_gw
  pull "$BOARD_A" gw03_a_listener.log
  pull "$BOARD_A" gw03_gw.log
  local n1 n2 d1 d2
  n1=$(heard_count gw03_a_listener.log)
  n2=$(heard_count gw03_pc_listener.log)
  d1=$(dup_max gw03_a_listener.log)
  d2=$(dup_max gw03_pc_listener.log)
  [ "$n1" -ge 40 ] && [ "$n2" -ge 40 ] && [ "$d1" -le 1 ] && [ "$d2" -le 1 ]
  verdict "GW-03" $? "bidir 60s: PC->A heard=$n1 dup=$d1, B->PC heard=$n2 dup=$d2"
}

s_gw04() {
  # Large messages PC -> board B across the gateway: 1KB..4MB, per-block
  # sequence continuity (lost=0 reorder=0; head-of-run discovery loss is
  # expected gateway behavior, so received>=1 per block, not ==count).
  gw_reset
  rbg "$BOARD_B" "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode sub --topic /mdds_sweep --sizes 1024,4096,65536,262144,1048576,4194304 --idle-timeout 20" gw04_sub.log
  start_gateway 300 gw04_gw.log
  sleep 6
  # --depth 200: the reliable Cyclone hop over WiFi repairs fragment losses
  # from the writer's history; KEEP_LAST(10) at 18-64 msg/s leaves only
  # ~150-550 ms of repair slack and thrashes (observed: 64KB block lost 174/183).
  # --rate 10: the Windows sleep floor (~15.6 ms) pins default 50 msg/s pacing
  # to ~64 msg/s, which overruns the gateway's cyclone receive path on 4KB.
  pc_start gw_pc_sweep_pub.bat gw04_pub.log --topic /mdds_sweep --sizes 1024,4096,65536,262144,1048576,4194304 --rate 10 --rate-bps 1200000 --depth 200 --wait-match --flush-ms 20000
  # worst case ~70s of paced publishing; poll for completion (hdc shell always
  # exits 0 — poll on captured content, never on exit status)
  local i n
  for i in $(seq 1 24); do
    sleep 10
    n=$(shell "$BOARD_B" "grep -c SWEEP_RESULT $DEVICE_DIR/gw04_sub.log 2>/dev/null || true" | tr -dc '0-9')
    [ -n "$n" ] && [ "$n" -ge 1 ] && break
  done
  pc_kill python.exe
  stopall; kill_gw
  pull "$BOARD_B" gw04_sub.log
  pull "$BOARD_A" gw04_gw.log
  local bad=0 line recv
  for size in 1024 4096 65536 262144 1048576 4194304; do
    line=$(grep "SWEEP-SUB size=$size " "$LOGDIR/gw04_sub.log")
    if [ -z "$line" ]; then echo "   missing block size=$size"; bad=1; continue; fi
    echo "$line" | grep -q "lost=0 reorder=0" || { echo "   gap/reorder: $line"; bad=1; }
    recv=$(echo "$line" | sed -n 's/.*received=\([0-9]*\)\/.*/\1/p')
    [ "${recv:-0}" -ge 1 ] || { echo "   zero received: $line"; bad=1; }
  done
  verdict "GW-04" $bad "large msgs PC->B 1KB..4MB seq-contiguous (gw04_sub.log)"
}

s_gw05() {
  # Ring dedup: B runs talker+listener on /chatter while the gateway bridges
  # it both ways; every message must arrive exactly once.
  gw_reset
  rbg "$BOARD_B" "\$ROS2_LISTENER" gw05_b_listener.log
  start_gateway 120 gw05_gw.log
  sleep 6
  rbg "$BOARD_B" "\$ROS2_TALKER" gw05_b_talker.log
  sleep 30
  stopall; kill_gw
  pull "$BOARD_B" gw05_b_listener.log
  pull "$BOARD_A" gw05_gw.log
  local n d
  n=$(heard_count gw05_b_listener.log)
  d=$(dup_max gw05_b_listener.log)
  [ "$n" -ge 20 ] && [ "$d" -le 1 ]
  verdict "GW-05" $? "ring dedup: heard=$n dup_max=$d (must be 1)"
}

s_gw06() {
  # Restart recovery: PC talker -> B listener flowing; kill the gateway for
  # 10 s, restart it; forwarding must resume (no storm: dup_max stays 1).
  gw_reset
  rbg "$BOARD_B" "\$ROS2_LISTENER" gw06_b_listener.log
  start_gateway 200 gw06_gw1.log
  sleep 6
  pc_start gw_pc_talker.bat gw06_pc_talker.log
  sleep 15
  local n1
  n1=$(shell "$BOARD_B" "grep -c 'I heard' $DEVICE_DIR/gw06_b_listener.log 2>/dev/null || true" | tr -dc '0-9')
  kill_gw
  sleep 10
  start_gateway 120 gw06_gw2.log
  sleep 25
  pc_kill talker.exe
  stopall; kill_gw
  pull "$BOARD_B" gw06_b_listener.log
  pull "$BOARD_A" gw06_gw2.log
  local n2 d
  n2=$(heard_count gw06_b_listener.log)
  d=$(dup_max gw06_b_listener.log)
  [ "${n1:-0}" -ge 5 ] && [ "$n2" -ge $((n1 + 5)) ] && [ "$d" -le 1 ]
  verdict "GW-06" $? "restart recovery: heard before=${n1:-0} after=$n2 dup_max=$d"
}

# --- main --------------------------------------------------------------------

if [ $# -eq 0 ]; then
  set -- gw01 gw02 gw03 gw04 gw05 gw06
elif [ "$1" = all ]; then
  set -- gw01 gw02 gw03 gw04 gw05 gw06
fi
push_gw_files
for sc in "$@"; do
  echo "== scenario: $sc =="
  "s_$sc" || echo "   (scenario $sc errored)"
done

echo
echo "== mdds gateway summary: $pass passed, $fail failed =="
[ ${#failed_ids[@]} -eq 0 ] || printf '   FAIL %s\n' "${failed_ids[@]}"
[ "$fail" -eq 0 ]
