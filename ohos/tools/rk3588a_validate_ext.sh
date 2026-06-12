#!/bin/sh
# Phase-2 extended ROS 2 feature matrix for RK3588A/OHOS. Covers the feature
# surface NOT in rk3588a_validate_all.sh: QoS policies, full parameter API,
# timers, serialized/loaned messages, content filtering, service introspection,
# wait sets, topic statistics, logging, rclpy executors/callback-groups/guard
# conditions, action cancel/async, transports, deeper ros2 CLI, and rosbag2
# burst/reindex/convert/compression.
#
# Emits one "RESULT|<lane>|PASS/FAIL|<evidence>" line per lane. Never aborts.

PREFIX="${PREFIX:-/data/local/tmp/ohos-prefix}"
OVERLAY="${OVERLAY:-/data/local/tmp/ohos-colcon-rk3588a}"
WORK_DIR="${WORK_DIR:-/data/local/tmp/valext}"
ROS2="${OVERLAY}/bin/ros2"
DOM_BASE="${DOM_BASE:-160}"
PY=/data/local/release/usr/bin/python3.12

mkdir -p "${WORK_DIR}"
rm -f "${WORK_DIR}"/*.log 2>/dev/null

# ROS 2 domain IDs must stay in [0,232]; this matrix uses up to DOM_BASE+39.
# Guard against an out-of-range DOM_BASE (FastDDS rejects domains > 232 with
# "Calculated port number is too high"), which would fail every lane.
if [ "${DOM_BASE}" -gt 193 ]; then
  echo "RESULT|_dom_base_guard|FAIL|DOM_BASE=${DOM_BASE} exceeds 193 (domain+39 must be <=232)"
  echo "EXT_VALIDATION_DONE"
  exit 1
fi

export HOME=/data/local/tmp
export ROS_LOG_DIR=/data/local/tmp/roslogs
export AMENT_PREFIX_PATH="${PREFIX}"
export ROS_DISTRO=jazzy
mkdir -p "${ROS_LOG_DIR}"

CXX_LD="${PREFIX}/lib:/data/local/tmp:/data/local/release/usr/lib"
for d in "${PREFIX}"/opt/*/lib; do [ -d "$d" ] && CXX_LD="${CXX_LD}:$d"; done

result() { echo "RESULT|$1|$2|$3"; }

kill_pattern() {
  ps -ef | grep "$1" | grep -v grep | while read _u _p _rest; do
    kill -"${2:-9}" "${_p}" 2>/dev/null
  done
}

# run_cxx <domain> <log> <binary...>  → pid
run_cxx_bg() {
  dom="$1"; log="$2"; shift 2
  env LD_LIBRARY_PATH="${CXX_LD}" RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
      ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET ROS_DOMAIN_ID="${dom}" \
      HOME=/data/local/tmp ROS_LOG_DIR="${ROS_LOG_DIR}" \
      "$@" >"${log}" 2>&1 &
  echo $!
}

# run installed python wrapper (self-contained env) in bg → pid
run_wrap_bg() {
  dom="$1"; log="$2"; shift 2
  env ROS_DOMAIN_ID="${dom}" RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
      ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET "$@" >"${log}" 2>&1 &
  echo $!
}

ros2_dom() {
  dom="$1"; shift
  env ROS_DOMAIN_ID="${dom}" RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
      ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET "${ROS2}" "$@"
}

# cli_timed <domain> <seconds> <log> <ros2 args...>  — run a long-lived ros2
# CLI command, capture output, then stop it
cli_timed() {
  dom="$1"; secs="$2"; log="$3"; shift 3
  env ROS_DOMAIN_ID="${dom}" RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
      ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET "${ROS2}" "$@" >"${log}" 2>&1 &
  cp=$!
  sleep "${secs}"
  kill "${cp}" 2>/dev/null; wait "${cp}" 2>/dev/null
}

QD() { echo "${PREFIX}/lib/quality_of_service_demo_cpp/$1"; }
DN() { echo "${PREFIX}/lib/demo_nodes_cpp/$1"; }
n=0

###########################################################################
# QoS policy lanes (quality_of_service_demo_cpp)
###########################################################################

