#!/bin/sh
# Board-side full-feature validation matrix for ROS 2 Jazzy on KaihongOS RK3588A.
# Runs every lane against PREFIX (standalone underlay) + OVERLAY (colcon rk3588a),
# emits one "RESULT|<lane>|PASS/FAIL|<evidence>" line per lane. Never aborts on
# a lane failure. Logs land in WORK_DIR.

PREFIX="${PREFIX:-/data/local/tmp/ohos-prefix}"
OVERLAY="${OVERLAY:-/data/local/tmp/ohos-colcon-rk3588a}"
TOOLS="${TOOLS:-/data/local/tmp/ros2-validate}"
WORK_DIR="${WORK_DIR:-/data/local/tmp/val}"
ROS2="${OVERLAY}/bin/ros2"
DOM_BASE="${DOM_BASE:-60}"
PY=/data/local/release/usr/bin/python3.12

mkdir -p "${WORK_DIR}"
rm -f "${WORK_DIR}"/*.log 2>/dev/null

CXX_LD="${PREFIX}/lib:/data/local/tmp:/data/local/release/usr/lib"
for d in "${PREFIX}"/opt/*/lib; do
  [ -d "$d" ] && CXX_LD="${CXX_LD}:$d"
done

export HOME=/data/local/tmp
export ROS_LOG_DIR=/data/local/tmp/roslogs
export AMENT_PREFIX_PATH="${PREFIX}"
mkdir -p "${ROS_LOG_DIR}"

result() {
  echo "RESULT|$1|$2|$3"
}

# kill every process whose command line matches $1 with signal $2 (default 9);
# device toybox lacks awk/pkill, so parse ps -ef with read
kill_pattern() {
  ps -ef | grep "$1" | grep -v grep | while read _u _p _rest; do
    kill -"${2:-9}" "${_p}" 2>/dev/null
  done
}

# run_cxx <domain> <log> <binary...>  — run a prefix C++ binary in background
run_cxx_bg() {
  dom="$1"; log="$2"; shift 2
  env LD_LIBRARY_PATH="${CXX_LD}" RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
      ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET ROS_DOMAIN_ID="${dom}" \
      HOME=/data/local/tmp ROS_LOG_DIR="${ROS_LOG_DIR}" \
      "$@" >"${log}" 2>&1 &
  echo $!
}

run_py_bg() {
  dom="$1"; log="$2"; shift 2
  env LD_PRELOAD=/data/local/release/usr/lib/libpython3.12.so.1.0 \
      PYTHONHOME=/data/local/release/usr \
      PYTHONPATH="${OVERLAY}/lib/python3.12/site-packages:${PREFIX}/lib/python3.12/site-packages" \
      LD_LIBRARY_PATH="${OVERLAY}/lib:${CXX_LD}" \
      RMW_IMPLEMENTATION=rmw_fastrtps_cpp ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET \
      ROS_DOMAIN_ID="${dom}" HOME=/data/local/tmp ROS_LOG_DIR="${ROS_LOG_DIR}" \
      "${PY}" "$@" >"${log}" 2>&1 &
  echo $!
}

ros2_dom() {
  dom="$1"; shift
  env ROS_DOMAIN_ID="${dom}" RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
      ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET "${ROS2}" "$@"
}

### 1. ros2 pkg list
n=$("${ROS2}" pkg list 2>/dev/null | wc -l)
if [ "${n}" -ge 190 ]; then result cli_pkg_list PASS "packages=${n}"; else result cli_pkg_list FAIL "packages=${n}"; fi

### 2. ros2 topic list --no-daemon
out=$(ros2_dom $((DOM_BASE+0)) topic list --no-daemon 2>&1)
case "${out}" in
  *"/parameter_events"*"/rosout"*|*"/rosout"*) result cli_topic_list PASS "graph_visible";;
  *) result cli_topic_list FAIL "${out}";;
esac

### 3. ros2 interface show (5 types)
ok=1; types=""
for t in "std_msgs/msg/String" "geometry_msgs/msg/Twist" "sensor_msgs/msg/Image" "nav_msgs/msg/Odometry" "tf2_msgs/msg/TFMessage"; do
  if ! "${ROS2}" interface show "$t" >/dev/null 2>&1; then ok=0; types="${types} $t"; fi
