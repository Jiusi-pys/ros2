#!/usr/bin/env bash
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROSBAG2_DIR="${ROOT_DIR}/src/ros2/rosbag2"
RCL_DIR="${ROOT_DIR}/src/ros2/rcl"
RCLCPP_DIR="${ROOT_DIR}/src/ros2/rclcpp"
PROBE_DIR="${ROOT_DIR}/ohos/tools/rmw_mdds_action_bag_probe"
HOST_RUNNER="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_action_bag.sh"
CROSS_BOARD_RUNNER="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_action_bag.sh"
CROSS_BUILD_SCRIPT="${ROOT_DIR}/ohos/colcon_rk3588a.sh"
DEPLOY_SCRIPT="${ROOT_DIR}/ohos/tools/deploy_rmw_mdds_delta.sh"
WORKSPACE_PATCH_SCRIPT="${ROOT_DIR}/ohos/apply_workspace_patches.sh"
RCL_PATCH="${ROOT_DIR}/ohos/patches_full/ros2_rcl.patch"
RCLCPP_PATCH="${ROOT_DIR}/ohos/patches_full/ros2_rclcpp.patch"
ROSBAG2_PATCH="${ROOT_DIR}/ohos/patches_full/ros2_rosbag2.patch"
IPC_CLIENT_SOURCE="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/src/ipc_client.cpp"
BROKER_PROCESS_TEST="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/test/test_broker_process.cpp"
FAILURES=0

fail_contract() {
  echo "RESULT|rmw_mdds_action_bag_contracts|RED|$*" >&2
  FAILURES=$((FAILURES + 1))
}

require_file() {
  local path="$1"
  [[ -f "${path}" ]] || fail_contract "missing_file=${path#${ROOT_DIR}/}"
}

require_pattern() {
  local path="$1"
  local pattern="$2"
  local label="$3"
  if [[ ! -f "${path}" ]] || ! grep -qE -- "${pattern}" "${path}"; then
    fail_contract "missing_capability=${label}"
  fi
}

require_help_option() {
  local help_text="$1"
  local option="$2"
  grep -q -- "${option}" <<<"${help_text}" || fail_contract "missing_cli_option=${option}"
}

require_file "${ROSBAG2_DIR}/rosbag2_cpp/include/rosbag2_cpp/action_utils.hpp"
require_file "${ROSBAG2_DIR}/rosbag2_cpp/src/rosbag2_cpp/action_utils.cpp"
require_file "${ROSBAG2_DIR}/rosbag2_transport/include/rosbag2_transport/player_action_client.hpp"
require_file "${ROSBAG2_DIR}/rosbag2_transport/src/rosbag2_transport/player_action_client.cpp"
require_file "${PROBE_DIR}/CMakeLists.txt"
require_file "${PROBE_DIR}/main.cpp"
require_file "${HOST_RUNNER}"
require_file "${CROSS_BOARD_RUNNER}"
require_file "${CROSS_BUILD_SCRIPT}"
require_file "${DEPLOY_SCRIPT}"
require_file "${WORKSPACE_PATCH_SCRIPT}"
require_file "${RCL_PATCH}"
require_file "${RCLCPP_PATCH}"
require_file "${ROSBAG2_PATCH}"
require_file "${IPC_CLIENT_SOURCE}"
require_file "${BROKER_PROCESS_TEST}"

require_pattern \
  "${ROSBAG2_DIR}/ros2bag/ros2bag/verb/record.py" \
  "--actions" \
  "record_actions_cli"
require_pattern \
  "${ROSBAG2_DIR}/ros2bag/ros2bag/verb/record.py" \
  "--all-actions" \
  "record_all_actions_cli"
require_pattern \
  "${ROSBAG2_DIR}/ros2bag/ros2bag/verb/record.py" \
  "--exclude-actions" \
  "record_exclude_actions_cli"
require_pattern \
  "${ROSBAG2_DIR}/ros2bag/ros2bag/verb/play.py" \
  "--send-actions-as-client" \
  "play_actions_as_client_cli"
require_pattern \
  "${ROSBAG2_DIR}/rosbag2_transport/include/rosbag2_transport/record_options.hpp" \
  "bool all_actions" \
  "record_all_actions_option"