# 1. message lost reporting
dom=$((DOM_BASE+0))
tp=$(run_cxx_bg ${dom} "${WORK_DIR}/qos_ml_t.log" "$(QD message_lost_talker)")
lp=$(run_cxx_bg ${dom} "${WORK_DIR}/qos_ml_l.log" "$(QD message_lost_listener)")
sleep 10; kill ${tp} ${lp} 2>/dev/null; wait ${tp} ${lp} 2>/dev/null
if grep -qi "I heard an image" "${WORK_DIR}/qos_ml_l.log"; then result ext_qos_message_lost PASS "latency_reported"; else result ext_qos_message_lost FAIL "see qos_ml_l.log"; fi

# 2. incompatible QoS event callbacks (positional policy name required)
dom=$((DOM_BASE+1))
ip=$(run_cxx_bg ${dom} "${WORK_DIR}/qos_inc.log" "$(QD incompatible_qos)" reliability)
sleep 9; kill ${ip} 2>/dev/null; kill_pattern "incompatible_qos"; wait ${ip} 2>/dev/null
if grep -qi "incompatible qos" "${WORK_DIR}/qos_inc.log"; then result ext_qos_incompatible PASS "event_fired"; else result ext_qos_incompatible FAIL "see qos_inc.log"; fi

# 3. deadline  (deadline_duration_ms [--publish-for ms] [--pause-for ms])
dom=$((DOM_BASE+2))
dp=$(run_cxx_bg ${dom} "${WORK_DIR}/qos_dl.log" "$(QD deadline)" 500 --publish-for 4000 --pause-for 1000)
sleep 9; kill ${dp} 2>/dev/null; kill_pattern "lib/quality_of_service_demo_cpp/deadline"; wait ${dp} 2>/dev/null
if grep -qiE "deadline|Publishing|Last|sample" "${WORK_DIR}/qos_dl.log"; then result ext_qos_deadline PASS "ran"; else result ext_qos_deadline FAIL "see qos_dl.log"; fi

# 4. lifespan  (lifespan_duration_ms [--publish-count n] [--subscribe-after ms])
dom=$((DOM_BASE+3))
lsp=$(run_cxx_bg ${dom} "${WORK_DIR}/qos_ls.log" "$(QD lifespan)" 500 --publish-count 5 --subscribe-after 1000)
sleep 9; kill ${lsp} 2>/dev/null; kill_pattern "lib/quality_of_service_demo_cpp/lifespan"; wait ${lsp} 2>/dev/null
if grep -qiE "Publishing|sample|lifespan|received" "${WORK_DIR}/qos_ls.log"; then result ext_qos_lifespan PASS "ran"; else result ext_qos_lifespan FAIL "see qos_ls.log"; fi

# 5. liveliness  (lease_duration_ms [--topic-assert-period ms] [--kill-publisher-after ms])
dom=$((DOM_BASE+4))
lvp=$(run_cxx_bg ${dom} "${WORK_DIR}/qos_lv.log" "$(QD liveliness)" 1000 --topic-assert-period 200 --kill-publisher-after 2500)
sleep 9; kill ${lvp} 2>/dev/null; kill_pattern "lib/quality_of_service_demo_cpp/liveliness"; wait ${lvp} 2>/dev/null
if grep -qiE "liveliness|alive|Publishing|not-alive" "${WORK_DIR}/qos_lv.log"; then result ext_qos_liveliness PASS "ran"; else result ext_qos_liveliness FAIL "see qos_lv.log"; fi

# 6. QoS overrides
dom=$((DOM_BASE+5))
ot=$(run_cxx_bg ${dom} "${WORK_DIR}/qos_ov_t.log" "$(QD qos_overrides_talker)")
ol=$(run_cxx_bg ${dom} "${WORK_DIR}/qos_ov_l.log" "$(QD qos_overrides_listener)")
sleep 9; kill ${ot} ${ol} 2>/dev/null; wait ${ot} ${ol} 2>/dev/null
if grep -qi "I heard" "${WORK_DIR}/qos_ov_l.log"; then result ext_qos_overrides PASS "received"; else result ext_qos_overrides FAIL "see qos_ov_l.log"; fi

# 7. best-effort reliability
dom=$((DOM_BASE+6))
bt=$(run_cxx_bg ${dom} "${WORK_DIR}/be_t.log" "$(DN talker)")
bl=$(run_cxx_bg ${dom} "${WORK_DIR}/be_l.log" "$(DN listener_best_effort)")
sleep 8; kill ${bt} ${bl} 2>/dev/null; wait ${bt} ${bl} 2>/dev/null
if grep -qi "I heard" "${WORK_DIR}/be_l.log"; then result ext_qos_best_effort PASS "received"; else result ext_qos_best_effort FAIL "see be_l.log"; fi

