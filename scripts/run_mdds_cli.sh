#!/usr/bin/env bash
# L5 application-layer CLI suite: drives the REAL ros2 CLI verbs on board A
# (python3.12 Scripts/ros2-script.py, RMW pinned to rmw_mdds) against demo
# nodes on board B, to prove ROS 2 features end to end at the application
# level rather than via orchestrated demo binaries (test plan §L5).
#
#   ./scripts/run_mdds_cli.sh [scenario ...]
# scenarios: node topic echo hz bw pub param service action all
# default (all): every scenario in dependency order.
#
# Known platform limitation (not an rmw_mdds defect, cyclone control fails
# identically): `ros2 action send_goal`'s wait_for_server never fires on
# KaihongOS, so the action scenario asserts the graph data the verb depends
# on (publisher/service counts) instead of sending a goal via the CLI; the
# goal/result/feedback data path itself is covered by E2E-06.
set -uo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00
BOARD_B=3e01ff55454d202020104433991c3b00
DEVICE_DIR=/data/local/tmp/ros2
LOGROOT=ohos_test_logs/mdds_cli

export MSYS2_ARG_CONV_EXCL='*'

RENVS=". $DEVICE_DIR/env.sh || exit 70; export MDDS_TOKEN_EXEC=$DEVICE_DIR/bin/mdds_token_exec; export RMW_IMPLEMENTATION=rmw_mdds;"
CLI="python3.12 $DEVICE_DIR/Scripts/ros2-script.py"

shell()  { "$HDC" -t "$1" shell "$2" </dev/null; }
source scripts/lib/mdds_owned_processes.sh
mdds_owned_init run_mdds_cli "$BOARD_A" "$BOARD_B" || exit 3
LOGDIR="$LOGROOT/$MDDS_OWNED_RUN_ID"
mkdir -p "$LOGDIR"
rbg()    { mdds_owned_launch "$1" "$RENVS" "$2" "$3"; }
# cli <board> <timeout_s> <logname> <verb args...>; output lands in $LOGDIR
cli() {
  local b=$1 t=$2 log=$3; shift 3
  local args
  printf -v args '%q ' "$@"   # keep '{a: 5, b: 7}'-style args as one word remotely
  shell "$b" "$RENVS timeout $t \$MDDS_TOKEN_EXEC -- $CLI $args" > "$LOGDIR/$log" 2>&1
}

pass=0; fail=0; failed_ids=()
verdict() { # verdict <ID> <0|1> [detail]
  if [ "$2" -eq 0 ]; then echo "$1 PASS $3"; pass=$((pass+1));
  else echo "$1 FAIL $3"; fail=$((fail+1)); failed_ids+=("$1"); fi
}

setup_nodes() {
  rbg "$BOARD_B" "$DEVICE_DIR/lib/demo_nodes_cpp/talker" cli_talker.log
  rbg "$BOARD_B" "$DEVICE_DIR/lib/demo_nodes_cpp/listener --ros-args -r chatter:=chatter_cli" cli_listener.log
  rbg "$BOARD_B" "$DEVICE_DIR/lib/demo_nodes_cpp/parameter_blackboard" cli_param_node.log
  rbg "$BOARD_B" "$DEVICE_DIR/lib/demo_nodes_cpp/add_two_ints_server" cli_srv_node.log
  rbg "$BOARD_B" "$DEVICE_DIR/lib/action_tutorials_cpp/fibonacci_action_server" cli_act_node.log
  # CLI-05 probe helper
  "$HDC" -t "$BOARD_A" file send "$(cygpath -w scripts/mdds_e2e/cli_probe_action.py)" \
    "$DEVICE_DIR/mdds_e2e/cli_probe_action.py" </dev/null >/dev/null
  sleep 6
}

teardown_nodes() {
  mdds_owned_stop_all
}

# --- scenarios ---------------------------------------------------------------

s_node() {
  cli "$BOARD_A" 30 cli01_node_list.log node list
  cli "$BOARD_A" 30 cli02_node_info.log node info /talker
  local bad=0
  grep -qx '/talker' "$LOGDIR/cli01_node_list.log" || { echo "   node list missing /talker"; bad=1; }
  grep -q '/chatter: std_msgs/msg/String' "$LOGDIR/cli02_node_info.log" || { echo "   node info missing chatter publisher"; bad=1; }
  grep -q '/talker/get_parameters' "$LOGDIR/cli02_node_info.log" || { echo "   node info missing param services"; bad=1; }
  verdict "CLI-01" $bad "node list/info (graph introspection, cross-board)"
}

s_topic() {
  cli "$BOARD_A" 30 cli03_topic_list.log topic list -t
  cli "$BOARD_A" 30 cli03_topic_info.log topic info /chatter
  local bad=0
  grep -q '/chatter \[std_msgs/msg/String\]' "$LOGDIR/cli03_topic_list.log" || { echo "   topic list missing /chatter"; bad=1; }
  grep -q 'Type: std_msgs/msg/String' "$LOGDIR/cli03_topic_info.log" || { echo "   topic info missing type"; bad=1; }
  grep -q 'Publisher count: 1' "$LOGDIR/cli03_topic_info.log" || { echo "   topic info publisher count != 1"; bad=1; }
  verdict "CLI-02" $bad "topic list/info (type + publisher count)"
}

