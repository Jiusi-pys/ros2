#!/usr/bin/env bash
# Per-board local feature lanes for ROS 2 features that are inherently
# single-process / non-distributable (can't be split across two boards):
# in-process composition, component container, rclpy executors/guard conditions,
# logging, topic statistics, image/point-cloud transports, wait sets, plus a few
# fixture-less CLI checks. Launcher-parameterized so it runs under FastDDS
# (/usr/local/bin/ros2) or CycloneDDS (/data/local/tmp/ohos-cyc/ros2-cyclone).
#
# Sourced by run_cross_board_full_matrix.sh. Reuses helpers from
# crossboard_lanes.sh (dev_bg/dev_fg/dev_kill/dev_cat/emit/verdict).
# Globals: DEV (target board), LAUNCH (launcher), DOM, TAG (e.g. local_fastdds_A).

# fixtureless: run launcher cmd fg, verdict on pat
lf_simple() { local lane="$1" pat="$2"; shift 2; verdict "$lane" "$(dev_fg "$DEV" "$LAUNCH $*")" "$pat"; }

# run a single self-contained node for $3 s, verdict its own log on $2
lf_node() {
  local lane="$1" pat="$2" secs="$3" kill="$4"; shift 4
  local log="${WORK}/${TAG}_${lane}.log"
  dev_bg "$DEV" "$log" "$LAUNCH $*"; sleep "$secs"; dev_kill "$DEV" "$kill"
  verdict "$lane" "$(dev_cat "$DEV" "$log")" "$pat"
}

# in-process composition (talker+listener loaded into one process)
lane_local_composition()    { lf_node composition "I heard|Publishing" 8 manual_composition run composition manual_composition; }
lane_local_logging()        { lf_node logging "\\[INFO\\]|\\[WARN\\]|\\[DEBUG\\]|\\[ERROR\\]|log message" 7 logging_demo_main run logging_demo logging_demo_main; }
lane_local_wait_set()       { lf_node wait_set "I heard|received|listener|wait_set" 9 wait_set run examples_rclcpp_wait_set wait_set; }

lane_local_image_transport()  { lf_simple image_transport "raw" run image_transport list_transports; }
lane_local_pcl_transport()    { lf_simple point_cloud_transport "raw" run point_cloud_transport list_transports; }
lane_local_doctor()           { lf_simple doctor "middleware name" doctor --report; }
lane_local_interface_show()   { lf_simple interface_show "string data" interface show std_msgs/msg/String; }
lane_local_pkg_list()         { lf_simple pkg_list "demo_nodes_cpp|rclcpp|std_msgs" pkg list; }

# rclpy runtime (executor/pub/sub) proven via a same-board CLI loopback (ros2cli IS rclpy)
lane_local_rclpy_loopback() {
  local lane=rclpy_loopback slog="${WORK}/${TAG}_rclpylo.log"
  dev_bg "$DEV" "$slog" "$LAUNCH topic echo /xb_lo std_msgs/msg/String"; sleep 2
  dev_bg "$DEV" "${WORK}/${TAG}_rclpyp.log" "$LAUNCH topic pub /xb_lo std_msgs/msg/String \"{data: lo}\""; sleep 5
  dev_kill "$DEV" "topic pub"; dev_kill "$DEV" "topic echo"
  verdict "$lane" "$(dev_cat "$DEV" "$slog")" "data: lo"
}

# topic statistics: display_topic_statistics 'string' is self-contained (it
# starts its own talker/listener + statistics_listener and prints real metrics).
lane_local_topic_stats() { lf_node topic_statistics "message_age|message_period|Statistics heard" 9 display_topic_statistics run topic_statistics_demo display_topic_statistics string; }

lane_local_component() {
  local lane=component clog="${WORK}/${TAG}_component.log"
  dev_bg "$DEV" "$clog" "$LAUNCH run rclcpp_components component_container"; sleep 4
  local o; o="$(dev_fg "$DEV" "$LAUNCH component load /ComponentManager composition composition::Talker")"
  local l; l="$(dev_fg "$DEV" "$LAUNCH component list")"
  dev_kill "$DEV" component_container
  if echo "$o $l" | grep -qiE "Loaded component|/ComponentManager|Talker"; then emit "$lane" PASS "$(echo "$o"|grep -m1 -iE 'Loaded|Talker'|cut -c1-50|tr -d '\r')"
  else verdict "$lane" "$o $l" "Loaded|Talker"; fi
}

run_local_features() {
  lane_local_composition
  lane_local_component
  lane_local_wait_set
  lane_local_logging
  lane_local_topic_stats
  lane_local_rclpy_loopback
  lane_local_image_transport
  lane_local_pcl_transport
  lane_local_interface_show
  lane_local_pkg_list
  lane_local_doctor
}
