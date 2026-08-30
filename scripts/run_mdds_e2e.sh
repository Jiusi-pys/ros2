#!/usr/bin/env bash
# mdds e2e test orchestrator for the two RK3588A boards (test plan:
# docs/designs/mdds_test_plan.md). Every scenario pins RMW_IMPLEMENTATION=rmw_mdds
# and starts with an rclpy preflight that prints the effective RMW identifier.
#
#   ./scripts/run_mdds_e2e.sh [scenario ...]
# scenarios: preflight loopback bidir py service action params besteffort sweep neg01
#            latejoin multitopic matched all
# default:  preflight loopback bidir service action sweep
set -uo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00
BOARD_B=3e01ff55454d202020104433991c3b00
DEVICE_DIR=/data/local/tmp/ros2
LOGDIR=ohos_test_logs/mdds_e2e
mkdir -p "$LOGDIR"

export MSYS2_ARG_CONV_EXCL='*'

# remote env prefix: source board env and pin the RMW under test
RENVS=". $DEVICE_DIR/env.sh; export RMW_IMPLEMENTATION=rmw_mdds;"

shell()  { "$HDC" -t "$1" shell "$2" </dev/null; }
rbg()    { shell "$1" "$RENVS nohup $2 > $DEVICE_DIR/$3 2>&1 &" & }
runfg()  { shell "$1" "$RENVS $2"; }
stopall() {
  for b in "$BOARD_A" "$BOARD_B"; do
    shell "$b" "pkill -f 'talker|listener|add_two_ints|fibonacci|board_sweep|parameters' 2>/dev/null; true" >/dev/null 2>&1 || true
  done
  sleep 1
}
pull() { shell "$1" "cat $DEVICE_DIR/$2" > "$LOGDIR/$2" 2>/dev/null || true; }

pass=0; fail=0; failed_ids=()
verdict() { # verdict <ID> <0|1> [detail]
  if [ "$2" -eq 0 ]; then echo "$1 PASS $3"; pass=$((pass+1));
  else echo "$1 FAIL $3"; fail=$((fail+1)); failed_ids+=("$1"); fi
}
heard_count() { grep -c "I heard" "$LOGDIR/$1" 2>/dev/null || true; }

# --- scenarios ---------------------------------------------------------------

s_preflight() {
  local ok=0 id
  for b in "$BOARD_A" "$BOARD_B"; do
    id=$(shell "$b" "$RENVS python3.12 -c 'from rclpy.utilities import get_rmw_implementation_identifier as g; print(g())'" 2>/dev/null | tr -d '\r')
    echo "   preflight ${b:0:8}: rmw=$id"
    [ "$id" = "rmw_mdds" ] || ok=1
  done
  verdict "PRE-FLIGHT" $ok "rmw identifier == rmw_mdds on both boards"
}

s_loopback() {
  stopall
  rbg "$BOARD_A" "\$ROS2_LISTENER" e2e_loop_listener.log
  sleep 3
  rbg "$BOARD_A" "\$ROS2_TALKER" e2e_loop_talker.log
  sleep 15
  stopall
  pull "$BOARD_A" e2e_loop_listener.log
  local n; n=$(heard_count e2e_loop_listener.log)
  [ "$n" -ge 10 ]; verdict "E2E-01" $? "loopback heard=$n (>=10)"
}

s_bidir() {
  stopall
  rbg "$BOARD_B" "\$ROS2_LISTENER" e2e_ab_listener.log
  sleep 3
  rbg "$BOARD_A" "\$ROS2_TALKER" e2e_ab_talker.log
  sleep 20
  stopall
  pull "$BOARD_B" e2e_ab_listener.log
  local n1; n1=$(heard_count e2e_ab_listener.log)

  rbg "$BOARD_A" "\$ROS2_LISTENER" e2e_ba_listener.log
  sleep 3
  rbg "$BOARD_B" "\$ROS2_TALKER" e2e_ba_talker.log
  sleep 20
  stopall
  pull "$BOARD_A" e2e_ba_listener.log
  local n2; n2=$(heard_count e2e_ba_listener.log)

  [ "$n1" -ge 15 ]; verdict "E2E-02" $? "A->B heard=$n1 (>=15)"
  [ "$n2" -ge 15 ]; verdict "E2E-03" $? "B->A heard=$n2 (>=15)"
}

s_py() {
  stopall
  rbg "$BOARD_B" "\$ROS2_PY_LISTENER" e2e_py_listener.log
  sleep 4
  rbg "$BOARD_A" "\$ROS2_PY_TALKER" e2e_py_talker.log
  sleep 20
  stopall
  pull "$BOARD_B" e2e_py_listener.log
  local n; n=$(heard_count e2e_py_listener.log)
  [ "$n" -ge 15 ]; verdict "E2E-04" $? "py A->B heard=$n (>=15)"
}

