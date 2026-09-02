#!/usr/bin/env bash
# mdds L4 stress/resilience orchestrator: PERF-01/02, SOAK-01, RES-01/02 of
# ../../docs/designs/mdds_test_plan.md. Board A <-> board B, rmw_mdds, logs land in
# ohos_test_logs/mdds_l4/. SOAK-01 runs 30 min and is not part of "all".
#
#   ./scripts/run_mdds_l4.sh [perf01|perf02|soak01|res01|res02|all]
set -uo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00
BOARD_B=3e01ff55454d202020104433991c3b00
DEVICE_DIR=/data/local/tmp/ros2
LOGDIR=ohos_test_logs/mdds_l4
mkdir -p "$LOGDIR"

export MSYS2_ARG_CONV_EXCL='*'

RENVS=". $DEVICE_DIR/env.sh; export RMW_IMPLEMENTATION=rmw_mdds;"
SWEEP="python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py"

shell()  { "$HDC" -t "$1" shell "$2" </dev/null; }
rbg()    { shell "$1" "$RENVS nohup $2 > $DEVICE_DIR/$3 2>&1 &" & }
pull()   { shell "$1" "cat $DEVICE_DIR/$2" > "$LOGDIR/$2" 2>/dev/null || true; }

stopall() {
  # Kill only processes running our test-installed executables/scripts; never
  # a bare name match, and not even the whole deploy root (an editor or shell
  # with the deploy dir in its command line must survive).
  for b in "$BOARD_A" "$BOARD_B"; do
    shell "$b" "pkill -f '$DEVICE_DIR/[Ll]ib/|$DEVICE_DIR/bin/ros2|$DEVICE_DIR/mdds_e2e/' 2>/dev/null; true" >/dev/null 2>&1 || true
  done
  sleep 1
}

push_sweep() {
  for b in "$BOARD_A" "$BOARD_B"; do
    shell "$b" "mkdir -p $DEVICE_DIR/mdds_e2e"
    "$HDC" -t "$b" file send "$(cygpath -w scripts/mdds_e2e/board_sweep.py)" \
      "$DEVICE_DIR/mdds_e2e/board_sweep.py" </dev/null >/dev/null
  done
}

pass=0; fail=0; failed_ids=()
verdict() { # verdict <ID> <0|1> [detail]
  if [ "$2" -eq 0 ]; then echo "$1 PASS $3"; pass=$((pass+1));
  else echo "$1 FAIL $3"; fail=$((fail+1)); failed_ids+=("$1"); fi
}

# Remote grep counter that always prints exactly one integer (hdc shell exit
# codes are unreliable; count matches, never status).
rcount() { shell "$1" "grep -c '$2' $DEVICE_DIR/$3 2>/dev/null || true" | tr -dc '0-9'; }

# poll_until <board> <pattern> <logfile> <tries> <interval_s>
poll_until() {
  local i n
  for i in $(seq 1 "$4"); do
    sleep "$5"
    n=$(rcount "$1" "$2" "$3")
    [ -n "$n" ] && [ "$n" -ge 1 ] && return 0
  done
  return 1
}

# --- scenarios ---------------------------------------------------------------

s_perf01() {
  # Throughput baseline, both directions sequentially: full size sweep at an
  # offered byte rate ABOVE the old dsoftbus-lane capacity so the udp
  # cross-device path sets the curve (lost/reorder are expected at overload
  # sizes; the baseline table records achieved MB/s per size).
  stopall
  rbg "$BOARD_A" "$SWEEP --mode sub --idle-timeout 25" perf01_ab_sub.log
  sleep 4
  rbg "$BOARD_B" "$SWEEP --mode pub --rate-bps 60000000" perf01_ab_pub.log
  poll_until "$BOARD_A" "SWEEP_RESULT" perf01_ab_sub.log 40 10 || true
  stopall
  pull "$BOARD_A" perf01_ab_sub.log

  rbg "$BOARD_B" "$SWEEP --mode sub --idle-timeout 25" perf01_ba_sub.log
  sleep 4
  rbg "$BOARD_A" "$SWEEP --mode pub --rate-bps 60000000" perf01_ba_pub.log
  poll_until "$BOARD_B" "SWEEP_RESULT" perf01_ba_sub.log 40 10 || true
  stopall
  pull "$BOARD_B" perf01_ba_sub.log

  {
    echo "# PERF-01 throughput baseline ($(date -Iseconds))"
    echo "# direction size_B received/expected lost reorder MB/s"
    for dir in ab ba; do
      grep "SWEEP-SUB size=" "$LOGDIR/perf01_${dir}_sub.log" 2>/dev/null | \
        sed "s/SWEEP-SUB /$dir /" || echo "$dir no-data"
    done
  } | tee "$LOGDIR/perf01_baseline.txt"
  grep -q "mbps=" "$LOGDIR/perf01_baseline.txt"
  verdict "PERF-01" $? "baseline table -> $LOGDIR/perf01_baseline.txt"
}

