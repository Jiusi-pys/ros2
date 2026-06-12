#!/bin/sh
# Exhaustive ros2 CLI command test matrix for RK3588A/OHOS.
#
# Goal: exercise EVERY `ros2 <command> <subcommand>` against a live fixture
# graph and verify each produces the expected output. Complements the
# feature-level matrices (validate_all / validate_ext) with a command-centric
# pass over the full CLI surface enumerated from `ros2 <cmd> --help`.
#
# Emits one "RESULT|<lane>|PASS/FAIL|<evidence>" line per command. Never aborts.
#
# The global `ros2` launcher (/usr/local/bin/ros2) self-exports HOME / LD path /
# PYTHONPATH, so fixtures are started with `ros2 run` to inherit that env.

OVERLAY="${OVERLAY:-/data/local/tmp/ohos-colcon-rk3588a}"
PREFIX="${PREFIX:-/data/local/tmp/ohos-prefix}"
ROS2="${ROS2:-/usr/local/bin/ros2}"
[ -x "${ROS2}" ] || ROS2="${OVERLAY}/bin/ros2"
WORK="${WORK:-/data/local/tmp/valcli}"
DOM="${DOM:-51}"

export HOME=/data/local/tmp
export ROS_LOG_DIR=/data/local/tmp/roslogs
export ROS_DISTRO=jazzy
export ROS_DOMAIN_ID="${DOM}"
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
export AMENT_PREFIX_PATH="${PREFIX}"
mkdir -p "${WORK}" "${ROS_LOG_DIR}"
rm -f "${WORK}"/*.log 2>/dev/null

result() { echo "RESULT|$1|$2|$3"; }

# pass if cmd output (stdout+stderr) matches the extended-regex in $2
ck() {  # ck <lane> <regex> <ros2 args...>
  lane="$1"; pat="$2"; shift 2
  out=$("${ROS2}" "$@" 2>&1)
  if echo "${out}" | grep -qE "${pat}"; then result "${lane}" PASS "$(echo "${out}" | grep -m1 -E "${pat}" | cut -c1-60)"
  else result "${lane}" FAIL "$(echo "${out}" | grep -vE '^\s*$' | head -1 | cut -c1-70)"; fi
}

# timed variant for long-lived cmds (hz/bw/delay/echo): capture a window
ckt() {  # ckt <lane> <secs> <regex> <ros2 args...>
  lane="$1"; secs="$2"; pat="$3"; shift 3
  "${ROS2}" "$@" >"${WORK}/${lane}.log" 2>&1 &
  cp=$!; sleep "${secs}"; kill "${cp}" 2>/dev/null; wait "${cp}" 2>/dev/null
  if grep -qE "${pat}" "${WORK}/${lane}.log"; then result "${lane}" PASS "$(grep -m1 -E "${pat}" "${WORK}/${lane}.log" | cut -c1-60)"
  else result "${lane}" FAIL "$(grep -vE '^\s*$' "${WORK}/${lane}.log" | head -1 | cut -c1-70)"; fi
}

kill_pat() { ps -ef | grep "$1" | grep -v grep | while read _u _p _r; do kill -"${2:-9}" "${_p}" 2>/dev/null; done; }
run_bg() { "${ROS2}" run $1 $2 >"${WORK}/$3.log" 2>&1 & echo $!; }

#############################################################################
echo "### GROUP pkg / interface / doctor / plugin (no graph)"
#############################################################################
ck cli_pkg_list        '^[a-z].*'                       pkg list
ck cli_pkg_prefix      '/data/local/tmp'                pkg prefix rclcpp
ck cli_pkg_executables 'talker'                         pkg executables demo_nodes_cpp
ck cli_pkg_xml         '<name>rclcpp</name>'            pkg xml rclcpp
# ros2 pkg create scaffolds a package (needs ament_copyright + ament_cmake/python)
rm -rf "${WORK}/pkgcreate"; mkdir -p "${WORK}/pkgcreate"
pc=$(cd "${WORK}/pkgcreate" && "${ROS2}" pkg create --build-type ament_cmake my_pkg 2>&1)
if echo "${pc}" | grep -qiE "going to create|creating folder|package my_pkg"; then result cli_pkg_create PASS "scaffolded"; else result cli_pkg_create FAIL "$(echo "${pc}" | grep -iE 'error|no module' | head -1 | cut -c1-70)"; fi

ck cli_interface_list     'msg/'                        interface list
ck cli_interface_show     'string data'                 interface show std_msgs/msg/String
ck cli_interface_package  'msg/String'                  interface package std_msgs
ck cli_interface_packages 'std_msgs'                    interface packages
ck cli_interface_proto    'data:'                       interface proto std_msgs/msg/String

ck cli_doctor_report   'report|platform|topic'          doctor --report
ck cli_plugin_list     'URDFXMLParser|URDFParser'        plugin list --package urdf
ck cli_wtf             'report|All|check'                wtf

#############################################################################
echo "### GROUP daemon / multicast"
#############################################################################
"${ROS2}" daemon stop >/dev/null 2>&1
ck cli_daemon_start  'started|already'                  daemon start
ck cli_daemon_status 'running'                          daemon status
ck cli_daemon_stop   'stopped|not running'              daemon stop

"${ROS2}" multicast receive >"${WORK}/mcast.log" 2>&1 &
mc=$!; sleep 2; "${ROS2}" multicast send >>"${WORK}/mcast.log" 2>&1; sleep 1
kill ${mc} 2>/dev/null; kill_pat "multicast receive"; wait ${mc} 2>/dev/null
if grep -qiE "Sending|Received" "${WORK}/mcast.log"; then result cli_multicast PASS "send/receive"; else result cli_multicast FAIL "see mcast.log"; fi

#############################################################################
echo "### GROUP node / topic / service / param / action (live fixtures)"
#############################################################################
TK=$(run_bg demo_nodes_cpp talker fx_talker)
# introspection_service serves /add_two_ints AND can publish service events
# (needed for `ros2 service echo`); introspection_client drives periodic calls.
SV=$(run_bg demo_nodes_cpp introspection_service fx_svc)
IC=$(run_bg demo_nodes_cpp introspection_client fx_svc_client)
PB=$(run_bg demo_nodes_cpp parameter_blackboard fx_param)
AS=$(run_bg examples_rclcpp_minimal_action_server action_server_member_functions fx_action)
# a stamped publisher for `ros2 topic delay`
( while true; do "${ROS2}" topic pub -1 /ps geometry_msgs/msg/PoseStamped "{header: {stamp: {sec: 0}}}" >/dev/null 2>&1; sleep 1; done ) &
PSP=$!
sleep 8

# --- node ---
ck cli_node_list  '/talker'                             node list
ck cli_node_info  '/chatter'                            node info /talker

# --- topic ---
ck  cli_topic_list  '/chatter'                          topic list
ck  cli_topic_info  'Type: std_msgs/msg/String'         topic info /chatter
ck  cli_topic_type  'std_msgs/msg/String'               topic type /chatter
ck  cli_topic_find  '/chatter'                          topic find std_msgs/msg/String
ckt cli_topic_echo  6 'data:'                            topic echo /chatter
ckt cli_topic_hz    8 'average rate'                     topic hz /chatter
ckt cli_topic_bw    9 'B/s|KB/s'                         topic bw /chatter
ckt cli_topic_delay 8 'delay|average'                    topic delay /ps
# topic pub: publish then confirm a subscriber hears it
"${ROS2}" topic echo /pubtest std_msgs/msg/String --once >"${WORK}/pubecho.log" 2>&1 &
pe=$!; sleep 2
"${ROS2}" topic pub --times 5 /pubtest std_msgs/msg/String "{data: cli_pub_probe}" >/dev/null 2>&1
sleep 2; kill ${pe} 2>/dev/null; wait ${pe} 2>/dev/null
if grep -q "cli_pub_probe" "${WORK}/pubecho.log"; then result cli_topic_pub PASS "published+received"; else result cli_topic_pub FAIL "see pubecho.log"; fi

# --- service ---
ck cli_service_list 'add_two_ints'                      service list
ck cli_service_type 'AddTwoInts'                        service type /add_two_ints
ck cli_service_find 'add_two_ints'                      service find example_interfaces/srv/AddTwoInts
ck cli_service_info 'Type|Clients|Servers|AddTwoInts'   service info /add_two_ints
ck cli_service_call 'sum=5|response'                    service call /add_two_ints example_interfaces/srv/AddTwoInts "{a: 2, b: 3}"
# enable service introspection so the _service_event topic gets a publisher,
# then echo it (introspection_client keeps calling, generating events)
"${ROS2}" param set /introspection_service service_configure_introspection metadata >/dev/null 2>&1
"${ROS2}" param set /introspection_client client_configure_introspection metadata >/dev/null 2>&1
ckt cli_service_echo 10 'event_type|client_gid|info|REQUEST' service echo /add_two_ints

# --- param (parameter_blackboard accepts dynamic params) ---
ck cli_param_list     'parameter_blackboard|use_sim_time'  param list /parameter_blackboard
"${ROS2}" param set /parameter_blackboard cli_p 1.25 >/dev/null 2>&1
ck cli_param_set      'set successful|successful'          param set /parameter_blackboard cli_p 2.5
ck cli_param_get      '2.5'                                 param get /parameter_blackboard cli_p
ck cli_param_describe 'Parameter name|Type|Double'         param describe /parameter_blackboard cli_p
"${ROS2}" param dump /parameter_blackboard > "${WORK}/params.yaml" 2>/dev/null
if grep -qE 'parameter_blackboard|cli_p' "${WORK}/params.yaml"; then result cli_param_dump PASS "yaml written"; else result cli_param_dump FAIL "dump empty"; fi
ck cli_param_load     'Set parameter|successful|loaded'    param load /parameter_blackboard "${WORK}/params.yaml"
ck cli_param_delete   'Deleted|successful'                 param delete /parameter_blackboard cli_p

# --- action ---
ck cli_action_list 'fibonacci'                          action list
ck cli_action_info 'Action servers: 1|/fibonacci'       action info /fibonacci
ck cli_action_type 'Fibonacci'                          action type /fibonacci
ck cli_action_send 'SUCCEEDED|sequence'                 action send_goal /fibonacci example_interfaces/action/Fibonacci "{order: 5}"

kill ${TK} ${SV} ${IC} ${PB} ${AS} ${PSP} 2>/dev/null
kill_pat "demo_nodes_cpp"; kill_pat "action_server_member"; kill_pat "topic pub -1 /ps"
wait ${TK} ${SV} ${IC} ${PB} ${AS} 2>/dev/null

#############################################################################
echo "### GROUP lifecycle"
#############################################################################
LT=$(run_bg lifecycle lifecycle_talker fx_lc)
sleep 6
ck cli_lifecycle_nodes '/lc_talker|lifecycle'           lifecycle nodes
ck cli_lifecycle_list  'configure|create'               lifecycle list /lc_talker
ck cli_lifecycle_get   'unconfigured|inactive|active'   lifecycle get /lc_talker
ck cli_lifecycle_set   'Transitioning|successful'       lifecycle set /lc_talker configure
kill ${LT} 2>/dev/null; kill_pat "lifecycle_talker"; wait ${LT} 2>/dev/null

#############################################################################
echo "### GROUP component"
#############################################################################
CC=$(run_bg rclcpp_components component_container fx_cc)
sleep 6
ck cli_component_types 'composition::|::Talker'         component types
ck cli_component_load  'Loaded component|unique'         component load /ComponentManager composition composition::Talker
ck cli_component_list  'Talker|/ComponentManager'        component list
ck cli_component_unload 'Unloaded|Failed'                component unload /ComponentManager 1
kill ${CC} 2>/dev/null; kill_pat "component_container"; wait ${CC} 2>/dev/null
# standalone runs a component as its own process
"${ROS2}" component standalone composition composition::Talker >"${WORK}/standalone.log" 2>&1 &
st=$!; sleep 6; kill ${st} 2>/dev/null; kill_pat "component standalone"; wait ${st} 2>/dev/null
if grep -qiE "Publishing|Talker" "${WORK}/standalone.log"; then result cli_component_standalone PASS "ran"; else result cli_component_standalone FAIL "see standalone.log"; fi

#############################################################################
echo "### GROUP bag"
#############################################################################
TK2=$(run_bg demo_nodes_cpp talker fx_bagtalker)
sleep 3
rm -rf "${WORK}/clibag"
( sleep 10; kill_pat "bag record" TERM; sleep 6; kill_pat "bag record" ) &
"${ROS2}" bag record --storage sqlite3 --topics /chatter -o "${WORK}/clibag" >"${WORK}/bagrec.log" 2>&1
[ -f "${WORK}/clibag/metadata.yaml" ] || "${ROS2}" bag reindex "${WORK}/clibag" -s sqlite3 >/dev/null 2>&1
kill ${TK2} 2>/dev/null; kill_pat "demo_nodes_cpp"; wait ${TK2} 2>/dev/null
ck cli_bag_record_info 'sqlite3|Messages'               bag info "${WORK}/clibag"
ck cli_bag_list_storage 'sqlite3|mcap'                  bag list storage
# rm -rf the target first: cp -r into an existing dir nests instead of replacing,
# and a stale bag dir from a prior run would feed reindex the wrong storage file
rm -rf "${WORK}/clibag_ri"; cp -r "${WORK}/clibag" "${WORK}/clibag_ri"; rm -f "${WORK}/clibag_ri/metadata.yaml"
ck cli_bag_reindex 'Reindexing complete|Beginning reindex' bag reindex "${WORK}/clibag_ri" -s sqlite3
[ -f "${WORK}/clibag_ri/metadata.yaml" ] && result cli_bag_reindex_check PASS "metadata rebuilt" || result cli_bag_reindex_check FAIL "no metadata"
# burst: `ros2 bag burst -n N` bursts N messages then STAYS PAUSED (does not
# exit), so it must run backgrounded and be killed after the burst lands.
LB=$(run_bg demo_nodes_cpp listener fx_burstlsn)
sleep 2
"${ROS2}" bag burst -s sqlite3 -n 5 "${WORK}/clibag" >"${WORK}/burst.log" 2>&1 &
bp=$!; sleep 6; kill ${bp} 2>/dev/null; kill_pat "bag burst"; wait ${bp} 2>/dev/null
kill ${LB} 2>/dev/null; kill_pat "demo_nodes_cpp"; wait ${LB} 2>/dev/null
if grep -qiE "Burst|bursting" "${WORK}/burst.log" || grep -q "I heard" "${WORK}/fx_burstlsn.log" 2>/dev/null; then result cli_bag_burst PASS "bursted"; else result cli_bag_burst FAIL "see burst.log"; fi
# convert sqlite3->mcap
rm -rf "${WORK}/clibag_mcap"
cat > "${WORK}/conv.yaml" <<YAML
output_bags:
  - uri: ${WORK}/clibag_mcap
    storage_id: mcap
    all_topics: true
YAML
"${ROS2}" bag convert -i "${WORK}/clibag" -o "${WORK}/conv.yaml" >/dev/null 2>&1
# assert via the produced .mcap file + a "Storage id:"/"Messages:" line, NOT a
# bare 'mcap' (which would also match the clibag_mcap path in a "does not exist"
# error and falsely PASS)
convinfo=$("${ROS2}" bag info "${WORK}/clibag_mcap" 2>&1)
if ls "${WORK}/clibag_mcap"/*.mcap >/dev/null 2>&1 && echo "${convinfo}" | grep -qE 'Storage id:|Messages:'; then
  result cli_bag_convert PASS "$(echo "${convinfo}" | grep -m1 -E 'Storage id:|Messages:' | cut -c1-40)"
else
  result cli_bag_convert FAIL "$(echo "${convinfo}" | grep -vE '^[[:space:]]*$' | head -1 | cut -c1-70)"
fi
# play: a finite bag exits on its own, but bound it with a watchdog for safety
LP=$(run_bg demo_nodes_cpp listener fx_playlsn)
sleep 2
"${ROS2}" bag play "${WORK}/clibag" >"${WORK}/play.log" 2>&1 &
pp2=$!; ( sleep 20; kill_pat "bag play" ) & wd=$!
wait ${pp2} 2>/dev/null; kill ${wd} 2>/dev/null
sleep 1; kill ${LP} 2>/dev/null; kill_pat "demo_nodes_cpp"; wait ${LP} 2>/dev/null
if grep -q "I heard" "${WORK}/fx_playlsn.log" 2>/dev/null; then result cli_bag_play PASS "replayed"; else result cli_bag_play FAIL "see play.log"; fi

#############################################################################
echo "### GROUP run / launch"
#############################################################################
"${ROS2}" run demo_nodes_cpp talker >"${WORK}/run.log" 2>&1 &
rp=$!; sleep 6; kill ${rp} 2>/dev/null; kill_pat "demo_nodes_cpp"; wait ${rp} 2>/dev/null
if grep -q "Publishing" "${WORK}/run.log"; then result cli_run PASS "talker ran"; else result cli_run FAIL "see run.log"; fi

"${ROS2}" launch demo_nodes_cpp talker_listener_launch.py --noninteractive >"${WORK}/launch.log" 2>&1 &
lp=$!; sleep 16; kill -INT ${lp} 2>/dev/null; sleep 2; kill ${lp} 2>/dev/null
kill_pat "talker_listener_launch"; kill_pat "demo_nodes_cpp"; wait ${lp} 2>/dev/null
if grep -q "I heard" "${WORK}/launch.log"; then result cli_launch PASS "launched"; else result cli_launch FAIL "see launch.log"; fi

# final cleanup
kill_pat "ros2-daemon"; kill_pat "demo_nodes_cpp"; kill_pat "component"; kill_pat "bag "
echo "CLI_VALIDATION_DONE"