done
if "${ROS2}" interface show action_tutorials_interfaces/action/Fibonacci 2>/dev/null | grep -q "int32 order"; then :; else ok=0; types="${types} Fibonacci"; fi
if [ "${ok}" = 1 ]; then result cli_interface_show PASS "6_types"; else result cli_interface_show FAIL "missing:${types}"; fi

### 4. rclcpp pub/sub (demo_nodes_cpp talker + listener)
dom=$((DOM_BASE+1))
tp=$(run_cxx_bg ${dom} "${WORK_DIR}/talker.log" "${PREFIX}/lib/demo_nodes_cpp/talker")
lp=$(run_cxx_bg ${dom} "${WORK_DIR}/listener.log" "${PREFIX}/lib/demo_nodes_cpp/listener")
sleep 8; kill ${tp} ${lp} 2>/dev/null; wait ${tp} ${lp} 2>/dev/null
if grep -q "I heard" "${WORK_DIR}/listener.log"; then result rclcpp_pubsub PASS "$(grep -c 'I heard' "${WORK_DIR}/listener.log") msgs"; else result rclcpp_pubsub FAIL "no_receive"; fi

### 5. rclcpp service roundtrip
sh "${TOOLS}/run_rclcpp_service_roundtrip.sh" "${PREFIX}" $((DOM_BASE+2)) "${WORK_DIR}/svc_server.log" "${WORK_DIR}/svc_client.log" >/dev/null 2>&1
if grep -q "result of 41 + 1 = 42" "${WORK_DIR}/svc_client.log" 2>/dev/null; then result rclcpp_service PASS "41+1=42"; else result rclcpp_service FAIL "see svc_client.log"; fi

### 6. rclcpp action roundtrip (binary client)
sh "${TOOLS}/run_rclcpp_action_binary_roundtrip.sh" "${PREFIX}" $((DOM_BASE+3)) "${WORK_DIR}/act_server.log" "${WORK_DIR}/act_client.log" >/dev/null 2>&1
if grep -q "55" "${WORK_DIR}/act_client.log" 2>/dev/null && grep -qi "result" "${WORK_DIR}/act_client.log"; then result rclcpp_action PASS "fib_seq_to_55"; else result rclcpp_action FAIL "see act_client.log"; fi

### 7. lifecycle C++ (talker + service client driver)
dom=$((DOM_BASE+4))
ltp=$(run_cxx_bg ${dom} "${WORK_DIR}/lc_talker.log" "${PREFIX}/lib/lifecycle/lifecycle_talker")
sleep 3
lcp=$(run_cxx_bg ${dom} "${WORK_DIR}/lc_client.log" "${PREFIX}/lib/lifecycle/lifecycle_service_client")
sleep 25; kill ${ltp} ${lcp} 2>/dev/null; wait ${ltp} ${lcp} 2>/dev/null
if grep -q "on_activate() is called" "${WORK_DIR}/lc_talker.log" && grep -q "on_deactivate() is called" "${WORK_DIR}/lc_talker.log"; then result lifecycle_cpp PASS "full_transition"; else result lifecycle_cpp FAIL "see lc_talker.log"; fi

### 8. composition dlopen
sh "${TOOLS}/run_composition_dlopen_roundtrip.sh" "${PREFIX}" $((DOM_BASE+5)) "${WORK_DIR}/dlopen.log" >/dev/null 2>&1
if grep -q "I heard" "${WORK_DIR}/dlopen.log" 2>/dev/null; then result composition_dlopen PASS "talker_listener_in_proc"; else result composition_dlopen FAIL "see dlopen.log"; fi

### 9. component container + ros2 component CLI
sh "${TOOLS}/run_component_cli_roundtrip.sh" "${PREFIX}" $((DOM_BASE+6)) "${WORK_DIR}/cc_container.log" "${WORK_DIR}/cc_cli.log" >/dev/null 2>&1
if grep -q "composition::Talker" "${WORK_DIR}/cc_cli.log" 2>/dev/null || grep -q "/talker" "${WORK_DIR}/cc_cli.log" 2>/dev/null; then result component_cli PASS "load_list_ok"; else result component_cli FAIL "see cc_cli.log"; fi

### 10. tf2 static_transform_publisher + tf2_echo
sh "${TOOLS}/run_tf2_static_echo_roundtrip.sh" "${PREFIX}" $((DOM_BASE+7)) "${WORK_DIR}/tf_pub.log" "${WORK_DIR}/tf_echo.log" >/dev/null 2>&1
if grep -q "Translation: \[1.000, 2.000, 3.000\]" "${WORK_DIR}/tf_echo.log" 2>/dev/null; then result tf2_roundtrip PASS "translation_resolved"; else result tf2_roundtrip FAIL "see tf_echo.log"; fi

