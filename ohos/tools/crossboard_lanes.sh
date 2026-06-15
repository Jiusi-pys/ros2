#!/usr/bin/env bash
# Cross-board ROS 2 lane library (sourced by run_cross_board_full_matrix.sh).
#
# Each lane runs a fixture/server/publisher on one board and a peer
# client/subscriber on the OTHER board over the live eth1 link, then verifies
# the traffic crossed the wire. Everything is driven through the per-board
# launcher ($LAUNCH_A / $LAUNCH_B) so the SAME lane works for FastDDS
# (/usr/local/bin/ros2), CycloneDDS (/data/local/tmp/ohos-cyc/ros2-cyclone),
# and cross-vendor interop (A=fastdds, B=cyclone). Generalizes the
# run_direction pattern from run_cross_board_cli_pubsub.sh.
#
# Globals set by the orchestrator before calling run_crossboard_lanes:
#   DEVA DEVB         hdc device ids (A=server/publisher side, B=peer side)
#   LAUNCH_A LAUNCH_B absolute launcher path per board
#   DOM               ROS_DOMAIN_ID for this lane group
#   TAG               result prefix (fastdds | cyclonedds | interop)
# Emits: RESULT|<TAG>_<lane>|PASS|FAIL|NA|<evidence>
#
# Quoting model: command strings are caller-built bash double-quoted strings;
# YAML payloads use \"...\" so the variable holds LITERAL double quotes. dev_bg
# embeds the command inside  nohup sh -c '<env> <cmd> > log'  on the device, so
# the literal double quotes survive into the inner sh and keep YAML as one arg.

WORK=/data/local/tmp/xbf
RR="ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET"

emit() { echo "RESULT|${TAG}_$1|$2|$3"; }

# start background node: dev, logfile, full-launcher-cmd
dev_bg() {
  hdc -t "$1" shell "mkdir -p ${WORK}; rm -f $2; nohup sh -c 'ROS_DOMAIN_ID=${DOM} ${RR} $3 > $2 2>&1' >/dev/null 2>&1 &" >/dev/null 2>&1
}
# run foreground, echo combined output: dev, full-launcher-cmd
# Wrapped in a device-side timeout so a hung client (e.g. cross-vendor RPC that
# does not interoperate) can never stall the matrix.
dev_fg() {
  hdc -t "$1" shell "ROS_DOMAIN_ID=${DOM} ${RR} timeout ${FG_TIMEOUT:-20} $2 2>&1" 2>/dev/null
}
dev_kill() { hdc -t "$1" shell "ps -ef | grep '$2' | grep -v grep | while read u p r; do kill -9 \$p 2>/dev/null; done" >/dev/null 2>&1; }
dev_cat()  { hdc -t "$1" shell "cat $2 2>/dev/null" 2>/dev/null; }

# verdict from captured text: lane, text, pass-regex
verdict() {
  local lane="$1" got="$2" pat="$3"
  if echo "$got" | grep -qE "$pat"; then emit "$lane" PASS "$(echo "$got" | grep -m1 -E "$pat" | cut -c1-56 | tr -d '\r')"
  elif echo "$got" | grep -qiE "not found|inaccessible|No such file|UnsupportedType|Skipping|No executable found|Usage for"; then emit "$lane" NA "demo binary/typesupport not deployed"
  elif echo "$got" | grep -qiE "create_contentfilteredtopic"; then emit "$lane" NA "content-filtered topics not enabled in this RMW build"
  else emit "$lane" FAIL "$(echo "$got" | grep -vE '^[[:space:]]*$' | head -1 | cut -c1-64 | tr -d '\r')"; fi
}

# pub/sub style: pub on $1 (cmd $2, kill $3), sub on $4 (cmd $5, kill $6), hold $7, pat $8, lane $9
ps_lane() {
  local pdev="$1" pcmd="$2" pkill="$3" sdev="$4" scmd="$5" skill="$6" secs="$7" pat="$8" lane="$9"
  dev_bg "$sdev" "${WORK}/${lane}_s.log" "$scmd"; sleep 3
  dev_bg "$pdev" "${WORK}/${lane}_p.log" "$pcmd"; sleep "$secs"
  dev_kill "$pdev" "$pkill"; dev_kill "$sdev" "$skill"
  verdict "$lane" "$(dev_cat "$sdev" "${WORK}/${lane}_s.log")" "$pat"
}