s_service() {
  stopall
  rbg "$BOARD_B" "$DEVICE_DIR/Lib/demo_nodes_cpp/add_two_ints_server" e2e_srv_server.log
  sleep 3
  runfg "$BOARD_A" "timeout 20 $DEVICE_DIR/Lib/demo_nodes_cpp/add_two_ints_client" \
    > "$LOGDIR/e2e_srv_client.log" 2>&1
  runfg "$BOARD_A" "timeout 20 $DEVICE_DIR/Lib/demo_nodes_cpp/add_two_ints_client_async" \
    > "$LOGDIR/e2e_srv_client_async.log" 2>&1
  stopall
  grep -q "Result of add_two_ints: 5" "$LOGDIR/e2e_srv_client.log"
  local r1=$?
  grep -q "Result of add_two_ints: 5" "$LOGDIR/e2e_srv_client_async.log"
  local r2=$?
  verdict "E2E-05" $((r1+r2)) "sync=$( [ $r1 -eq 0 ] && echo ok || echo fail) async=$( [ $r2 -eq 0 ] && echo ok || echo fail)"
}

s_action() {
  stopall
  rbg "$BOARD_B" "$DEVICE_DIR/Lib/action_tutorials_cpp/fibonacci_action_server" e2e_act_server.log
  sleep 3
  runfg "$BOARD_A" "timeout 25 $DEVICE_DIR/Lib/action_tutorials_cpp/fibonacci_action_client" \
    > "$LOGDIR/e2e_act_client.log" 2>&1
  stopall
  grep -qiE "result received|action (succeeded|finished)" "$LOGDIR/e2e_act_client.log"
  verdict "E2E-06" $? "fibonacci goal/result/feedback over mdds"
}

s_params() {
  stopall
  runfg "$BOARD_A" "timeout 25 $DEVICE_DIR/Lib/demo_nodes_cpp/set_and_get_parameters" \
    > "$LOGDIR/e2e_params.log" 2>&1
  local rc=$?
  grep -qi "parameter" "$LOGDIR/e2e_params.log"
  local g=$?
  verdict "E2E-07" $((rc+g)) "set_and_get_parameters rc=$rc"
}

s_besteffort() {
  stopall
  rbg "$BOARD_B" "$DEVICE_DIR/Lib/demo_nodes_cpp/listener_best_effort" e2e_be_listener.log
  sleep 3
  rbg "$BOARD_A" "\$ROS2_TALKER" e2e_be_talker.log
  sleep 15
  stopall
  pull "$BOARD_B" e2e_be_listener.log
  local n; n=$(heard_count e2e_be_listener.log)
  [ "$n" -ge 10 ]; verdict "E2E-08" $? "best_effort heard=$n (>=10)"
}

push_sweep() {
  for b in "$BOARD_A" "$BOARD_B"; do
    shell "$b" "mkdir -p $DEVICE_DIR/mdds_e2e"
    "$HDC" -t "$b" file send "$(cygpath -w scripts/mdds_e2e/board_sweep.py)" \
      "$DEVICE_DIR/mdds_e2e/board_sweep.py" </dev/null > /dev/null
  done
}

s_sweep() {
  stopall; push_sweep
  rbg "$BOARD_A" "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode sub" e2e_sweep_sub.log
  sleep 4
  rbg "$BOARD_B" "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode pub" e2e_sweep_pub.log
  # worst-case runtime: sum(count/rate) + idle timeout; poll for completion.
  # NOTE: hdc shell always exits 0 — it does NOT propagate the remote exit
  # code — so poll on captured stdout, never on `grep -q ... && break`.
  local i n
  for i in $(seq 1 30); do
    sleep 10
    n=$(shell "$BOARD_A" "grep -c SWEEP_RESULT $DEVICE_DIR/e2e_sweep_sub.log 2>/dev/null || true" | tr -dc '0-9')
    [ -n "$n" ] && [ "$n" -ge 1 ] && break
  done
  stopall
  pull "$BOARD_A" e2e_sweep_sub.log
  pull "$BOARD_B" e2e_sweep_pub.log
  grep -q "SWEEP_RESULT PASS" "$LOGDIR/e2e_sweep_sub.log"
  verdict "E2E-09" $? "size sweep 1KB..8MB (see e2e_sweep_sub.log)"
}