###########################################################################
# Parameter API lanes (demo_nodes_cpp)
###########################################################################

# 8. set_and_get_parameters
dom=$((DOM_BASE+7))
run_cxx_bg ${dom} "${WORK_DIR}/p_sg.log" "$(DN set_and_get_parameters)" >/dev/null
sleep 6; kill_pattern "set_and_get_parameters"
if grep -qiE "Parameter|foo|=" "${WORK_DIR}/p_sg.log"; then result ext_param_set_get PASS "ran"; else result ext_param_set_get FAIL "see p_sg.log"; fi

# 9. list_parameters
dom=$((DOM_BASE+8))
run_cxx_bg ${dom} "${WORK_DIR}/p_list.log" "$(DN list_parameters)" >/dev/null
sleep 6; kill_pattern "lib/demo_nodes_cpp/list_parameters"
if grep -qiE "Parameter|foo|bar" "${WORK_DIR}/p_list.log"; then result ext_param_list PASS "ran"; else result ext_param_list FAIL "see p_list.log"; fi

# 10. parameter event handler (node name is "this_node"; cb1 watches an_int_param)
dom=$((DOM_BASE+9))
peh=$(run_cxx_bg ${dom} "${WORK_DIR}/p_evh.log" "$(DN parameter_event_handler)")
sleep 5
ros2_dom ${dom} param set /this_node an_int_param 21 --no-daemon >/dev/null 2>&1
sleep 3; kill ${peh} 2>/dev/null; kill_pattern "parameter_event_handler"; wait ${peh} 2>/dev/null
if grep -qE "cb1: Received an update to parameter" "${WORK_DIR}/p_evh.log"; then result ext_param_event_handler PASS "callback_fired"; else result ext_param_event_handler FAIL "see p_evh.log"; fi

# 11. parameter blackboard + ros2 param CLI (set/get/list/dump)
dom=$((DOM_BASE+10))
pb=$(run_cxx_bg ${dom} "${WORK_DIR}/p_bb.log" "$(DN parameter_blackboard)")
sleep 4
ros2_dom ${dom} param set /parameter_blackboard test_p 1.5 >"${WORK_DIR}/p_cli.log" 2>&1
ros2_dom ${dom} param get /parameter_blackboard test_p >>"${WORK_DIR}/p_cli.log" 2>&1
ros2_dom ${dom} param list /parameter_blackboard >>"${WORK_DIR}/p_cli.log" 2>&1
ros2_dom ${dom} param dump /parameter_blackboard >>"${WORK_DIR}/p_cli.log" 2>&1
kill ${pb} 2>/dev/null; kill_pattern "parameter_blackboard"; wait ${pb} 2>/dev/null
if grep -q "1.5" "${WORK_DIR}/p_cli.log"; then result ext_param_blackboard_cli PASS "set_get_dump"; else result ext_param_blackboard_cli FAIL "$(tail -2 "${WORK_DIR}/p_cli.log" | tr '\n' ' ')"; fi

# 12. set_parameters_callback (unique node)
dom=$((DOM_BASE+11))
# node name is set_param_callback_node (not the binary name)
spc=$(run_cxx_bg ${dom} "${WORK_DIR}/p_spc.log" "$(DN set_parameters_callback)")
sleep 7
ros2_dom ${dom} param set /set_param_callback_node param1 1.0 --no-daemon >/dev/null 2>&1
sleep 2
p2=$(ros2_dom ${dom} param get /set_param_callback_node param2 --no-daemon 2>&1)
kill ${spc} 2>/dev/null; kill_pattern "set_parameters_callback"; wait ${spc} 2>/dev/null
case "${p2}" in *4.0*) result ext_param_callback PASS "param2=4.0";; *) result ext_param_callback FAIL "${p2}";; esac

###########################################################################
# Timers / serialized / loaned / content-filter / introspection / events
###########################################################################

# 13. one-off + reuse timer
dom=$((DOM_BASE+12))
run_cxx_bg ${dom} "${WORK_DIR}/t_oneoff.log" "$(DN one_off_timer)" >/dev/null
sleep 6; kill_pattern "one_off_timer"
if grep -qiE "timer|callback|cancel" "${WORK_DIR}/t_oneoff.log"; then result ext_timer_oneoff PASS "ran"; else result ext_timer_oneoff FAIL "see t_oneoff.log"; fi