s_echo() {
  cli "$BOARD_A" 40 cli04_echo.log topic echo --once /chatter
  grep -q "data: 'Hello World:" "$LOGDIR/cli04_echo.log"
  verdict "CLI-03" $? "topic echo --once received cross-board sample"
}

s_hz() {
  cli "$BOARD_A" 20 cli05_hz.log topic hz /chatter
  local rate
  rate=$(grep -m1 'average rate:' "$LOGDIR/cli05_hz.log" | sed 's/.*average rate: //')
  [ -n "$rate" ] && awk -v r="$rate" 'BEGIN{exit !(r>0.9 && r<1.1)}'
  verdict "CLI-04" $? "topic hz average rate=${rate:-none} (talker 1Hz, window 0.9-1.1)"
}

s_bw() {
  cli "$BOARD_A" 20 cli06_bw.log topic bw /chatter
  grep -q 'Message size mean' "$LOGDIR/cli06_bw.log"
  verdict "CLI-05" $? "topic bw reported message-size stats"
}

s_pub() {
  local marker="cli_probe_$$"
  cli "$BOARD_A" 40 cli07_pub.log topic pub --once /chatter_cli std_msgs/msg/String "{data: $marker}"
  sleep 3
  shell "$BOARD_B" "grep -c '$marker' '$MDDS_OWNED_REMOTE_DIR/cli_listener.log' 2>/dev/null || true" | grep -q '^[1-9]'
  verdict "CLI-06" $? "topic pub --once reached B listener (marker=$marker)"
}

s_param() {
  cli "$BOARD_A" 30 cli08_param_get1.log param get /parameter_blackboard use_sim_time
  cli "$BOARD_A" 30 cli08_param_set.log param set /parameter_blackboard use_sim_time true
  cli "$BOARD_A" 30 cli08_param_get2.log param get /parameter_blackboard use_sim_time
  cli "$BOARD_A" 30 cli08_param_restore.log param set /parameter_blackboard use_sim_time false
  local bad=0
  grep -q 'Boolean value is: False' "$LOGDIR/cli08_param_get1.log" || { echo "   get1 not False"; bad=1; }
  grep -q 'Set parameter successful' "$LOGDIR/cli08_param_set.log" || { echo "   set failed"; bad=1; }
  grep -q 'Boolean value is: True' "$LOGDIR/cli08_param_get2.log" || { echo "   get2 not True"; bad=1; }
  verdict "CLI-07" $bad "param get/set/get round trip (use_sim_time)"
}

s_service() {
  cli "$BOARD_A" 30 cli09_service_list.log service list
  cli "$BOARD_A" 40 cli09_service_call.log service call /add_two_ints example_interfaces/srv/AddTwoInts '{a: 5, b: 7}'
  local bad=0
  grep -qx '/add_two_ints' "$LOGDIR/cli09_service_list.log" || { echo "   service list missing /add_two_ints"; bad=1; }
  grep -q 'sum=12' "$LOGDIR/cli09_service_call.log" || { echo "   service call wrong response"; bad=1; }
  verdict "CLI-08" $bad "service list + call add_two_ints(5,7)=12"
}

s_action() {
  cli "$BOARD_A" 30 cli10_action_list.log action list -t
  shell "$BOARD_A" "$RENVS timeout 30 \$MDDS_TOKEN_EXEC -- python3.12 $DEVICE_DIR/mdds_e2e/cli_probe_action.py" \
    > "$LOGDIR/cli10_action_probe.log" 2>&1
  local bad=0
  grep -q '/fibonacci \[action_tutorials_interfaces/action/Fibonacci\]' \
    "$LOGDIR/cli10_action_list.log" || { echo "   action list missing /fibonacci"; bad=1; }
  grep -q 'PROBE_RESULT PASS' "$LOGDIR/cli10_action_probe.log" || { echo "   action graph probe failed"; bad=1; }
  verdict "CLI-09" $bad "action list + graph completeness probe (send_goal CLI = platform issue)"
}

# --- driver ------------------------------------------------------------------

trap 'rc=$?; trap - EXIT; mdds_owned_finish >/dev/null 2>&1 || rc=1; exit $rc' EXIT

ids=("$@")
[ ${#ids[@]} -eq 0 ] && ids=(all)
if [ "${ids[0]}" = "all" ]; then
  ids=(node topic echo hz bw pub param service action)
fi

setup_nodes
for sc in "${ids[@]}"; do
  echo "== scenario: $sc =="
  "s_$sc" || echo "   (scenario $sc errored)"
done
teardown_nodes

echo
echo "== mdds cli summary: $pass passed, $fail failed =="
[ $fail -eq 0 ]