require_pattern \
  "${ROSBAG2_DIR}/rosbag2_transport/include/rosbag2_transport/play_options.hpp" \
  "bool send_actions_as_client" \
  "play_actions_as_client_option"
require_pattern \
  "${RCL_DIR}/rcl_action/include/rcl_action/action_client.h" \
  "rcl_action_client_configure_action_introspection" \
  "rcl_action_client_introspection_api"
require_pattern \
  "${RCL_DIR}/rcl_action/include/rcl_action/action_server.h" \
  "rcl_action_server_configure_action_introspection" \
  "rcl_action_server_introspection_api"
require_pattern \
  "${RCLCPP_DIR}/rclcpp_action/include/rclcpp_action/client.hpp" \
  "configure_introspection" \
  "rclcpp_action_client_introspection_api"
require_pattern \
  "${RCLCPP_DIR}/rclcpp_action/include/rclcpp_action/server.hpp" \
  "configure_introspection" \
  "rclcpp_action_server_introspection_api"
require_pattern \
  "${PROBE_DIR}/main.cpp" \
  "RCL_SERVICE_INTROSPECTION_CONTENTS" \
  "probe_enables_action_introspection"
require_pattern \
  "${PROBE_DIR}/main.cpp" \
  "ACTION_BAG_SERVER_READY" \
  "probe_server_ready_marker"
require_pattern \
  "${PROBE_DIR}/main.cpp" \
  "ACTION_BAG_CLIENT_PASS" \
  "probe_client_pass_marker"
require_pattern \
  "${PROBE_DIR}/main.cpp" \
  "async_cancel_goal" \
  "probe_exercises_cancel_goal"
require_pattern \
  "${HOST_RUNNER}" \
  "RMW_IMPLEMENTATION=rmw_mdds_cpp" \
  "host_runner_forces_rmw_mdds"
require_pattern \
  "${HOST_RUNNER}" \
  "bag record --actions" \
  "host_runner_records_actions"
require_pattern \
  "${HOST_RUNNER}" \
  "--send-actions-as-client" \
  "host_runner_replays_actions_as_client"
require_pattern \
  "${CROSS_BOARD_RUNNER}" \
  "RMW_IMPLEMENTATION='rmw_mdds_cpp'" \
  "cross_board_runner_forces_rmw_mdds"
require_pattern \
  "${CROSS_BOARD_RUNNER}" \
  "bag record -s sqlite3 --actions" \
  "cross_board_runner_records_actions"
require_pattern \
  "${CROSS_BOARD_RUNNER}" \
  "--send-actions-as-client" \
  "cross_board_runner_replays_actions_as_client"
require_pattern \
  "${CROSS_BOARD_RUNNER}" \
  'ROS2_BIN.*daemon stop' \
  "cross_board_runner_resets_domain_daemon"
require_pattern \
  "${CROSS_BOARD_RUNNER}" \
  'Service: send_goal \| Request Count: 2 \| Response Count: 2' \
  "cross_board_runner_checks_exact_goal_counts"
require_pattern \
  "${CROSS_BOARD_RUNNER}" \
  '^remove_remote_runtime_files\(\)' \
  "cross_board_runner_defines_runtime_cleanup"
require_pattern \
  "${CROSS_BOARD_RUNNER}" \
  'remove_remote_runtime_files "\$\{CLIENT_DEVICE_ID\}"' \
  "cross_board_runner_removes_client_runtime_files"
require_pattern \
  "${CROSS_BOARD_RUNNER}" \
  'remove_remote_runtime_files "\$\{SERVER_DEVICE_ID\}"' \
  "cross_board_runner_removes_server_runtime_files"
require_pattern \
  "${IPC_CLIENT_SOURCE}" \
  'TriggerGraphGuardConditions' \
  "broker_graph_updates_trigger_node_guards"
require_pattern \
  "${BROKER_PROCESS_TEST}" \
  'GraphUpdateTriggersNodeGraphGuardCondition' \
  "broker_graph_guard_regression"