### 11. robot_state_publisher probe
sh "${TOOLS}/run_urdf_robot_state_publisher_probe.sh" "${PREFIX}" $((DOM_BASE+8)) "${WORK_DIR}/rsp" >"${WORK_DIR}/rsp_probe.log" 2>&1
if grep -rq "Translation: \[0.000, 0.000, 1.000\]" "${WORK_DIR}/rsp" 2>/dev/null || grep -q "Translation: \[0.000, 0.000, 1.000\]" "${WORK_DIR}/rsp_probe.log" 2>/dev/null; then result robot_state_publisher PASS "fixed_tf_resolved"; else result robot_state_publisher FAIL "see rsp_probe.log"; fi

### 12. pluginlib
out=$(env LD_LIBRARY_PATH="${CXX_LD}" "${PREFIX}/lib/pluginlib/list_plugins" urdf_parser_plugin urdf::URDFParser 2>&1)
case "${out}" in
  *URDFXMLParser*) result pluginlib PASS "urdf_xml_parser";;
  *) result pluginlib FAIL "${out}";;
esac

### 13. rclpy node + CLI graph/param
dom=$((DOM_BASE+9))
np=$(run_py_bg ${dom} "${WORK_DIR}/rclpy_node.log" "${TOOLS}/rclpy_cli_node.py")
sleep 6
nodes=$(ros2_dom ${dom} node list 2>/dev/null)
pset=$(ros2_dom ${dom} param set /rclpy_cli_node demo_text updated_by_validate 2>&1)
pget=$(ros2_dom ${dom} param get /rclpy_cli_node demo_text 2>&1)
echo_out=$(ros2_dom ${dom} topic echo /rclpy_cli_topic --once 2>&1 | head -3)
kill ${np} 2>/dev/null; wait ${np} 2>/dev/null
if echo "${nodes}" | grep -q "/rclpy_cli_node" && echo "${pget}" | grep -q "updated_by_validate" && echo "${echo_out}" | grep -q "data:"; then
  result rclpy_node_cli PASS "node+param+echo"
else
  result rclpy_node_cli FAIL "nodes=${nodes} pget=${pget}"
fi

### 14. rclpy service + ros2 service call
dom=$((DOM_BASE+10))
sp=$(run_py_bg ${dom} "${WORK_DIR}/rclpy_svc.log" "${TOOLS}/rclpy_cli_service.py")
sleep 6
call=$(ros2_dom ${dom} service call /rclpy_cli_trigger std_srvs/srv/Trigger '{}' 2>&1)
kill ${sp} 2>/dev/null; wait ${sp} 2>/dev/null
case "${call}" in
  *success=True*|*"success: true"*|*trigger_count*) result rclpy_service PASS "trigger_ok";;
  *) result rclpy_service FAIL "${call}";;
esac

### 15. rclpy action + ros2 action send_goal
dom=$((DOM_BASE+11))
ap=$(run_py_bg ${dom} "${WORK_DIR}/rclpy_act.log" "${TOOLS}/rclpy_cli_action.py")
sleep 8
ros2_dom ${dom} action send_goal --feedback /rclpy_cli_fibonacci example_interfaces/action/Fibonacci "{order: 5}" >"${WORK_DIR}/rclpy_goal.log" 2>&1
kill ${ap} 2>/dev/null; wait ${ap} 2>/dev/null
if grep -q "SUCCEEDED" "${WORK_DIR}/rclpy_goal.log"; then result rclpy_action PASS "fib_succeeded"; else result rclpy_action FAIL "$(tail -2 "${WORK_DIR}/rclpy_goal.log" | tr '\n' ' ')"; fi

### 16. demo_nodes_py pub + CLI echo
dom=$((DOM_BASE+12))
if [ -x "${PREFIX}/bin/demo_nodes_py__talker" ]; then PYTALKER="${PREFIX}/bin/demo_nodes_py__talker"; else PYTALKER="${PREFIX}/bin/talker"; fi
env ROS_DOMAIN_ID=${dom} "${PYTALKER}" >"${WORK_DIR}/py_talker.log" 2>&1 &
ptp=$!
sleep 6
pecho=$(ros2_dom ${dom} topic echo /chatter --once 2>&1 | head -2)
kill ${ptp} 2>/dev/null; wait ${ptp} 2>/dev/null
case "${pecho}" in
  *"Hello World"*) result demo_py_pubsub PASS "echo_ok";;
  *) result demo_py_pubsub FAIL "${pecho}";;