# request style: server on $1 (cmd $2, kill $3), client fg on $4 (cmd $5), pat $6, lane $7
req_lane() {
  local srvdev="$1" srvcmd="$2" srvkill="$3" cldev="$4" clcmd="$5" pat="$6" lane="$7"
  dev_bg "$srvdev" "${WORK}/${lane}_srv.log" "$srvcmd"; sleep 5
  local out; out="$(dev_fg "$cldev" "$clcmd")"
  dev_kill "$srvdev" "$srvkill"
  verdict "$lane" "$out" "$pat"
}

############################ NAMED LANES ############################
# Servers/publishers on A ($DEVA/$LAUNCH_A); peers on B ($DEVB/$LAUNCH_B).

lane_pubsub_cpp_ab() { ps_lane "$DEVA" "$LAUNCH_A run demo_nodes_cpp talker" talker \
  "$DEVB" "$LAUNCH_B run demo_nodes_cpp listener" listener 10 "I heard" pubsub_cpp_ab; }
lane_pubsub_cpp_ba() { ps_lane "$DEVB" "$LAUNCH_B run demo_nodes_cpp talker" talker \
  "$DEVA" "$LAUNCH_A run demo_nodes_cpp listener" listener 10 "I heard" pubsub_cpp_ba; }

lane_pubsub_py_ab() { ps_lane \
  "$DEVA" "$LAUNCH_A topic pub /xb_py std_msgs/msg/String \"{data: hi_py}\"" "topic pub" \
  "$DEVB" "$LAUNCH_B topic echo /xb_py std_msgs/msg/String" "topic echo" 9 "data:.*hi_py" pubsub_py_ab; }

lane_complex_msg_ab() { ps_lane \
  "$DEVA" "$LAUNCH_A topic pub /xb_pose geometry_msgs/msg/PoseStamped \"{header: {frame_id: xb_geo}, pose: {position: {x: 7.0}}}\"" "topic pub" \
  "$DEVB" "$LAUNCH_B topic echo /xb_pose geometry_msgs/msg/PoseStamped" "topic echo" 9 "frame_id: xb_geo|x: 7\\.0" complex_msg_ab; }

lane_qos_best_effort_ab() { ps_lane "$DEVA" "$LAUNCH_A run demo_nodes_cpp talker" talker \
  "$DEVB" "$LAUNCH_B run demo_nodes_cpp listener_best_effort" listener_best_effort 10 "I heard" qos_best_effort_ab; }
lane_qos_reliable_ba() { ps_lane "$DEVB" "$LAUNCH_B run demo_nodes_cpp talker" talker \
  "$DEVA" "$LAUNCH_A run demo_nodes_cpp listener" listener 10 "I heard" qos_reliable_ba; }

lane_serialized_ab() { ps_lane "$DEVA" "$LAUNCH_A run demo_nodes_cpp talker_serialized_message" talker_serialized \
  "$DEVB" "$LAUNCH_B run demo_nodes_cpp listener_serialized_message" listener_serialized 10 "I heard|received" serialized_ab; }

lane_content_filter_ab() { ps_lane "$DEVA" "$LAUNCH_A run demo_nodes_cpp content_filtering_publisher" content_filtering_pub \
  "$DEVB" "$LAUNCH_B run demo_nodes_cpp content_filtering_subscriber" content_filtering_sub 12 "Received|temperature|emergency" content_filter_ab; }

lane_service_cpp() { req_lane "$DEVA" "$LAUNCH_A run demo_nodes_cpp add_two_ints_server" add_two_ints_server \
  "$DEVB" "$LAUNCH_B service call /add_two_ints example_interfaces/srv/AddTwoInts \"{a: 2, b: 3}\"" "sum=5|sum: 5" service_cpp; }

lane_action_cpp() { req_lane "$DEVA" "$LAUNCH_A run action_tutorials_cpp fibonacci_action_server" fibonacci_action_server \
  "$DEVB" "$LAUNCH_B action send_goal /fibonacci action_tutorials_interfaces/action/Fibonacci \"{order: 5}\"" "sequence|SUCCEEDED|Result" action_cpp; }

lane_lifecycle() { req_lane "$DEVA" "$LAUNCH_A run lifecycle lifecycle_talker" lifecycle_talker \
  "$DEVB" "$LAUNCH_B lifecycle set /lc_talker configure" "Transitioning successful|success" lifecycle; }

lane_params() { req_lane "$DEVA" "$LAUNCH_A run demo_nodes_cpp parameter_blackboard" parameter_blackboard \
  "$DEVB" "$LAUNCH_B param set /parameter_blackboard xb_p 3.5" "successful|Set parameter" params; }