# 14. serialized message pub/sub
dom=$((DOM_BASE+13))
st=$(run_cxx_bg ${dom} "${WORK_DIR}/ser_t.log" "$(DN talker_serialized_message)")
sl=$(run_cxx_bg ${dom} "${WORK_DIR}/ser_l.log" "$(DN listener_serialized_message)")
sleep 8; kill ${st} ${sl} 2>/dev/null; wait ${st} ${sl} 2>/dev/null
if grep -qi "I heard" "${WORK_DIR}/ser_l.log"; then result ext_serialized_msg PASS "received"; else result ext_serialized_msg FAIL "see ser_l.log"; fi

# 15. loaned message publish
dom=$((DOM_BASE+14))
ltk=$(run_cxx_bg ${dom} "${WORK_DIR}/loaned.log" "$(DN talker_loaned_message)")
sleep 6; kill ${ltk} 2>/dev/null; kill_pattern "talker_loaned_message"; wait ${ltk} 2>/dev/null
if grep -qiE "Publishing|loaned" "${WORK_DIR}/loaned.log"; then result ext_loaned_msg PASS "published"; else result ext_loaned_msg FAIL "see loaned.log"; fi

# 16. content filtering pub/sub
dom=$((DOM_BASE+15))
cfp=$(run_cxx_bg ${dom} "${WORK_DIR}/cf_p.log" "$(DN content_filtering_publisher)")
cfs=$(run_cxx_bg ${dom} "${WORK_DIR}/cf_s.log" "$(DN content_filtering_subscriber)")
sleep 10; kill ${cfp} ${cfs} 2>/dev/null; wait ${cfp} ${cfs} 2>/dev/null
if grep -qiE "I heard|Received|filter" "${WORK_DIR}/cf_s.log"; then result ext_content_filter PASS "received"; else result ext_content_filter FAIL "see cf_s.log"; fi

# 17. service introspection (_service_event). The C++ demo defaults to
# 'disabled'; enable it by setting the service_configure_introspection param,
# then the node publishes events to /add_two_ints/_service_event.
dom=$((DOM_BASE+16))
isv=$(run_cxx_bg ${dom} "${WORK_DIR}/intro_s.log" "$(DN introspection_service)")
icl=$(run_cxx_bg ${dom} "${WORK_DIR}/intro_c.log" "$(DN introspection_client)")
sleep 4
ros2_dom ${dom} param set /introspection_service service_configure_introspection metadata >/dev/null 2>&1
sleep 1
ros2_dom ${dom} topic echo --once /add_two_ints/_service_event >"${WORK_DIR}/intro_event.log" 2>&1
kill ${isv} ${icl} 2>/dev/null; kill_pattern "introspection_"; wait ${isv} ${icl} 2>/dev/null
if grep -qiE "event_type|client_gid|sequence_number" "${WORK_DIR}/intro_event.log" 2>/dev/null; then result ext_service_introspection PASS "service_event_metadata"; else result ext_service_introspection FAIL "$(grep -vE '^[[:space:]]*$' "${WORK_DIR}/intro_event.log" 2>/dev/null | head -2 | tr '\n' ' ')"; fi

# 18. matched event detect
dom=$((DOM_BASE+17))
me=$(run_cxx_bg ${dom} "${WORK_DIR}/matched.log" "$(DN matched_event_detect)")
sleep 8; kill ${me} 2>/dev/null; kill_pattern "matched_event_detect"; wait ${me} 2>/dev/null
if grep -qiE "connected|matched|subscription" "${WORK_DIR}/matched.log"; then result ext_matched_event PASS "event_seen"; else result ext_matched_event FAIL "see matched.log"; fi

###########################################################################
# Logging / wait-set / topic statistics
###########################################################################

# 19. logging demo (severity transitions)
dom=$((DOM_BASE+18))
lg=$(run_cxx_bg ${dom} "${WORK_DIR}/logdemo.log" "${PREFIX}/lib/logging_demo/logging_demo_main")
sleep 9; kill ${lg} 2>/dev/null; kill_pattern "logging_demo_main"; wait ${lg} 2>/dev/null
if grep -qiE "logger|DEBUG|severity" "${WORK_DIR}/logdemo.log"; then result ext_logging_demo PASS "severity_output"; else result ext_logging_demo FAIL "see logdemo.log"; fi