esac

### 17. demo_nodes_py add_two_ints service
dom=$((DOM_BASE+13))
if [ -x "${PREFIX}/bin/demo_nodes_py__add_two_ints_server" ]; then ATS="${PREFIX}/bin/demo_nodes_py__add_two_ints_server"; else ATS="${PREFIX}/bin/add_two_ints_server"; fi
if [ -x "${PREFIX}/bin/demo_nodes_py__add_two_ints_client" ]; then ATC="${PREFIX}/bin/demo_nodes_py__add_two_ints_client"; else ATC="${PREFIX}/bin/add_two_ints_client_async"; fi
env ROS_DOMAIN_ID=${dom} "${ATS}" >"${WORK_DIR}/ats.log" 2>&1 &
atsp=$!
sleep 5
env ROS_DOMAIN_ID=${dom} "${ATC}" >"${WORK_DIR}/atc.log" 2>&1 &
atcp=$!
sleep 12
kill ${atsp} ${atcp} 2>/dev/null; wait ${atsp} ${atcp} 2>/dev/null
if grep -qE "Result of add_two_ints: 5|: 5$" "${WORK_DIR}/atc.log" 2>/dev/null; then result demo_py_service PASS "2+3=5"; else result demo_py_service FAIL "see atc.log"; fi

### 18. lifecycle_py (python talker + C++ driver)
dom=$((DOM_BASE+14))
if [ -x "${PREFIX}/bin/lifecycle_py__lifecycle_talker" ]; then LCT="${PREFIX}/bin/lifecycle_py__lifecycle_talker"; else LCT="${PREFIX}/bin/lifecycle_talker"; fi
env ROS_DOMAIN_ID=${dom} "${LCT}" >"${WORK_DIR}/lcpy_talker.log" 2>&1 &
lcpyp=$!
sleep 5
lcdrv=$(run_cxx_bg ${dom} "${WORK_DIR}/lcpy_client.log" "${PREFIX}/lib/lifecycle/lifecycle_service_client")
sleep 25; kill ${lcpyp} ${lcdrv} 2>/dev/null; wait ${lcpyp} ${lcdrv} 2>/dev/null
if grep -q "on_activate() is called" "${WORK_DIR}/lcpy_talker.log"; then result lifecycle_py PASS "py_transitions"; else result lifecycle_py FAIL "see lcpy_talker.log"; fi

### 19. ros2 bag record/info (sqlite3)
dom=$((DOM_BASE+15))
rm -rf "${WORK_DIR}/bag_sqlite3"
tp2=$(run_cxx_bg ${dom} "${WORK_DIR}/bag_talker.log" "${PREFIX}/lib/demo_nodes_cpp/talker")
ros2_dom ${dom} bag record --storage sqlite3 --topics /chatter -o "${WORK_DIR}/bag_sqlite3" >"${WORK_DIR}/bag_rec.log" 2>&1 &
brp=$!
sleep 12
kill_pattern "bag record" INT; sleep 8; kill_pattern "bag record"; kill ${brp} ${tp2} 2>/dev/null; wait ${brp} ${tp2} 2>/dev/null
if [ ! -f "${WORK_DIR}/bag_sqlite3/metadata.yaml" ]; then
  ros2_dom ${dom} bag reindex "${WORK_DIR}/bag_sqlite3" -s sqlite3 >>"${WORK_DIR}/bag_rec.log" 2>&1
fi
info=$("${ROS2}" bag info "${WORK_DIR}/bag_sqlite3" 2>&1)
case "${info}" in
  *sqlite3*"std_msgs/msg/String"*) result bag_sqlite3 PASS "$(echo "${info}" | grep -o 'Messages:[^|]*' | head -1)";;
  *) result bag_sqlite3 FAIL "$(echo "${info}" | tail -2)";;
esac