s_perf02() {
  # Hz storm: 4 talker/listener pairs on independent topics /perf_1..4,
  # 100 Hz x 1 KB x 30 s (3000 msgs) each; every listener >= 95% reception.
  stopall
  local i
  for i in 1 2 3 4; do
    rbg "$BOARD_A" "$SWEEP --mode sub --topic /perf_$i --sizes 1024 --count 3000 --idle-timeout 20" perf02_sub$i.log
  done
  sleep 4
  for i in 1 2 3 4; do
    rbg "$BOARD_B" "$SWEEP --mode pub --topic /perf_$i --sizes 1024 --count 3000 --rate 100" perf02_pub$i.log
  done
  for i in 1 2 3 4; do
    poll_until "$BOARD_A" "SWEEP_RESULT" perf02_sub$i.log 12 5 || true
  done
  stopall
  local bad=0 line recv
  for i in 1 2 3 4; do
    pull "$BOARD_A" perf02_sub$i.log
    line=$(grep "SWEEP-SUB size=1024 " "$LOGDIR/perf02_sub$i.log")
    if [ -z "$line" ]; then echo "   pair $i: no result line"; bad=1; continue; fi
    recv=$(echo "$line" | sed -n 's/.*received=\([0-9]*\)\/.*/\1/p')
    echo "   pair $i: $line"
    # 95% of 3000 = 2850
    [ "${recv:-0}" -ge 2850 ] || { echo "   pair $i below 95%"; bad=1; }
  done
  verdict "PERF-02" $bad "4x100Hzx1KBx30s reception >=95% per listener"
}

s_soak01() {
  # Soak: 10 Hz x 64 KB x 30 min (18000 msgs), RSS sampled once a minute on
  # both endpoints; 0 loss over the whole run; RSS growth <= 10%.
  stopall
  rbg "$BOARD_A" "$SWEEP --mode sub --sizes 65536 --count 18000 --idle-timeout 30" soak01_sub.log
  sleep 4
  rbg "$BOARD_B" "$SWEEP --mode pub --sizes 65536 --count 18000 --rate 10" soak01_pub.log
  local minute pub_pid sub_pid pub_rss sub_rss
  : > "$LOGDIR/soak01_rss.txt"
  # Board-side pid/RSS sampling WITHOUT awk (absent on the board): ps output is
  # word-split with `set --`; VmRSS fields are split with `read`. All numeric
  # filtering (CRLF stripping) happens host-side via tr.
  for minute in $(seq 1 31); do
    sleep 60
    pub_pid=$(shell "$BOARD_B" "ps -ef | grep 'board_sweep.py --mode pub' | grep -v grep | head -1 | { read -r line; set -- \$line; echo \$2; }" | tr -dc '0-9')
    sub_pid=$(shell "$BOARD_A" "ps -ef | grep 'board_sweep.py --mode sub' | grep -v grep | head -1 | { read -r line; set -- \$line; echo \$2; }" | tr -dc '0-9')
    pub_rss=$([ -n "$pub_pid" ] && shell "$BOARD_B" "grep VmRSS /proc/$pub_pid/status 2>/dev/null | { read -r k v u; echo \$v; }" | tr -dc '0-9')
    sub_rss=$([ -n "$sub_pid" ] && shell "$BOARD_A" "grep VmRSS /proc/$sub_pid/status 2>/dev/null | { read -r k v u; echo \$v; }" | tr -dc '0-9')
    echo "minute=$minute pub_rss_kb=${pub_rss:-dead} sub_rss_kb=${sub_rss:-dead}" | tee -a "$LOGDIR/soak01_rss.txt"
  done
  # The sub reports its verdict only after --idle-timeout of silence, and pub
  # pacing slip can push the publish phase past the nominal 1800 s; poll for
  # the verdict instead of killing the sub blind (batch mode runs rclpy with
  # SignalHandlerOptions.NO, so SIGTERM leaves no trace).
  poll_until "$BOARD_A" 'SWEEP_RESULT' soak01_sub.log 24 10 || \
    echo "   no SWEEP_RESULT within grace window"
  stopall
  pull "$BOARD_A" soak01_sub.log
  local line recv lost
  line=$(grep "SWEEP-SUB size=65536 " "$LOGDIR/soak01_sub.log")
  echo "   $line"
  recv=$(echo "$line" | sed -n 's/.*received=\([0-9]*\)\/.*/\1/p')
  lost=$(echo "$line" | sed -n 's/.*lost=\([0-9]*\).*/\1/p')
  # RSS growth <= 10% between minute 1 and minute 31 (both endpoints)
  local first last bad=0
  first=$(sed -n 's/minute=1 pub_rss_kb=\([0-9]*\).*/\1/p' "$LOGDIR/soak01_rss.txt")
  last=$(sed -n 's/minute=31 pub_rss_kb=\([0-9]*\).*/\1/p' "$LOGDIR/soak01_rss.txt")
  if [ -n "$first" ] && [ -n "$last" ] && [ "$last" -gt $((first * 110 / 100)) ]; then
    echo "   pub RSS grew >10%: $first -> $last kB"; bad=1
  fi
  first=$(sed -n 's/minute=1 sub_rss_kb=\([0-9]*\).*/\1/p' "$LOGDIR/soak01_rss.txt")
  last=$(sed -n 's/minute=31 sub_rss_kb=\([0-9]*\).*/\1/p' "$LOGDIR/soak01_rss.txt")
  if [ -n "$first" ] && [ -n "$last" ] && [ "$last" -gt $((first * 110 / 100)) ]; then
    echo "   sub RSS grew >10%: $first -> $last kB"; bad=1
  fi
  [ "${recv:-0}" -ge 18000 ] && [ "${lost:-1}" -eq 0 ] && [ $bad -eq 0 ]
  verdict "SOAK-01" $? "10Hzx64KBx30min received=${recv:-0}/18000 lost=${lost:-na} (rss: soak01_rss.txt)"
}