# 20. logger service (runtime level changes)
dom=$((DOM_BASE+19))
ul=$(run_cxx_bg ${dom} "${WORK_DIR}/loggersvc.log" "$(DN use_logger_service)")
sleep 9; kill ${ul} 2>/dev/null; kill_pattern "use_logger_service"; wait ${ul} 2>/dev/null
if grep -qiE "DEBUG logger level|WARN logger level|ERROR logger level" "${WORK_DIR}/loggersvc.log"; then result ext_logger_service PASS "level_transitions"; else result ext_logger_service FAIL "see loggersvc.log"; fi

# 21. wait-set talker/listener
dom=$((DOM_BASE+20))
wt=$(run_cxx_bg ${dom} "${WORK_DIR}/ws_t.log" "${PREFIX}/lib/examples_rclcpp_wait_set/wait_set_talker")
wl=$(run_cxx_bg ${dom} "${WORK_DIR}/ws_l.log" "${PREFIX}/lib/examples_rclcpp_wait_set/wait_set_listener")
sleep 9; kill ${wt} ${wl} 2>/dev/null; wait ${wt} ${wl} 2>/dev/null
if grep -qi "I heard" "${WORK_DIR}/ws_l.log"; then result ext_wait_set PASS "received"; else result ext_wait_set FAIL "see ws_l.log"; fi

# 22. minimal subscriber wait-set (subscribes /topic, not /chatter)
dom=$((DOM_BASE+21))
mws=$(run_cxx_bg ${dom} "${WORK_DIR}/mws.log" "${PREFIX}/lib/examples_rclcpp_minimal_subscriber/wait_set_subscriber")
sleep 3
( i=0; while [ $i -lt 12 ]; do
    env ROS_DOMAIN_ID=${dom} RMW_IMPLEMENTATION=rmw_fastrtps_cpp "${ROS2}" topic pub --once /topic std_msgs/msg/String "{data: ws_msg_${i}}" >/dev/null 2>&1
    i=$((i+1))
  done ) >/dev/null 2>&1 &
pubp=$!
sleep 9; kill ${mws} ${pubp} 2>/dev/null; kill_pattern "wait_set_subscriber"; kill_pattern "topic pub --once /topic"; wait ${mws} 2>/dev/null
if grep -qi "I heard" "${WORK_DIR}/mws.log"; then result ext_wait_set_sub PASS "received"; else result ext_wait_set_sub FAIL "see mws.log"; fi

# 23. topic statistics
dom=$((DOM_BASE+22))
ts=$(run_cxx_bg ${dom} "${WORK_DIR}/topicstats.log" "${PREFIX}/lib/topic_statistics_demo/display_topic_statistics" string --publish-period 1000)
sleep 10; kill ${ts} 2>/dev/null; kill_pattern "display_topic_statistics"; wait ${ts} 2>/dev/null
if grep -qiE "message_age|message_period|Metric" "${WORK_DIR}/topicstats.log"; then result ext_topic_statistics PASS "metrics_published"; else result ext_topic_statistics FAIL "see topicstats.log"; fi

###########################################################################
# rclpy executors / callback groups / guard conditions / action variants
###########################################################################

# 24. rclpy executors talker/listener
dom=$((DOM_BASE+23))
et=$(run_wrap_bg ${dom} "${WORK_DIR}/rpe_t.log" "${PREFIX}/bin/examples_rclpy_executors__talker")
el=$(run_wrap_bg ${dom} "${WORK_DIR}/rpe_l.log" "${PREFIX}/bin/examples_rclpy_executors__listener")
sleep 9; kill ${et} ${el} 2>/dev/null; kill_pattern "examples_rclpy_executors"; wait ${et} ${el} 2>/dev/null
if grep -qi "I heard" "${WORK_DIR}/rpe_l.log"; then result ext_rclpy_executors PASS "received"; else result ext_rclpy_executors FAIL "see rpe_l.log"; fi

# 25. rclpy callback group (single process)
dom=$((DOM_BASE+24))
cg=$(run_wrap_bg ${dom} "${WORK_DIR}/rpe_cg.log" "${PREFIX}/bin/examples_rclpy_executors__callback_group")
sleep 9; kill ${cg} 2>/dev/null; kill_pattern "callback_group"; wait ${cg} 2>/dev/null
if grep -qiE "Publishing|I heard" "${WORK_DIR}/rpe_cg.log"; then result ext_rclpy_callback_group PASS "ran"; else result ext_rclpy_callback_group FAIL "see rpe_cg.log"; fi