### 20. ros2 bag record/info (mcap)
dom=$((DOM_BASE+16))
rm -rf "${WORK_DIR}/bag_mcap"
tp3=$(run_cxx_bg ${dom} "${WORK_DIR}/bag_talker2.log" "${PREFIX}/lib/demo_nodes_cpp/talker")
ros2_dom ${dom} bag record --storage mcap --topics /chatter -o "${WORK_DIR}/bag_mcap" >"${WORK_DIR}/bag_rec2.log" 2>&1 &
brp2=$!
sleep 12
kill_pattern "bag record" INT; sleep 8; kill_pattern "bag record"; kill ${brp2} ${tp3} 2>/dev/null; wait ${brp2} ${tp3} 2>/dev/null
if [ ! -f "${WORK_DIR}/bag_mcap/metadata.yaml" ]; then
  ros2_dom ${dom} bag reindex "${WORK_DIR}/bag_mcap" -s mcap >>"${WORK_DIR}/bag_rec2.log" 2>&1
fi
info2=$("${ROS2}" bag info "${WORK_DIR}/bag_mcap" 2>&1)
case "${info2}" in
  *mcap*"std_msgs/msg/String"*) result bag_mcap PASS "$(echo "${info2}" | grep -o 'Messages:[^|]*' | head -1)";;
  *) result bag_mcap FAIL "$(echo "${info2}" | tail -2)";;
esac

### 21. ros2 bag play + listener
dom=$((DOM_BASE+17))
lp2=$(run_cxx_bg ${dom} "${WORK_DIR}/play_listener.log" "${PREFIX}/lib/demo_nodes_cpp/listener")
ros2_dom ${dom} bag play "${WORK_DIR}/bag_sqlite3" >"${WORK_DIR}/bag_play.log" 2>&1 &
bpp=$!
sleep 12
kill_pattern "bag play"; kill ${bpp} ${lp2} 2>/dev/null; wait ${bpp} ${lp2} 2>/dev/null
if grep -q "I heard" "${WORK_DIR}/play_listener.log"; then result bag_play PASS "replay_received"; else result bag_play FAIL "see play_listener.log"; fi

### 22. ros2 launch talker_listener
dom=$((DOM_BASE+18))
env ROS_DOMAIN_ID=${dom} RMW_IMPLEMENTATION=rmw_fastrtps_cpp "${ROS2}" launch demo_nodes_cpp talker_listener_launch.py --noninteractive >"${WORK_DIR}/launch.log" 2>&1 &
lnp=$!
sleep 18
kill_pattern "talker_listener_launch\|demo_nodes_cpp/talker\|demo_nodes_cpp/listener" INT; sleep 2
kill_pattern "ros2 launch"; kill ${lnp} 2>/dev/null; wait ${lnp} 2>/dev/null
kill_pattern "demo_nodes_cpp/talker"; kill_pattern "demo_nodes_cpp/listener"
if grep -q "I heard" "${WORK_DIR}/launch.log"; then result ros2_launch PASS "launched_exchange"; else result ros2_launch FAIL "see launch.log"; fi

### 23. mixed: overlay CLI action send_goal vs underlay C++ action server (known-issue #2 retest)
dom=$((DOM_BASE+19))
asp=$(run_cxx_bg ${dom} "${WORK_DIR}/mixed_server.log" "${PREFIX}/lib/examples_rclcpp_minimal_action_server/action_server_not_composable")
sleep 4
mg=$(ros2_dom ${dom} action send_goal --feedback /fibonacci example_interfaces/action/Fibonacci "{order: 5}" 2>&1)
kill ${asp} 2>/dev/null; wait ${asp} 2>/dev/null
case "${mg}" in
  *SUCCEEDED*) result mixed_cli_cpp_action PASS "no_sigsegv";;
  *) result mixed_cli_cpp_action FAIL "$(echo "${mg}" | tail -2)";;
esac

### 24. ros2 run
dom=$((DOM_BASE+20))
env ROS_DOMAIN_ID=${dom} "${ROS2}" run demo_nodes_cpp talker >"${WORK_DIR}/ros2run.log" 2>&1 &
rrp=$!
sleep 8
kill ${rrp} 2>/dev/null; wait ${rrp} 2>/dev/null
kill_pattern "demo_nodes_cpp/talker"
if grep -q "Publishing" "${WORK_DIR}/ros2run.log"; then result ros2_run PASS "talker_via_run"; else result ros2_run FAIL "see ros2run.log"; fi

### 25. ros2 doctor
if "${ROS2}" doctor -h >/dev/null 2>&1; then result ros2_doctor PASS "cli_loads"; else result ros2_doctor FAIL "cli_error"; fi

# leave the board clean: stop CLI daemons and any stray validation nodes
kill_pattern "ros2-daemon"
kill_pattern "rclpy_cli_"
kill_pattern "bag record"

echo "VALIDATION_DONE"
