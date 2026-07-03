#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

LOCAL_SCRIPT="${ROOT_DIR}/ohos/tools/run_rmw_mdds_pubsub.sh"
BROKER_SCRIPT="${ROOT_DIR}/ohos/tools/run_rmw_mdds_broker_pubsub.sh"
BROKER_SERVICE_SCRIPT="${ROOT_DIR}/ohos/tools/run_rmw_mdds_broker_service.sh"
BROKER_CTL_SCRIPT="${ROOT_DIR}/ohos/tools/rmw_mdds_broker_ctl.sh"
CROSS_SCRIPT="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_fastdds.sh"
CROSS_MDDS_SCRIPT="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds.sh"
CROSS_MDDS_SERVICE_SCRIPT="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_service.sh"
MATRIX_SCRIPT="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_matrix.sh"
M2M_SCRIPT="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_m2m.sh"
GATEWAY_SERVICE_SCRIPT="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_service_gw.sh"
GATEWAY_ACTION_SCRIPT="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_action_gw.sh"
GATEWAY_LIFECYCLE_SCRIPT="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_lifecycle_gw.sh"
GATEWAY_PARAMS_SCRIPT="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_params_gw.sh"
DOCTOR_SCRIPT="${ROOT_DIR}/ohos/tools/run_rmw_mdds_doctor.sh"
DEPLOY_SCRIPT="${ROOT_DIR}/ohos/tools/deploy_rmw_mdds_delta.sh"
STAGE_RUNTIME_SCRIPT="${ROOT_DIR}/ohos/stage_colcon_runtime_closure.sh"
COLCON_RK3588A_SCRIPT="${ROOT_DIR}/ohos/colcon_rk3588a.sh"
ARTIFACT_CONTRACT_SCRIPT="${ROOT_DIR}/ohos/test_rmw_mdds_artifact_contracts.sh"
PSUTIL_STUB="${ROOT_DIR}/ohos/python_stubs/psutil.py"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

require_file() {
  local file="$1"
  [[ -f "${file}" ]] || fail "missing script ${file}"
  [[ -x "${file}" ]] || fail "script is not executable ${file}"
  bash -n "${file}"
}

require_usage() {
  local file="$1"
  local output
  set +e
  output="$("${file}" 2>&1)"
  local status=$?
  set -e
  [[ ${status} -ne 0 ]] || fail "${file} should reject missing arguments"
  grep -q "Usage:" <<< "${output}" || fail "${file} missing Usage output"
}

require_contains() {
  local file="$1"
  local pattern="$2"
  grep -qE -- "${pattern}" "${file}" || fail "${file} missing pattern ${pattern}"
}

require_fail_path_exits_nonzero() {
  local file="$1"
  require_contains "${file}" "RESULT\\|.*\\|FAIL"
  require_contains "${file}" "exit 1"
}

require_absent() {
  local file="$1"
  local pattern="$2"
  ! grep -qE -- "${pattern}" "${file}" || fail "${file} contains fragile pattern ${pattern}"
}