# 26. rclpy guard condition
dom=$((DOM_BASE+25))
gc=$(run_wrap_bg ${dom} "${WORK_DIR}/rpe_gc.log" "${PREFIX}/bin/examples_rclpy_guard_conditions__trigger_guard_condition")
sleep 7; kill ${gc} 2>/dev/null; kill_pattern "trigger_guard_condition"; wait ${gc} 2>/dev/null
if grep -qiE "guard|trigger|shutdown" "${WORK_DIR}/rpe_gc.log"; then result ext_rclpy_guard PASS "triggered"; else result ext_rclpy_guard FAIL "see rpe_gc.log"; fi

# 27. rclpy action cancel
dom=$((DOM_BASE+26))
asv=$(run_wrap_bg ${dom} "${WORK_DIR}/rpa_s.log" "${PREFIX}/bin/examples_rclpy_minimal_action_server__server")
sleep 4
acl=$(run_wrap_bg ${dom} "${WORK_DIR}/rpa_c.log" "${PREFIX}/bin/examples_rclpy_minimal_action_client__client_cancel")
sleep 8; kill ${asv} ${acl} 2>/dev/null; kill_pattern "examples_rclpy_minimal_action"; wait ${asv} ${acl} 2>/dev/null
if grep -qiE "cancel|Goal canceled|canceled" "${WORK_DIR}/rpa_c.log"; then result ext_rclpy_action_cancel PASS "canceled"; else result ext_rclpy_action_cancel FAIL "see rpa_c.log"; fi

# 28. action_tutorials_py roundtrip
dom=$((DOM_BASE+27))
ats=$(run_wrap_bg ${dom} "${WORK_DIR}/atpy_s.log" "${PREFIX}/bin/action_tutorials_py__fibonacci_action_server")
sleep 4
atc=$(run_wrap_bg ${dom} "${WORK_DIR}/atpy_c.log" "${PREFIX}/bin/action_tutorials_py__fibonacci_action_client")
sleep 9; kill ${ats} ${atc} 2>/dev/null; kill_pattern "fibonacci_action"; wait ${ats} ${atc} 2>/dev/null
if grep -qiE "Result:|34, 55|Goal succeeded" "${WORK_DIR}/atpy_c.log"; then result ext_action_tutorials_py PASS "fib_result"; else result ext_action_tutorials_py FAIL "see atpy_c.log"; fi

# 29. rclpy QoS incompatible
dom=$((DOM_BASE+28))
qpy=$(run_wrap_bg ${dom} "${WORK_DIR}/qospy.log" "${PREFIX}/bin/quality_of_service_demo_py__incompatible_qos" reliability)
sleep 9; kill ${qpy} 2>/dev/null; kill_pattern "quality_of_service_demo_py"; wait ${qpy} 2>/dev/null
if grep -qiE "incompatible qos|RELIABILITY" "${WORK_DIR}/qospy.log"; then result ext_rclpy_qos PASS "event_fired"; else result ext_rclpy_qos FAIL "see qospy.log"; fi

# 30. topic monitor
dom=$((DOM_BASE+29))
tm=$(run_wrap_bg ${dom} "${WORK_DIR}/tmon.log" "${PREFIX}/bin/topic_monitor__topic_monitor")
sleep 2
dp2=$(run_wrap_bg ${dom} "${WORK_DIR}/tmon_pub.log" "${PREFIX}/bin/topic_monitor__data_publisher" critical --end-after 5)
sleep 9; kill ${tm} ${dp2} 2>/dev/null; kill_pattern "topic_monitor"; wait ${tm} ${dp2} 2>/dev/null
if grep -qiE "Subscribing|reception rate|Alive|critical" "${WORK_DIR}/tmon.log"; then result ext_topic_monitor PASS "monitoring"; else result ext_topic_monitor FAIL "see tmon.log"; fi

###########################################################################
# Transports
###########################################################################

# 31. image_transport raw
out=$(env LD_LIBRARY_PATH="${CXX_LD}" AMENT_PREFIX_PATH="${PREFIX}" "${PREFIX}/lib/image_transport/list_transports" 2>&1)
case "${out}" in *image_transport/raw*) result ext_image_transport PASS "raw_declared";; *) result ext_image_transport FAIL "$(echo "${out}" | tail -2 | tr '\n' ' ')";; esac

# 32. point_cloud_transport raw
out=$(env LD_LIBRARY_PATH="${CXX_LD}" AMENT_PREFIX_PATH="${PREFIX}" "${PREFIX}/lib/point_cloud_transport/list_transports" 2>&1)
case "${out}" in *point_cloud_transport/raw*) result ext_point_cloud_transport PASS "raw_declared";; *) result ext_point_cloud_transport FAIL "$(echo "${out}" | tail -2 | tr '\n' ' ')";; esac