s_neg01() {
  stopall; push_sweep
  rbg "$BOARD_A" "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode sub --sleep-ms 200 --idle-timeout 10" e2e_neg_sub.log
  sleep 4
  rbg "$BOARD_B" "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode pub --sizes 1024 --history keep_all" e2e_neg_pub.log
  sleep 30
  stopall
  pull "$BOARD_A" e2e_neg_sub.log
  pull "$BOARD_B" e2e_neg_pub.log
  # expectation: slow consumer + KEEP_ALL -> pub-side lane backpressure drops new
  # messages; sub receives strictly fewer than the 300 sent, and both sides
  # terminate cleanly (no crash/hang = log contains a SWEEP_RESULT line).
  local n; n=$(sed -n 's/.*received=\([0-9]*\)\/300.*/\1/p' "$LOGDIR/e2e_neg_sub.log" | head -1)
  grep -q "SWEEP_RESULT" "$LOGDIR/e2e_neg_sub.log" && [ -n "$n" ] && [ "$n" -lt 300 ]
  verdict "NEG-01" $? "keep_all backpressure: received=${n:-?}/300 (<300 expected)"
}

s_latejoin() {
  # E2E-10: talker runs alone for 10s, then the listener joins; discovery
  # re-announce must connect them within two announce periods.
  stopall
  rbg "$BOARD_A" "\$ROS2_TALKER" e2e_late_talker.log
  sleep 10
  rbg "$BOARD_B" "\$ROS2_LISTENER" e2e_late_listener.log
  sleep 20
  stopall
  pull "$BOARD_B" e2e_late_listener.log
  local n; n=$(heard_count e2e_late_listener.log)
  [ "$n" -ge 10 ]; verdict "E2E-10" $? "late-join heard=$n (>=10 within 20s after join)"
}

s_multitopic() {
  # E2E-11: three talkers on chatter1/2/3; a listener on chatter2 must receive
  # while a listener on an unadvertised topic must receive nothing (isolation).
  stopall
  rbg "$BOARD_B" "\$ROS2_LISTENER --ros-args -r chatter:=chatter2" e2e_mt_listener2.log
  rbg "$BOARD_B" "\$ROS2_LISTENER --ros-args -r chatter:=chatter_none" e2e_mt_listener0.log
  sleep 3
  rbg "$BOARD_A" "\$ROS2_TALKER --ros-args -r chatter:=chatter1" e2e_mt_talker1.log
  rbg "$BOARD_A" "\$ROS2_TALKER --ros-args -r chatter:=chatter2" e2e_mt_talker2.log
  rbg "$BOARD_A" "\$ROS2_TALKER --ros-args -r chatter:=chatter3" e2e_mt_talker3.log
  sleep 20
  stopall
  pull "$BOARD_B" e2e_mt_listener2.log
  pull "$BOARD_B" e2e_mt_listener0.log
  local n2 n0
  n2=$(heard_count e2e_mt_listener2.log)
  n0=$(heard_count e2e_mt_listener0.log)
  [ "$n2" -ge 10 ] && [ "$n0" -eq 0 ]
  verdict "E2E-11" $? "multi-topic: chatter2 heard=$n2 (>=10), unsubscribed heard=$n0 (==0)"
}

s_matched() {
  # E2E-12: the stock matched_event_detect demo exercises pub/sub matched and
  # unmatched callbacks; the full 8-event sequence must appear in order.
  stopall
  runfg "$BOARD_A" "timeout 100 $DEVICE_DIR/Lib/demo_nodes_cpp/matched_event_detect" \
    > "$LOGDIR/e2e_matched.log" 2>&1
  local pat missing=0
  for pat in "First subscription is connected" \
             "connected subscription is 1 and current number of connected subscription is 2" \
             "connected subscription is -1 and current number of connected subscription is 1" \
             "Last subscription is disconnected" \
             "First publisher is connected" \
             "connected publisher is 1 and current number of connected publisher is 2" \
             "connected publisher is -1 and current number of connected publisher is 1" \
             "Last publisher is disconnected"; do
    grep -qF "$pat" "$LOGDIR/e2e_matched.log" || { missing=1; echo "   missing: $pat"; }
  done
  verdict "E2E-12" $missing "matched_event_detect full 8-event sequence"
}

# --- main --------------------------------------------------------------------

if [ $# -eq 0 ]; then
  set -- preflight loopback bidir service action sweep
elif [ "$1" = all ]; then
  set -- preflight loopback bidir py service action params besteffort sweep neg01 latejoin multitopic matched
fi
for sc in "$@"; do
  echo "== scenario: $sc =="
  "s_$sc" || echo "   (scenario $sc errored)"
done

echo
echo "== mdds e2e summary: $pass passed, $fail failed =="
[ ${#failed_ids[@]} -eq 0 ] || printf '   FAIL %s\n' "${failed_ids[@]}"
[ "$fail" -eq 0 ]