require_no_rmw_implementation_preload_dependency() {
  local hits
  hits="$(
    grep -RInE 'librmw_implementation\.so|LD_PRELOAD=.*librmw_implementation' \
      "${ROOT_DIR}/ohos/tools"/*.sh 2>/dev/null || true
  )"
  [[ -z "${hits}" ]] || fail "rmw_mdds board scripts still depend on explicit librmw_implementation preload: ${hits}"
}

require_no_hardcoded_device_ids() {
  local file="$1"
  ! grep -qE '3e01ff[0-9a-f]+' "${file}" || fail "${file} hardcodes lab device IDs"
}

require_psutil_stub_network_report_shape() {
  PYTHONPATH="${ROOT_DIR}/ohos/python_stubs" python3 - <<'PY' || exit 1
import psutil

stats = psutil.net_if_stats()
if 'lo' not in stats:
    raise SystemExit('psutil.net_if_stats missing lo interface')
if not hasattr(stats['lo'], 'mtu'):
    raise SystemExit('psutil.net_if_stats entries must expose mtu')
if stats['lo'].mtu <= 0:
    raise SystemExit('psutil.net_if_stats lo mtu must be positive')
PY
}

require_colcon_overlay_package_dir_override() {
  local package_name="$1"
  local body
  body="$(
    awk '
      /^OVERLAY_PACKAGE_DIR_OVERRIDES=\(/ { in_body = 1; next }
      in_body && /^\)/ { exit }
      in_body { print }
    ' "${COLCON_RK3588A_SCRIPT}"
  )"
  grep -qx "  ${package_name}" <<<"${body}" ||
    fail "${COLCON_RK3588A_SCRIPT} does not prefer overlay CMake config for ${package_name}"
}

require_file "${LOCAL_SCRIPT}"
require_file "${BROKER_SCRIPT}"
require_file "${BROKER_SERVICE_SCRIPT}"
require_file "${BROKER_CTL_SCRIPT}"
require_file "${CROSS_SCRIPT}"
require_file "${CROSS_MDDS_SCRIPT}"
require_file "${CROSS_MDDS_SERVICE_SCRIPT}"
require_file "${MATRIX_SCRIPT}"
require_file "${M2M_SCRIPT}"
require_file "${GATEWAY_SERVICE_SCRIPT}"
require_file "${GATEWAY_ACTION_SCRIPT}"
require_file "${GATEWAY_LIFECYCLE_SCRIPT}"
require_file "${GATEWAY_PARAMS_SCRIPT}"
require_file "${DOCTOR_SCRIPT}"
require_file "${DEPLOY_SCRIPT}"
require_file "${STAGE_RUNTIME_SCRIPT}"
require_file "${COLCON_RK3588A_SCRIPT}"
require_file "${ARTIFACT_CONTRACT_SCRIPT}"
[[ -f "${PSUTIL_STUB}" ]] || fail "missing psutil stub ${PSUTIL_STUB}"
require_psutil_stub_network_report_shape
require_no_rmw_implementation_preload_dependency
require_colcon_overlay_package_dir_override "rmw_implementation"
require_colcon_overlay_package_dir_override "rcl"
require_colcon_overlay_package_dir_override "rcl_action"
require_colcon_overlay_package_dir_override "rclcpp"
require_colcon_overlay_package_dir_override "rclcpp_action"
require_colcon_overlay_package_dir_override "rclcpp_components"
require_colcon_overlay_package_dir_override "rclcpp_lifecycle"
require_colcon_overlay_package_dir_override "demo_nodes_cpp"
require_colcon_overlay_package_dir_override "action_tutorials_cpp"

require_usage "${LOCAL_SCRIPT}"
require_usage "${BROKER_SCRIPT}"
require_usage "${BROKER_SERVICE_SCRIPT}"
require_usage "${BROKER_CTL_SCRIPT}"
require_usage "${CROSS_SCRIPT}"
require_usage "${CROSS_MDDS_SCRIPT}"
require_usage "${CROSS_MDDS_SERVICE_SCRIPT}"
require_usage "${MATRIX_SCRIPT}"
require_usage "${M2M_SCRIPT}"
require_usage "${GATEWAY_SERVICE_SCRIPT}"
require_usage "${GATEWAY_ACTION_SCRIPT}"
require_usage "${GATEWAY_LIFECYCLE_SCRIPT}"
require_usage "${GATEWAY_PARAMS_SCRIPT}"
require_usage "${DOCTOR_SCRIPT}"
require_usage "${DEPLOY_SCRIPT}"

require_contains "${LOCAL_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${LOCAL_SCRIPT}" "RESULT\\|rmw_mdds_pubsub\\|PASS"
require_contains "${LOCAL_SCRIPT}" "--wait-matching-subscriptions 0|-w 0"
require_contains "${LOCAL_SCRIPT}" "rmw_mdds_echo_started"
require_absent "${LOCAL_SCRIPT}" "RMW_MDDS_BRIDGE_LIBRARY"

require_contains "${BROKER_SCRIPT}" "rmw_mdds_broker"
require_contains "${BROKER_SCRIPT}" "RMW_MDDS_BROKER=1"
require_contains "${BROKER_SCRIPT}" "RMW_MDDS_BROKER_SOCKET"
require_contains "${BROKER_SCRIPT}" "LD_LIBRARY_PATH"
require_contains "${BROKER_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${BROKER_SCRIPT}" "RMW_MDDS_BRIDGE_LIBRARY.*no/such/libmdds_bridge_shared.z.so"
require_contains "${BROKER_SCRIPT}" "RESULT\\|rmw_mdds_broker_pubsub\\|PASS"
require_contains "${BROKER_SCRIPT}" "--wait-matching-subscriptions 0|-w 0"

require_contains "${BROKER_SERVICE_SCRIPT}" "rmw_mdds_broker"
require_contains "${BROKER_SERVICE_SCRIPT}" "RMW_MDDS_BROKER=1"
require_contains "${BROKER_SERVICE_SCRIPT}" "RMW_MDDS_BROKER_SOCKET"
require_contains "${BROKER_SERVICE_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${BROKER_SERVICE_SCRIPT}" "RMW_MDDS_BRIDGE_LIBRARY.*no/such/libmdds_bridge_shared.z.so"
require_contains "${BROKER_SERVICE_SCRIPT}" "std_srvs.srv"
require_contains "${BROKER_SERVICE_SCRIPT}" "start_type_description_service"
require_contains "${BROKER_SERVICE_SCRIPT}" "wait_for_service"
require_contains "${BROKER_SERVICE_SCRIPT}" "RESULT\\|rmw_mdds_broker_service\\|PASS"
require_contains "${BROKER_SERVICE_SCRIPT}" "TRIGGER_RESPONSE success=True"
require_contains "${BROKER_SERVICE_SCRIPT}" "RMW_MDDS_BROKER_MANAGED"
require_contains "${BROKER_SERVICE_SCRIPT}" "broker_managed_enabled"
require_absent "${BROKER_SERVICE_SCRIPT}" "&&[[:space:]]+nohup"

require_contains "${BROKER_CTL_SCRIPT}" "rmw_mdds_broker"
require_contains "${BROKER_CTL_SCRIPT}" "start\\|stop\\|restart\\|status"
require_contains "${BROKER_CTL_SCRIPT}" "RMW_MDDS_BROKER_SOCKET"
require_contains "${BROKER_CTL_SCRIPT}" "RMW_MDDS_BROKER_LOG_DIR"
require_contains "${BROKER_CTL_SCRIPT}" "broker.pid"
require_contains "${BROKER_CTL_SCRIPT}" "LD_LIBRARY_PATH"
require_contains "${BROKER_CTL_SCRIPT}" "RESULT\\|rmw_mdds_broker_ctl\\|PASS"
require_absent "${BROKER_CTL_SCRIPT}" "awk"

require_contains "${CROSS_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${CROSS_SCRIPT}" "RMW_IMPLEMENTATION=rmw_fastrtps_cpp"
require_contains "${CROSS_SCRIPT}" "RMW_MDDS_BROKER=1"
require_contains "${CROSS_SCRIPT}" "mdds_dds_gateway"
require_contains "${CROSS_SCRIPT}" "RESULT\\|mdds_to_fastdds\\|PASS"
require_contains "${CROSS_SCRIPT}" "RESULT\\|fastdds_to_mdds\\|PASS"
require_contains "${CROSS_SCRIPT}" "--wait-matching-subscriptions 0|-w 0"
require_contains "${CROSS_SCRIPT}" "ros2-daemon .*rmw_mdds_cpp"
require_contains "${CROSS_SCRIPT}" "topic echo .*rmw_mdds"
require_contains "${CROSS_SCRIPT}" "RMW_MDDS_POLL_TIMEOUT_SECONDS:-90"
require_contains "${CROSS_SCRIPT}" "DOMAIN_ID > 232"
require_absent "${CROSS_SCRIPT}" "&&[[:space:]]+nohup"

require_contains "${MATRIX_SCRIPT}" "MATRIX_SUMMARY pass="
require_contains "${MATRIX_SCRIPT}" "wait_gateway_counter"
require_contains "${MATRIX_SCRIPT}" "toDds"
require_contains "${MATRIX_SCRIPT}" "toMdds"
require_fail_path_exits_nonzero "${MATRIX_SCRIPT}"

require_contains "${CROSS_MDDS_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${CROSS_MDDS_SCRIPT}" "RMW_MDDS_BROKER=1"
require_contains "${CROSS_MDDS_SCRIPT}" "RESULT\\|mdds_a_to_b\\|PASS"
require_contains "${CROSS_MDDS_SCRIPT}" "RESULT\\|mdds_b_to_a\\|PASS"
require_contains "${CROSS_MDDS_SCRIPT}" "--wait-matching-subscriptions 0|-w 0"
require_absent "${CROSS_MDDS_SCRIPT}" "&&[[:space:]]+nohup"

require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "RMW_MDDS_BROKER=1"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "std_srvs.srv"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "start_type_description_service"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "RMW_MDDS_SERVICE_WARMUP_SECONDS"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "RMW_MDDS_HDC_RETRY_ATTEMPTS"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "Connect server failed"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "RESULT\\|mdds_service_a_to_b\\|PASS"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "TRIGGER_RESPONSE success=True"
require_absent "${CROSS_MDDS_SERVICE_SCRIPT}" "&&[[:space:]]+nohup"
require_absent "${CROSS_MDDS_SERVICE_SCRIPT}" "SERVICE_TIMEOUT_SECONDS \\+ WARMUP_SECONDS"

require_contains "${M2M_SCRIPT}" "device A and B must be distinct"
require_contains "${M2M_SCRIPT}" "RESULT\\|m2m_pubsub_std_msgs_string\\|PASS"
require_contains "${M2M_SCRIPT}" "RESULT\\|m2m_service_add_two_ints\\|PASS"
require_contains "${M2M_SCRIPT}" "RESULT\\|m2m_action_fibonacci\\|PASS"
require_contains "${M2M_SCRIPT}" "M2M_SUMMARY pass="
require_contains "${M2M_SCRIPT}" "HOME=/data/local/tmp"
require_contains "${M2M_SCRIPT}" "ROS_LOG_DIR=\\$\\{LOG\\}"
require_contains "${M2M_SCRIPT}" "\\$\\{PFX\\}/lib/demo_nodes_cpp/add_two_ints_server"
require_contains "${M2M_SCRIPT}" "\\$\\{PFX\\}/lib/action_tutorials_cpp/fibonacci_action_server"
require_absent "${M2M_SCRIPT}" "ros2 run demo_nodes_cpp add_two_ints_server"
require_absent "${M2M_SCRIPT}" "ros2 run action_tutorials_cpp fibonacci_action_server"
require_fail_path_exits_nonzero "${M2M_SCRIPT}"

require_contains "${GATEWAY_SERVICE_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${GATEWAY_SERVICE_SCRIPT}" "RMW_IMPLEMENTATION=rmw_fastrtps_cpp"
require_contains "${GATEWAY_SERVICE_SCRIPT}" "MDDS_GATEWAY_SERVICES"
require_contains "${GATEWAY_SERVICE_SCRIPT}" "wait_gateway_service_stats"
require_contains "${GATEWAY_SERVICE_SCRIPT}" "requests="
require_contains "${GATEWAY_SERVICE_SCRIPT}" "replies="
require_contains "${GATEWAY_SERVICE_SCRIPT}" "RESULT\\|service_addints_mdds_client_to_fastrtps_server\\|PASS"
require_fail_path_exits_nonzero "${GATEWAY_SERVICE_SCRIPT}"

require_contains "${GATEWAY_ACTION_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${GATEWAY_ACTION_SCRIPT}" "RMW_IMPLEMENTATION=rmw_fastrtps_cpp"
require_contains "${GATEWAY_ACTION_SCRIPT}" "MDDS_GATEWAY_SERVICES"
require_contains "${GATEWAY_ACTION_SCRIPT}" "action_tutorials_interfaces/action/Fibonacci"
require_contains "${GATEWAY_ACTION_SCRIPT}" "wait_gateway_action_stats"
require_contains "${GATEWAY_ACTION_SCRIPT}" "fibonacci/_action/send_goal"
require_contains "${GATEWAY_ACTION_SCRIPT}" "fibonacci/_action/get_result"
require_contains "${GATEWAY_ACTION_SCRIPT}" "fibonacci/_action/feedback"
require_contains "${GATEWAY_ACTION_SCRIPT}" "fibonacci/_action/status"
require_contains "${GATEWAY_ACTION_SCRIPT}" "RESULT\\|action_fibonacci_mdds_client_to_fastrtps_server\\|PASS"
require_fail_path_exits_nonzero "${GATEWAY_ACTION_SCRIPT}"

require_contains "${GATEWAY_LIFECYCLE_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${GATEWAY_LIFECYCLE_SCRIPT}" "RMW_IMPLEMENTATION=rmw_fastrtps_cpp"
require_contains "${GATEWAY_LIFECYCLE_SCRIPT}" "lifecycle_msgs/srv"
require_contains "${GATEWAY_LIFECYCLE_SCRIPT}" "RESULT\\|lifecycle_mdds_client_to_fastrtps_node\\|PASS"
require_fail_path_exits_nonzero "${GATEWAY_LIFECYCLE_SCRIPT}"

require_contains "${GATEWAY_PARAMS_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${GATEWAY_PARAMS_SCRIPT}" "RMW_IMPLEMENTATION=rmw_fastrtps_cpp"
require_contains "${GATEWAY_PARAMS_SCRIPT}" "rcl_interfaces/srv"
require_contains "${GATEWAY_PARAMS_SCRIPT}" "RESULT\\|params_cli_mdds_client_to_fastrtps_node\\|PASS"
require_fail_path_exits_nonzero "${GATEWAY_PARAMS_SCRIPT}"

require_contains "${DOCTOR_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${DOCTOR_SCRIPT}" "ros2 doctor --report"
require_contains "${DOCTOR_SCRIPT}" "timeout .*ros2 doctor --report"
require_contains "${DOCTOR_SCRIPT}" "ros2 topic list"
require_contains "${DOCTOR_SCRIPT}" "ROS_DISTRO=jazzy"
require_contains "${DOCTOR_SCRIPT}" "ROSDISTRO_INDEX_URL=file://"
require_contains "${DOCTOR_SCRIPT}" "Report entry point"
require_contains "${DOCTOR_SCRIPT}" "No module named 'rosdistro'"
require_contains "${DOCTOR_SCRIPT}" "Fail to call NetworkReport class functions"
require_contains "${DOCTOR_SCRIPT}" "Fail to call PackageReport class functions"
require_contains "${DOCTOR_SCRIPT}" "Fail to call RosdistroReport class functions"
require_contains "${DOCTOR_SCRIPT}" "librmw_mdds_cpp.so"
require_contains "${DOCTOR_SCRIPT}" "LD_LIBRARY_PATH"
require_contains "${DOCTOR_SCRIPT}" "RESULT\\|rmw_mdds_doctor\\|PASS"
require_absent "${DOCTOR_SCRIPT}" "RMW_MDDS_BRIDGE_LIBRARY"

require_contains "${STAGE_RUNTIME_SCRIPT}" "/usr/lib/python3/dist-packages/rosdistro"
require_contains "${STAGE_RUNTIME_SCRIPT}" "/usr/lib/python3/dist-packages/rosdistro-.*egg-info"
require_contains "${STAGE_RUNTIME_SCRIPT}" "/usr/lib/python3.8/distutils"
require_contains "${STAGE_RUNTIME_SCRIPT}" "stage_local_rosdistro_index"
require_contains "${STAGE_RUNTIME_SCRIPT}" "index-v4.yaml"
require_contains "${STAGE_RUNTIME_SCRIPT}" "distribution.yaml"
require_contains "${STAGE_RUNTIME_SCRIPT}" "release_platforms"

require_contains "${DEPLOY_SCRIPT}" "librmw_mdds_cpp.so"
require_contains "${DEPLOY_SCRIPT}" "rmw_mdds_broker"
require_contains "${DEPLOY_SCRIPT}" "rmw_typesupport"
require_contains "${DEPLOY_SCRIPT}" "hdc_send_verify"
require_contains "${DEPLOY_SCRIPT}" "tar"
require_contains "${DEPLOY_SCRIPT}" "RESULT\\|rmw_mdds_deploy\\|PASS"
require_absent "${DEPLOY_SCRIPT}" "lib/librcl.so"
require_absent "${DEPLOY_SCRIPT}" "_rclpy_pybind11"
require_absent "${LOCAL_SCRIPT}" "awk"
require_absent "${BROKER_SCRIPT}" "awk"
require_absent "${BROKER_SERVICE_SCRIPT}" "awk"
require_absent "${BROKER_CTL_SCRIPT}" "awk"
require_absent "${CROSS_SCRIPT}" "awk"
require_absent "${CROSS_MDDS_SCRIPT}" "awk"
require_absent "${CROSS_MDDS_SERVICE_SCRIPT}" "awk"
require_absent "${M2M_SCRIPT}" "awk"
require_absent "${GATEWAY_SERVICE_SCRIPT}" "awk"
require_absent "${GATEWAY_ACTION_SCRIPT}" "awk"
require_absent "${GATEWAY_LIFECYCLE_SCRIPT}" "awk"
require_absent "${GATEWAY_PARAMS_SCRIPT}" "awk"
require_absent "${DOCTOR_SCRIPT}" "awk"
require_absent "${DEPLOY_SCRIPT}" "awk"

require_no_hardcoded_device_ids "${LOCAL_SCRIPT}"
require_no_hardcoded_device_ids "${BROKER_SCRIPT}"
require_no_hardcoded_device_ids "${BROKER_SERVICE_SCRIPT}"
require_no_hardcoded_device_ids "${BROKER_CTL_SCRIPT}"
require_no_hardcoded_device_ids "${CROSS_SCRIPT}"
require_no_hardcoded_device_ids "${MATRIX_SCRIPT}"
require_no_hardcoded_device_ids "${CROSS_MDDS_SCRIPT}"
require_no_hardcoded_device_ids "${CROSS_MDDS_SERVICE_SCRIPT}"
require_no_hardcoded_device_ids "${M2M_SCRIPT}"
require_no_hardcoded_device_ids "${GATEWAY_SERVICE_SCRIPT}"
require_no_hardcoded_device_ids "${GATEWAY_ACTION_SCRIPT}"
require_no_hardcoded_device_ids "${GATEWAY_LIFECYCLE_SCRIPT}"
require_no_hardcoded_device_ids "${GATEWAY_PARAMS_SCRIPT}"
require_no_hardcoded_device_ids "${DOCTOR_SCRIPT}"
require_no_hardcoded_device_ids "${DEPLOY_SCRIPT}"

echo "rmw_mdds_script_contracts_ok"