###########################################################################
# Deeper ros2 CLI
###########################################################################

# 33. ros2 node info
dom=$((DOM_BASE+31))
tk=$(run_cxx_bg ${dom} "${WORK_DIR}/ni_talker.log" "$(DN talker)")
sleep 5
ni=$(ros2_dom ${dom} node info /talker 2>&1)
kill ${tk} 2>/dev/null; wait ${tk} 2>/dev/null
case "${ni}" in *Publishers*|*/chatter*) result ext_cli_node_info PASS "publishers_listed";; *) result ext_cli_node_info FAIL "$(echo "${ni}" | head -1)";; esac

# 34. ros2 topic hz
dom=$((DOM_BASE+32))
tk=$(run_cxx_bg ${dom} "${WORK_DIR}/hz_talker.log" "$(DN talker)")
sleep 4
cli_timed ${dom} 7 "${WORK_DIR}/hz.log" topic hz /chatter
kill ${tk} 2>/dev/null; wait ${tk} 2>/dev/null
if grep -qiE "average rate|rate" "${WORK_DIR}/hz.log"; then result ext_cli_topic_hz PASS "rate_measured"; else result ext_cli_topic_hz FAIL "see hz.log"; fi

# 35. ros2 topic bw
dom=$((DOM_BASE+33))
tk=$(run_cxx_bg ${dom} "${WORK_DIR}/bw_talker.log" "$(DN talker)")
sleep 4
cli_timed ${dom} 8 "${WORK_DIR}/bw.log" topic bw /chatter
kill ${tk} 2>/dev/null; wait ${tk} 2>/dev/null
if grep -qiE "B/s|KB/s|bandwidth|/s from" "${WORK_DIR}/bw.log"; then result ext_cli_topic_bw PASS "bw_measured"; else result ext_cli_topic_bw FAIL "see bw.log"; fi

# 36. ros2 topic type + find
dom=$((DOM_BASE+34))
tk=$(run_cxx_bg ${dom} "${WORK_DIR}/tf_talker.log" "$(DN talker)")
sleep 5
ty=$(ros2_dom ${dom} topic type /chatter 2>&1)
fd=$(ros2_dom ${dom} topic find std_msgs/msg/String 2>&1)
kill ${tk} 2>/dev/null; wait ${tk} 2>/dev/null
if echo "${ty}" | grep -q "std_msgs/msg/String" && echo "${fd}" | grep -q "/chatter"; then result ext_cli_topic_type_find PASS "type+find"; else result ext_cli_topic_type_find FAIL "ty=${ty} fd=${fd}"; fi

# 37. ros2 service type + introspection list
dom=$((DOM_BASE+35))
sv=$(run_cxx_bg ${dom} "${WORK_DIR}/svc_srv.log" "$(DN add_two_ints_server)")
sleep 5
slist=$(ros2_dom ${dom} service list 2>&1)
stype=$(ros2_dom ${dom} service type /add_two_ints 2>&1)
kill ${sv} 2>/dev/null; wait ${sv} 2>/dev/null
if echo "${slist}" | grep -q "/add_two_ints" && echo "${stype}" | grep -q "AddTwoInts"; then result ext_cli_service_type PASS "list+type"; else result ext_cli_service_type FAIL "list=$(echo "${slist}"|head -1) type=${stype}"; fi

# 38. ros2 multicast (local send/receive roundtrip)
dom=$((DOM_BASE+36))
env ROS_DOMAIN_ID=${dom} "${ROS2}" multicast receive >"${WORK_DIR}/mcast.log" 2>&1 &
mc=$!
sleep 2
env ROS_DOMAIN_ID=${dom} "${ROS2}" multicast send >>"${WORK_DIR}/mcast.log" 2>&1
sleep 1; kill ${mc} 2>/dev/null; kill_pattern "multicast receive"; wait ${mc} 2>/dev/null
if grep -qiE "Sending|Received|multicast" "${WORK_DIR}/mcast.log"; then result ext_cli_multicast PASS "send_receive"; else result ext_cli_multicast FAIL "see mcast.log"; fi

###########################################################################
# rosbag2 deeper: burst / reindex / convert / compression
###########################################################################