require_pattern \
  "${CROSS_BUILD_SCRIPT}" \
  '"\$\{ROOT_DIR\}/ohos/tools"' \
  "cross_build_discovers_probe_packages"
require_pattern \
  "${CROSS_BUILD_SCRIPT}" \
  'overlay_package_dir_args' \
  "cross_build_prefers_fresh_overlay_dependencies"
require_pattern \
  "${CROSS_BUILD_SCRIPT}" \
  'for overlay_package_name in "\$\{PACKAGES\[@\]\}"' \
  "cross_build_limits_overlay_overrides_to_requested_packages"
require_pattern \
  "${PROBE_DIR}/CMakeLists.txt" \
  'if\(NOT TARGET Python3::Python OR NOT TARGET Python3::NumPy\)' \
  "probe_preserves_cross_python_targets"
require_pattern \
  "${DEPLOY_SCRIPT}" \
  'lib/librcl_action.so' \
  "deploy_includes_rcl_action"
require_pattern \
  "${DEPLOY_SCRIPT}" \
  'lib/librclcpp_action.so' \
  "deploy_includes_rclcpp_action"
require_pattern \
  "${DEPLOY_SCRIPT}" \
  'lib/librosbag2_storage_sqlite3.so' \
  "deploy_includes_action_filter_storage"
require_pattern \
  "${DEPLOY_SCRIPT}" \
  'lib/rmw_mdds_action_bag_probe/rmw_mdds_action_bag_probe' \
  "deploy_includes_action_bag_probe"
require_pattern \
  "${DEPLOY_SCRIPT}" \
  'lib/python3.12/site-packages/ros2bag' \
  "deploy_includes_action_bag_cli"
require_pattern \
  "${WORKSPACE_PATCH_SCRIPT}" \
  'patches_full/ros2_rcl\.patch' \
  "workspace_patch_applies_rcl_action_introspection"
require_pattern \
  "${WORKSPACE_PATCH_SCRIPT}" \
  'patches_full/ros2_rclcpp\.patch' \
  "workspace_patch_applies_rclcpp_action_introspection"
require_pattern \
  "${WORKSPACE_PATCH_SCRIPT}" \
  'patches_full/ros2_rosbag2\.patch' \
  "workspace_patch_applies_native_action_bag"
require_pattern \
  "${RCL_PATCH}" \
  'rcl_action_client_configure_action_introspection' \
  "rcl_patch_contains_action_introspection"
require_pattern \
  "${RCLCPP_PATCH}" \
  'configure_introspection' \
  "rclcpp_patch_contains_action_introspection"
require_pattern \
  "${ROSBAG2_PATCH}" \
  'player_action_client' \
  "rosbag2_patch_contains_action_replay"

if [[ "${RMW_MDDS_ACTION_BAG_CHECK_INSTALLED:-1}" -eq 1 ]]; then
  if [[ ! -f "${ROOT_DIR}/install/setup.bash" ]]; then
    fail_contract "missing_host_install_setup"
  else
    record_help="$(
      bash -lc "source '${ROOT_DIR}/install/setup.bash' && ros2 bag record --help" 2>&1
    )"
    record_rc=$?
    play_help="$(
      bash -lc "source '${ROOT_DIR}/install/setup.bash' && ros2 bag play --help" 2>&1
    )"
    play_rc=$?
    if [[ "${record_rc}" -ne 0 ]]; then
      fail_contract "record_help_rc=${record_rc}"
    else
      require_help_option "${record_help}" "--actions"
      require_help_option "${record_help}" "--all-actions"
      require_help_option "${record_help}" "--exclude-actions"
    fi
    if [[ "${play_rc}" -ne 0 ]]; then
      fail_contract "play_help_rc=${play_rc}"
    else
      require_help_option "${play_help}" "--actions"
      require_help_option "${play_help}" "--exclude-actions"
      require_help_option "${play_help}" "--send-actions-as-client"
    fi
  fi
fi

if [[ "${FAILURES}" -ne 0 ]]; then
  echo "rmw_mdds_action_bag_contracts_failed count=${FAILURES}" >&2
  exit 1
fi

echo "RESULT|rmw_mdds_action_bag_contracts|PASS"
echo "rmw_mdds_action_bag_contracts_ok"