s_res01() {
  # Listener restart: stream A->B, kill B listener mid-stream, restart after
  # 5 s, reception must resume within 10 s.
  stopall
  rbg "$BOARD_A" "\$ROS2_TALKER" res01_talker.log
  sleep 3
  rbg "$BOARD_B" "\$ROS2_LISTENER" res01_listener1.log
  sleep 10
  shell "$BOARD_B" "pkill -f '$DEVICE_DIR/Lib/demo_nodes_cpp/listener' 2>/dev/null; true" >/dev/null 2>&1 || true
  sleep 5
  rbg "$BOARD_B" "\$ROS2_LISTENER" res01_listener2.log
  # recovery: new messages in the restarted listener's log within 10 s
  local ok=1 i n
  for i in $(seq 1 10); do
    sleep 1
    n=$(rcount "$BOARD_B" "I heard" res01_listener2.log)
    [ -n "$n" ] && [ "$n" -ge 3 ] && { ok=0; break; }
  done
  stopall
  pull "$BOARD_B" res01_listener1.log
  pull "$BOARD_B" res01_listener2.log
  verdict "RES-01" $ok "listener restart: heard=${n:-0} within 10s of restart"
}

s_res02() {
  # Talker restart: stream A->B, kill A talker mid-stream, restart after 5 s,
  # reception must resume within 10 s.
  stopall
  rbg "$BOARD_B" "\$ROS2_LISTENER" res02_listener.log
  sleep 3
  rbg "$BOARD_A" "\$ROS2_TALKER" res02_talker1.log
  sleep 10
  shell "$BOARD_A" "pkill -f '$DEVICE_DIR/Lib/demo_nodes_cpp/talker' 2>/dev/null; true" >/dev/null 2>&1 || true
  local n_before
  n_before=$(rcount "$BOARD_B" "I heard" res02_listener.log)
  sleep 5
  rbg "$BOARD_A" "\$ROS2_TALKER" res02_talker2.log
  local ok=1 i n
  for i in $(seq 1 10); do
    sleep 1
    n=$(rcount "$BOARD_B" "I heard" res02_listener.log)
    [ -n "$n" ] && [ "${n_before:-0}" -ge 3 ] && [ "$n" -ge $((n_before + 3)) ] && { ok=0; break; }
  done
  stopall
  pull "$BOARD_B" res02_listener.log
  verdict "RES-02" $ok "talker restart: heard before=${n_before:-0} after=${n:-0} within 10s"
}

# --- main --------------------------------------------------------------------

if [ $# -eq 0 ] || [ "$1" = all ]; then
  set -- perf01 perf02 res01 res02
fi
push_sweep
for sc in "$@"; do
  echo "== scenario: $sc =="
  if ! declare -F "s_$sc" >/dev/null; then
    # Unknown scenario names must count as failures, not just a note.
    echo "   unknown scenario: $sc"
    fail=$((fail+1)); failed_ids+=("$sc")
    continue
  fi
  "s_$sc" || echo "   (scenario $sc errored)"
done

echo
echo "== mdds L4 summary: $pass passed, $fail failed =="
[ ${#failed_ids[@]} -eq 0 ] || printf '   FAIL %s\n' "${failed_ids[@]}"
[ "$fail" -eq 0 ]