# prep: record a small sqlite3 bag with a foreground recorder + watchdog
dom=$((DOM_BASE+37))
rm -rf "${WORK_DIR}/extbag"
tk=$(run_cxx_bg ${dom} "${WORK_DIR}/extbag_talker.log" "$(DN talker)")
sleep 3
( sleep 12; kill_pattern "bag record" INT; sleep 8; kill_pattern "bag record" ) &
wd=$!
ros2_dom ${dom} bag record --storage sqlite3 --topics /chatter -o "${WORK_DIR}/extbag" >"${WORK_DIR}/extbag_rec.log" 2>&1
kill ${wd} 2>/dev/null; kill ${tk} 2>/dev/null; wait ${tk} 2>/dev/null

# 39. reindex
cp -r "${WORK_DIR}/extbag" "${WORK_DIR}/extbag_ri" 2>/dev/null
rm -f "${WORK_DIR}/extbag_ri/metadata.yaml"
ros2_dom ${dom} bag reindex "${WORK_DIR}/extbag_ri" >"${WORK_DIR}/reindex.log" 2>&1
if [ -f "${WORK_DIR}/extbag_ri/metadata.yaml" ]; then result ext_bag_reindex PASS "metadata_rebuilt"; else result ext_bag_reindex FAIL "see reindex.log"; fi

# 40. burst playback (dedicated `ros2 bag burst` verb)
dom=$((DOM_BASE+38))
bl=$(run_cxx_bg ${dom} "${WORK_DIR}/burst_listener.log" "$(DN listener)")
sleep 3
( sleep 15; kill_pattern "bag burst" INT; sleep 4; kill_pattern "bag burst" ) &
wd=$!
ros2_dom ${dom} bag burst -s sqlite3 -n 10 "${WORK_DIR}/extbag" >"${WORK_DIR}/burst.log" 2>&1
kill ${wd} 2>/dev/null; kill ${bl} 2>/dev/null; wait ${bl} 2>/dev/null
if grep -qi "I heard" "${WORK_DIR}/burst_listener.log" || grep -qiE "Burst|bursting" "${WORK_DIR}/burst.log"; then result ext_bag_burst PASS "bursted"; else result ext_bag_burst FAIL "$(tail -2 "${WORK_DIR}/burst.log" | tr '\n' ' ')"; fi

# 41. convert sqlite3 -> mcap
cat > "${WORK_DIR}/convert.yaml" <<YAML
output_bags:
  - uri: ${WORK_DIR}/extbag_mcap
    storage_id: mcap
    all_topics: true
YAML
ros2_dom ${dom} bag convert -i "${WORK_DIR}/extbag" -o "${WORK_DIR}/convert.yaml" >"${WORK_DIR}/convert.log" 2>&1
ci=$("${ROS2}" bag info "${WORK_DIR}/extbag_mcap" 2>&1)
case "${ci}" in *mcap*"std_msgs/msg/String"*) result ext_bag_convert PASS "sqlite3_to_mcap";; *) result ext_bag_convert FAIL "$(echo "${ci}" | tail -2 | tr '\n' ' ')";; esac

# 42. compressed recording (zstd, per-message)
dom=$((DOM_BASE+39))
rm -rf "${WORK_DIR}/cbag"
tk=$(run_cxx_bg ${dom} "${WORK_DIR}/cbag_talker.log" "$(DN talker)")
sleep 3
( sleep 12; kill_pattern "bag record" INT; sleep 8; kill_pattern "bag record" ) &
wd=$!
ros2_dom ${dom} bag record --storage sqlite3 --compression-mode message --compression-format zstd --topics /chatter -o "${WORK_DIR}/cbag" >"${WORK_DIR}/cbag_rec.log" 2>&1
kill ${wd} 2>/dev/null; kill ${tk} 2>/dev/null; wait ${tk} 2>/dev/null
# message-mode compression stores compressed payloads inside the .db3 and
# records the format in metadata.yaml (no separate .zstd file)
if grep -qi "compression_format: zstd" "${WORK_DIR}/cbag/metadata.yaml" 2>/dev/null; then result ext_bag_compression PASS "zstd_message_mode"; else result ext_bag_compression FAIL "$(grep -i compression "${WORK_DIR}/cbag/metadata.yaml" 2>/dev/null | tr '\n' ' ')"; fi

# cleanup daemons/strays
kill_pattern "ros2-daemon"
kill_pattern "bag record"
kill_pattern "bag play"

echo "EXT_VALIDATION_DONE"