lane_tf2() {
  local lane=tf2
  dev_bg "$DEVA" "${WORK}/${lane}_fx.log" "$LAUNCH_A run tf2_ros static_transform_publisher --x 1 --y 2 --z 3 --frame-id world --child-frame-id xb_tf"; sleep 4
  local q="${WORK}/${lane}_q.log"; dev_bg "$DEVB" "$q" "$LAUNCH_B run tf2_ros tf2_echo world xb_tf"; sleep 6
  dev_kill "$DEVB" "tf2_echo"; dev_kill "$DEVA" "static_transform_publisher"
  verdict "$lane" "$(dev_cat "$DEVB" "$q")" "Translation:|At time|- 1\\."
}

lane_bag_play() {
  local lane=bag_play slog="${WORK}/bag_play_s.log" bag="${WORK}/xbbag"
  dev_bg "$DEVB" "$slog" "$LAUNCH_B run demo_nodes_cpp listener"; sleep 2
  # record a talker into a bag on A
  hdc -t "$DEVA" shell "rm -rf ${bag}; nohup sh -c 'ROS_DOMAIN_ID=${DOM} ${RR} ${LAUNCH_A} run demo_nodes_cpp talker >/dev/null 2>&1' >/dev/null 2>&1 & sleep 1; ROS_DOMAIN_ID=${DOM} ${RR} ${LAUNCH_A} bag record -o ${bag} /chatter >/dev/null 2>&1 & sleep 6; ps -ef|grep -E '[b]ag record|[t]alker'|while read u p r; do kill -9 \$p 2>/dev/null; done" >/dev/null 2>&1
  # play it back into the live graph -> B listener should hear
  dev_fg "$DEVA" "$LAUNCH_A bag play ${bag}" >/dev/null 2>&1; sleep 2
  dev_kill "$DEVB" listener
  verdict "$lane" "$(dev_cat "$DEVB" "$slog")" "I heard"
}

# CLI graph introspection across the wire: talker on A, queries from B.
lane_cli_introspect() {
  dev_bg "$DEVA" "${WORK}/cli_fx.log" "$LAUNCH_A run demo_nodes_cpp talker"; sleep 5
  verdict cli_node_info  "$(dev_fg "$DEVB" "$LAUNCH_B node info /talker")"               "/chatter"
  verdict cli_topic_type "$(dev_fg "$DEVB" "$LAUNCH_B topic type /chatter")"             "std_msgs/msg/String"
  verdict cli_topic_find "$(dev_fg "$DEVB" "$LAUNCH_B topic find std_msgs/msg/String")"  "/chatter"
  verdict cli_topic_echo "$(dev_fg "$DEVB" "$LAUNCH_B topic echo /chatter --once")"      "data:"
  local h="${WORK}/cli_hz.log"; dev_bg "$DEVB" "$h" "$LAUNCH_B topic hz /chatter"; sleep 7; dev_kill "$DEVB" "topic hz"
  verdict cli_topic_hz "$(dev_cat "$DEVB" "$h")" "average rate"
  local b="${WORK}/cli_bw.log"; dev_bg "$DEVB" "$b" "$LAUNCH_B topic bw /chatter"; sleep 8; dev_kill "$DEVB" "topic bw"
  verdict cli_topic_bw "$(dev_cat "$DEVB" "$b")" "B/s|KB/s"
  dev_kill "$DEVA" talker
}

# Pub/sub-class lanes (RTPS topics) interoperate across DDS vendors.
run_crossboard_pubsub_lanes() {
  lane_pubsub_cpp_ab
  lane_pubsub_cpp_ba
  lane_pubsub_py_ab
  lane_complex_msg_ab
  lane_qos_best_effort_ab
  lane_qos_reliable_ba
  lane_serialized_ab
  lane_content_filter_ab
  lane_tf2
  lane_cli_introspect
  lane_bag_play
}

# Request/reply-class lanes (services, actions, lifecycle, parameters). These
# work same-vendor; cross-vendor (interop) RPC is NOT interoperable on this
# platform (the call hangs), so the orchestrator skips these in interop mode.
run_crossboard_rpc_lanes() {
  lane_service_cpp
  lane_action_cpp
  lane_lifecycle
  lane_params
}

run_crossboard_lanes() {
  run_crossboard_pubsub_lanes
  run_crossboard_rpc_lanes
}
