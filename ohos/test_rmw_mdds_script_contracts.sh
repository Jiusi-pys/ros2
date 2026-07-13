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
GATEWAY_DOMAIN_RENDERER="${ROOT_DIR}/ohos/tools/render_rmw_mdds_gateway_config.py"
GATEWAY_MATRIX_TEMPLATE="${ROOT_DIR}/ohos/tools/gateway_rmw_mdds_matrix.yaml"
DOCTOR_SCRIPT="${ROOT_DIR}/ohos/tools/run_rmw_mdds_doctor.sh"
COVERAGE2_SCRIPT="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_coverage2.sh"
DEPLOY_SCRIPT="${ROOT_DIR}/ohos/tools/deploy_rmw_mdds_delta.sh"
FULL_OVERLAY_DEPLOY_SCRIPT="${ROOT_DIR}/ohos/tools/deploy_rmw_mdds_full_overlay.sh"
BOARD_TEST_RMW_SCRIPT="${ROOT_DIR}/ohos/tools/run_rmw_mdds_test_rmw_board.sh"
BOARD_TEST_RMW_RUNNER="${ROOT_DIR}/ohos/tools/rmw_mdds_test_rmw_board_runner.sh"
DYNAMIC_BROKER_LOAN_BOARD_SCRIPT="${ROOT_DIR}/ohos/tools/run_rmw_mdds_broker_dynamic_loan_board.sh"
DYNAMIC_BROKER_LOAN_BOARD_RUNNER="${ROOT_DIR}/ohos/tools/rmw_mdds_dynamic_loan_probe/broker_dynamic_loan_board_runner.sh"
SERVICE_STRESS_GATE_SCRIPT="${ROOT_DIR}/ohos/tools/run_rmw_mdds_service_stress_gate.py"
STAGE_RUNTIME_SCRIPT="${ROOT_DIR}/ohos/stage_colcon_runtime_closure.sh"
COLCON_RK3588A_SCRIPT="${ROOT_DIR}/ohos/colcon_rk3588a.sh"
TEST_RMW_IMPLEMENTATION_CMAKE="${ROOT_DIR}/src/ros2/rmw_implementation/test_rmw_implementation/CMakeLists.txt"
SINGLE_PACKAGE_BUILD_SCRIPT="${ROOT_DIR}/ohos/build_ros2_package.sh"
ARTIFACT_CONTRACT_SCRIPT="${ROOT_DIR}/ohos/test_rmw_mdds_artifact_contracts.sh"
PSUTIL_STUB="${ROOT_DIR}/ohos/python_stubs/psutil.py"
FULLSTACK_TSAN_SCRIPT="${ROOT_DIR}/ohos/tools/run_rmw_mdds_fullstack_tsan_board.sh"
TSAN_COMPAT_BUILD_SCRIPT="${ROOT_DIR}/ohos/tools/build_tsan_ohos_runtime_compat.sh"
TSAN_COMPAT_SOURCE="${ROOT_DIR}/ohos/tools/tsan_ohos_runtime_compat.cpp"
RMW_IMPLEMENTATION_QOS_PATCH="${ROOT_DIR}/ohos/patches/0008-rmw-implementation-destroy-qos-test-entities.patch"
CLI_SMOKE_SCRIPT="${ROOT_DIR}/ohos/tools/rmw_mdds_cli_smoke.sh"

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

require_posix_sh() {
  local file="$1"
  [[ "$(head -n 1 "${file}")" == '#!/bin/sh' ]] ||
    fail "${file} must use the board-available /bin/sh interpreter"
  sh -n "${file}"
  require_absent "${file}" '\[\['
  require_absent "${file}" '(^|[[:space:];])source[[:space:]]'
  require_absent "${file}" 'pipefail'
  require_absent "${file}" '(^|[[:space:];])local[[:space:]]'
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

require_occurrences() {
  local file="$1"
  local pattern="$2"
  local expected="$3"
  local actual
  actual="$(grep -cE -- "${pattern}" "${file}" || true)"
  [[ "${actual}" -eq "${expected}" ]] ||
    fail "${file} expected ${expected} occurrences of ${pattern}, found ${actual}"
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

require_graceful_broker_cleanup() {
  local file="$1"
  require_contains "${file}" "[Gg]raceful.*broker|broker.*[Gg]raceful"
  require_contains "${file}" "kill -0"
  require_contains "${file}" "kill -9"
}

require_no_rmw_implementation_preload_dependency() {
  local hits
  hits="$(
    grep -RInE 'librmw_implementation\.so|LD_PRELOAD=.*librmw_implementation' \
      "${ROOT_DIR}/ohos/tools"/*.sh 2>/dev/null |
      grep -vF "${FULLSTACK_TSAN_SCRIPT}:" || true
  )"
  [[ -z "${hits}" ]] || fail "rmw_mdds board scripts still depend on explicit librmw_implementation preload: ${hits}"
}

require_tsan_runner_usage() {
  local output status
  set +e
  output="$("${FULLSTACK_TSAN_SCRIPT}" 2>&1)"
  status=$?
  set -e
  [[ ${status} -eq 2 ]] || fail "TSAN runner missing-hash status must be 2, got ${status}"
  grep -q "usage:" <<< "${output}" || fail "TSAN runner missing usage output"
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

require_gateway_domain_renderer() {
  local rendered domain_zero
  [[ -f "${GATEWAY_DOMAIN_RENDERER}" ]] || fail "missing gateway domain renderer ${GATEWAY_DOMAIN_RENDERER}"
  python3 -m py_compile "${GATEWAY_DOMAIN_RENDERER}"
  rendered="$(mktemp)"
  domain_zero="$(mktemp)"
  python3 "${GATEWAY_DOMAIN_RENDERER}" \
    --template "${GATEWAY_MATRIX_TEMPLATE}" --output "${rendered}" --domain 42
  python3 "${GATEWAY_DOMAIN_RENDERER}" \
    --template "${GATEWAY_MATRIX_TEMPLATE}" --output "${domain_zero}" --domain 0
  grep -q 'mddsTopicName: d42/rt/mx_chatter' "${rendered}" ||
    fail "gateway renderer did not add the requested domain namespace"
  grep -q 'mddsTopicName: rt/mx_chatter' "${domain_zero}" ||
    fail "gateway renderer did not preserve the domain-zero topic contract"
  rm -f "${rendered}" "${domain_zero}"
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
require_file "${COVERAGE2_SCRIPT}"
require_file "${DEPLOY_SCRIPT}"
require_file "${FULL_OVERLAY_DEPLOY_SCRIPT}"
require_file "${BOARD_TEST_RMW_SCRIPT}"
require_file "${BOARD_TEST_RMW_RUNNER}"
require_file "${DYNAMIC_BROKER_LOAN_BOARD_SCRIPT}"
require_file "${DYNAMIC_BROKER_LOAN_BOARD_RUNNER}"
[[ -f "${SERVICE_STRESS_GATE_SCRIPT}" ]] || fail "missing script ${SERVICE_STRESS_GATE_SCRIPT}"
python3 -m py_compile "${SERVICE_STRESS_GATE_SCRIPT}"
require_file "${STAGE_RUNTIME_SCRIPT}"
require_file "${COLCON_RK3588A_SCRIPT}"
require_file "${SINGLE_PACKAGE_BUILD_SCRIPT}"
require_file "${ARTIFACT_CONTRACT_SCRIPT}"
require_file "${FULLSTACK_TSAN_SCRIPT}"
require_file "${TSAN_COMPAT_BUILD_SCRIPT}"
require_file "${CLI_SMOKE_SCRIPT}"
require_posix_sh "${CLI_SMOKE_SCRIPT}"
require_contains "${CLI_SMOKE_SCRIPT}" "CHILD_PIDS"
require_contains "${CLI_SMOKE_SCRIPT}" "ps -ef"
require_contains "${CLI_SMOKE_SCRIPT}" "kill -9"
require_contains "${CLI_SMOKE_SCRIPT}" "BROKER_SOCKET_OWNED"
require_contains "${CLI_SMOKE_SCRIPT}" "RMW_MDDS_BROKER_SOCKET"
require_contains "${CLI_SMOKE_SCRIPT}" "COLCON_CURRENT_PREFIX"
[[ -f "${PSUTIL_STUB}" ]] || fail "missing psutil stub ${PSUTIL_STUB}"
[[ -f "${TSAN_COMPAT_SOURCE}" ]] || fail "missing TSAN compatibility source ${TSAN_COMPAT_SOURCE}"
[[ -f "${RMW_IMPLEMENTATION_QOS_PATCH}" ]] ||
  fail "missing RMW implementation QoS lifecycle patch ${RMW_IMPLEMENTATION_QOS_PATCH}"
require_psutil_stub_network_report_shape
require_gateway_domain_renderer
require_no_rmw_implementation_preload_dependency
require_tsan_runner_usage
require_graceful_broker_cleanup "${CROSS_MDDS_SCRIPT}"
require_graceful_broker_cleanup "${M2M_SCRIPT}"
require_graceful_broker_cleanup "${BOARD_TEST_RMW_RUNNER}"
require_graceful_broker_cleanup "${DYNAMIC_BROKER_LOAN_BOARD_RUNNER}"
require_colcon_overlay_package_dir_override "rmw_implementation"
require_colcon_overlay_package_dir_override "rcl"
require_colcon_overlay_package_dir_override "rcl_action"
require_colcon_overlay_package_dir_override "rclcpp"
require_colcon_overlay_package_dir_override "rclcpp_action"
require_colcon_overlay_package_dir_override "rclcpp_components"
require_colcon_overlay_package_dir_override "rclcpp_lifecycle"
require_colcon_overlay_package_dir_override "rmw_fastrtps_shared_cpp"
require_colcon_overlay_package_dir_override "rmw_fastrtps_cpp"
require_colcon_overlay_package_dir_override "rmw_fastrtps_dynamic_cpp"
require_colcon_overlay_package_dir_override "rmw_cyclonedds_cpp"
require_colcon_overlay_package_dir_override "demo_nodes_cpp"
require_colcon_overlay_package_dir_override "action_tutorials_cpp"
require_colcon_overlay_package_dir_override "test_interface_files"
require_colcon_overlay_package_dir_override "test_msgs"
require_colcon_overlay_package_dir_override "osrf_testing_tools_cpp"
require_colcon_overlay_package_dir_override "ament_lint_auto"
require_colcon_overlay_package_dir_override "ament_lint_common"
require_contains "${CLI_SMOKE_SCRIPT}" '^[[:space:]]*set[[:space:]]+\+u$'
require_contains "${CLI_SMOKE_SCRIPT}" '^[[:space:]]*set[[:space:]]+-u$'
require_contains "${TEST_RMW_IMPLEMENTATION_CMAKE}" "option\(TEST_RMW_IMPLEMENTATION_ENABLE_LINT"
require_contains "${COLCON_RK3588A_SCRIPT}" "TEST_RMW_IMPLEMENTATION_ENABLE_LINT=OFF"
require_occurrences "${COLCON_RK3588A_SCRIPT}" "append_package_cmake_args" 3
require_contains "${COLCON_RK3588A_SCRIPT}" "ROSBAG2_PY_OHOS_TARGET_PYTHON_INCLUDE_DIR"
require_contains "${COLCON_RK3588A_SCRIPT}" "src/ros2/rmw_fastrtps"
require_contains "${COLCON_RK3588A_SCRIPT}" "src/ros2/rmw_cyclonedds"
require_contains "${COLCON_RK3588A_SCRIPT}" "ROS2_OHOS_CYCLONEDDS_INSTALL_DIR"
require_contains "${COLCON_RK3588A_SCRIPT}" 'libddsc\.so'
require_contains "${COLCON_RK3588A_SCRIPT}" "ROS2_OHOS_LZ4_INCLUDE_DIR"
require_contains "${COLCON_RK3588A_SCRIPT}" "ROS2_OHOS_LZ4_LIBRARY"
require_contains "${COLCON_RK3588A_SCRIPT}" "ROS2_OHOS_ZSTD_INCLUDE_DIR"
require_contains "${COLCON_RK3588A_SCRIPT}" "ROS2_OHOS_ZSTD_LIBRARY"
require_contains "${COLCON_RK3588A_SCRIPT}" "ROS2_OHOS_EIGEN3_INCLUDE_DIR"
require_contains "${COLCON_RK3588A_SCRIPT}" "Eigen3Config.cmake"
require_contains "${COLCON_RK3588A_SCRIPT}" "ROS2_OHOS_BUILD_TESTING"
require_contains "${COLCON_RK3588A_SCRIPT}" "ROSBAG2_PY_OHOS_TARGET_PYTHON_LIBRARY"
require_contains "${COLCON_RK3588A_SCRIPT}" "ROSBAG2_PY_OHOS_TARGET_EXTENSION_SUFFIX"
require_contains "${SINGLE_PACKAGE_BUILD_SCRIPT}" "ROS2_OHOS_OPENSSL_INCLUDE_DIR"
require_contains "${SINGLE_PACKAGE_BUILD_SCRIPT}" "ROS2_OHOS_OPENSSL_CRYPTO_LIBRARY"
require_contains "${SINGLE_PACKAGE_BUILD_SCRIPT}" "RMW_MDDS_TARGET_OPENSSL_INCLUDE_DIR"
require_contains "${SINGLE_PACKAGE_BUILD_SCRIPT}" "RMW_MDDS_TARGET_OPENSSL_CRYPTO_LIBRARY"

require_contains "${FULLSTACK_TSAN_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${FULLSTACK_TSAN_SCRIPT}" "LD_PRELOAD=.*librmw_implementation.so"
require_contains "${FULLSTACK_TSAN_SCRIPT}" "EXPECTED_BRIDGE_SHA"
require_contains "${FULLSTACK_TSAN_SCRIPT}" "RMW_MDDS_TSAN_REPORT"
require_contains "${FULLSTACK_TSAN_SCRIPT}" "test_tsan_fail"
require_contains "${FULLSTACK_TSAN_SCRIPT}" "broker_tsan_files"
require_contains "${FULLSTACK_TSAN_SCRIPT}" "broker_tsan_text"
require_contains "${FULLSTACK_TSAN_SCRIPT}" "broker_alive"
require_contains "${FULLSTACK_TSAN_SCRIPT}" '"\$\{total\}" -eq 16'
require_contains "${FULLSTACK_TSAN_SCRIPT}" '"\$\{bridge_sha\}" = "\$\{EXPECTED_BRIDGE_SHA\}"'
require_contains "${FULLSTACK_TSAN_SCRIPT}" "BOARD_RC=0"
require_contains "${TSAN_COMPAT_BUILD_SCRIPT}" "-fno-sanitize=thread"
require_contains "${TSAN_COMPAT_BUILD_SCRIPT}" "__tsan::OnReport"
require_contains "${TSAN_COMPAT_BUILD_SCRIPT}" "__tsan::OnFinalize"
require_contains "${TSAN_COMPAT_BUILD_SCRIPT}" "__tsan_on_finalize"
require_contains "${TSAN_COMPAT_SOURCE}" "RMW_MDDS_TSAN_REPORT"
require_contains "${TSAN_COMPAT_SOURCE}" "TSAN_REPORT_EXIT_CODE = 66"
require_contains "${RMW_IMPLEMENTATION_QOS_PATCH}" "rmw_destroy_client"
require_contains "${RMW_IMPLEMENTATION_QOS_PATCH}" "rmw_destroy_service"

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
require_usage "${COVERAGE2_SCRIPT}"
require_usage "${DEPLOY_SCRIPT}"
require_usage "${FULL_OVERLAY_DEPLOY_SCRIPT}"
require_usage "${DYNAMIC_BROKER_LOAN_BOARD_SCRIPT}"

require_contains "${SERVICE_STRESS_GATE_SCRIPT}" "def remote_stop_stress_command"
require_contains "${SERVICE_STRESS_GATE_SCRIPT}" "STRESS_STOP_DONE"
require_contains "${SERVICE_STRESS_GATE_SCRIPT}" "summary.json"
require_contains "${SERVICE_STRESS_GATE_SCRIPT}" "remote_stop_stress_command\\(\\)"
require_contains "${SERVICE_STRESS_GATE_SCRIPT}" "created_expected"
require_contains "${SERVICE_STRESS_GATE_SCRIPT}" "summary\\[\"CLIENT_CREATED\"\\] == created_expected"

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
require_contains "${MATRIX_SCRIPT}" "MDDS_GATEWAY_CONFIG_TEMPLATE"
require_contains "${MATRIX_SCRIPT}" "GATEWAY_DOMAIN_RENDERER"
require_contains "${MATRIX_SCRIPT}" 'OHOS_HDC_BIN="\$\{HDC_BIN\}"'
require_contains "${MATRIX_SCRIPT}" 'gateway_topic\(\)'
require_contains "${MATRIX_SCRIPT}" "GATEWAY_START_TIMEOUT_SECONDS"
require_contains "${MATRIX_SCRIPT}" "RESULT\\|gateway_start\\|FAIL"
require_contains "${MATRIX_SCRIPT}" "rmw_mdds_broker"
require_absent "${MATRIX_SCRIPT}" "pidof mdds_dds_gateway"
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
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "RMW_MDDS_SERVICE_REQUESTS"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "RMW_MDDS_SERVICE_PROGRESS_INTERVAL"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "RMW_MDDS_HDC_RETRY_ATTEMPTS"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "Connect server failed"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "RESULT\\|mdds_service_a_to_b\\|PASS"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "RESULT\\|mdds_service_soak\\|PASS"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "TRIGGER_RESPONSE success="
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "TRIGGER_CLIENT_DONE"
require_contains "${CROSS_MDDS_SERVICE_SCRIPT}" "TRIGGER_SERVER_DONE"
require_absent "${CROSS_MDDS_SERVICE_SCRIPT}" "&&[[:space:]]+nohup"
require_absent "${CROSS_MDDS_SERVICE_SCRIPT}" "SERVICE_TIMEOUT_SECONDS \\+ WARMUP_SECONDS"

require_contains "${M2M_SCRIPT}" "device A and B must be distinct"
require_contains "${M2M_SCRIPT}" "RESULT\\|m2m_pubsub_std_msgs_string\\|PASS"
require_contains "${M2M_SCRIPT}" "RESULT\\|m2m_service_add_two_ints\\|PASS"
require_contains "${M2M_SCRIPT}" "RESULT\\|m2m_action_fibonacci\\|PASS"
require_contains "${M2M_SCRIPT}" "M2M_SUMMARY pass="
require_contains "${M2M_SCRIPT}" "kill_lane_processes"
require_contains "${M2M_SCRIPT}" "HOME=/data/local/tmp"
require_contains "${M2M_SCRIPT}" "ROS_LOG_DIR=\\$\\{LOG\\}"
require_contains "${M2M_SCRIPT}" "\\$\\{PFX\\}/lib/demo_nodes_cpp/add_two_ints_server"
require_contains "${M2M_SCRIPT}" "\\$\\{PFX\\}/lib/action_tutorials_cpp/fibonacci_action_server"
require_contains "${M2M_SCRIPT}" "RMW_MDDS_PUBLISH_DONE"
require_contains "${M2M_SCRIPT}" '"\$RX" -eq 40'
require_absent "${M2M_SCRIPT}" "sleep 22"
require_absent "${M2M_SCRIPT}" "WARN:lost"

require_contains "${COVERAGE2_SCRIPT}" "RMW_MDDS_COVERAGE2_ONLY=large\\|service_large\\|transient\\|liveliness\\|bag"
require_contains "${COVERAGE2_SCRIPT}" "rcl_interfaces.srv"
require_contains "${COVERAGE2_SCRIPT}" "SetParameters"
require_contains "${COVERAGE2_SCRIPT}" "RESULT\\|cov2_\\$\\{name\\}\\|PASS"
require_contains "${COVERAGE2_SCRIPT}" "service_lane service_large512k"
require_contains "${COVERAGE2_SCRIPT}" "service_lane service_large1m"
require_contains "${COVERAGE2_SCRIPT}" "service_lane service_large1500k"
require_contains "${COVERAGE2_SCRIPT}" "RMW_MDDS_COVERAGE2_LARGE_EXTRA_CASES"
require_contains "${COVERAGE2_SCRIPT}" "RMW_MDDS_COVERAGE2_SERVICE_EXTRA_CASES"
require_contains "${COVERAGE2_SCRIPT}" "RMW_MDDS_COVERAGE2_SKIP_DEFAULT_LARGE"
require_contains "${COVERAGE2_SCRIPT}" "RMW_MDDS_COVERAGE2_SKIP_DEFAULT_SERVICE_LARGE"
require_contains "${COVERAGE2_SCRIPT}" '"\$\{sent:-0\}" -eq "\$\{times\}"'
require_contains "${COVERAGE2_SCRIPT}" '"\$\{requests:-0\}" -eq "\$\{times\}"'
require_contains "${COVERAGE2_SCRIPT}" '"\$\{valid:-0\}" -eq "\$\{times\}"'
require_contains "${COVERAGE2_SCRIPT}" "grep -c '\\^data: TLRETAIN\\\$'"
require_contains "${COVERAGE2_SCRIPT}" '"\$\{TLRX:-0\}" -eq 1'
require_contains "${COVERAGE2_SCRIPT}" "parse_extra_case"
require_fail_path_exits_nonzero "${COVERAGE2_SCRIPT}"
require_absent "${M2M_SCRIPT}" "ros2 run demo_nodes_cpp add_two_ints_server"
require_absent "${M2M_SCRIPT}" "ros2 run action_tutorials_cpp fibonacci_action_server"
require_fail_path_exits_nonzero "${M2M_SCRIPT}"

require_contains "${GATEWAY_SERVICE_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${GATEWAY_SERVICE_SCRIPT}" "RMW_IMPLEMENTATION=rmw_fastrtps_cpp"
require_contains "${GATEWAY_SERVICE_SCRIPT}" "MDDS_GATEWAY_SERVICES"
require_contains "${GATEWAY_SERVICE_SCRIPT}" "GATEWAY_DOMAIN_RENDERER"
require_contains "${GATEWAY_SERVICE_SCRIPT}" 'gateway_name\(\)'
require_contains "${GATEWAY_SERVICE_SCRIPT}" "wait_gateway_service_stats"
require_contains "${GATEWAY_SERVICE_SCRIPT}" "requests="
require_contains "${GATEWAY_SERVICE_SCRIPT}" "replies="
require_contains "${GATEWAY_SERVICE_SCRIPT}" "RESULT\\|service_addints_mdds_client_to_fastrtps_server\\|PASS"
require_fail_path_exits_nonzero "${GATEWAY_SERVICE_SCRIPT}"

require_contains "${GATEWAY_ACTION_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${GATEWAY_ACTION_SCRIPT}" "RMW_IMPLEMENTATION=rmw_fastrtps_cpp"
require_contains "${GATEWAY_ACTION_SCRIPT}" "MDDS_GATEWAY_SERVICES"
require_contains "${GATEWAY_ACTION_SCRIPT}" "GATEWAY_DOMAIN_RENDERER"
require_contains "${GATEWAY_ACTION_SCRIPT}" 'gateway_name\(\)'
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
require_contains "${GATEWAY_LIFECYCLE_SCRIPT}" "GATEWAY_DOMAIN_RENDERER"
require_contains "${GATEWAY_LIFECYCLE_SCRIPT}" 'gateway_name\(\)'
require_contains "${GATEWAY_LIFECYCLE_SCRIPT}" "RESULT\\|lifecycle_mdds_client_to_fastrtps_node\\|PASS"
require_fail_path_exits_nonzero "${GATEWAY_LIFECYCLE_SCRIPT}"

require_contains "${GATEWAY_PARAMS_SCRIPT}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${GATEWAY_PARAMS_SCRIPT}" "RMW_IMPLEMENTATION=rmw_fastrtps_cpp"
require_contains "${GATEWAY_PARAMS_SCRIPT}" "rcl_interfaces/srv"
require_contains "${GATEWAY_PARAMS_SCRIPT}" "GATEWAY_DOMAIN_RENDERER"
require_contains "${GATEWAY_PARAMS_SCRIPT}" 'gateway_name\(\)'
require_contains "${GATEWAY_PARAMS_SCRIPT}" "RMW_MDDS_NODE_SYNC_TOPIC"
require_contains "${GATEWAY_PARAMS_SCRIPT}" "RMW_MDDS_BROKER_SOCKET"
require_contains "${GATEWAY_PARAMS_SCRIPT}" "RMW_MDDS_BROKER_LOG"
require_contains "${GATEWAY_PARAMS_SCRIPT}" "RMW_MDDS_GRAPH_DEBUG=1"
require_contains "${GATEWAY_PARAMS_SCRIPT}" 'sh_cap "\$A" "mkdir -p \$\{LOG\}'
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
require_contains "${DEPLOY_SCRIPT}" "rmw_mdds_bridge_protected_transport_probe"
require_contains "${DEPLOY_SCRIPT}" "librosbag2_cpp.so"
require_contains "${DEPLOY_SCRIPT}" "librosbag2_transport.so"
require_contains "${DEPLOY_SCRIPT}" "lib/python3.12/site-packages/rosbag2_py"
require_contains "${DEPLOY_SCRIPT}" "libsoftbus_client.z.so"
require_contains "${DEPLOY_SCRIPT}" "rmw_typesupport"
require_contains "${DEPLOY_SCRIPT}" "RMW_MDDS_REMOTE_BRIDGE_ALIAS"
require_contains "${DEPLOY_SCRIPT}" "mdds_bridge_runtime_alias_deploy"
require_contains "${DEPLOY_SCRIPT}" "hdc_send_verify"
require_contains "${DEPLOY_SCRIPT}" "tar"
require_contains "${DEPLOY_SCRIPT}" "RESULT\\|rmw_mdds_deploy\\|PASS"
require_absent "${DEPLOY_SCRIPT}" "lib/librcl.so"
require_absent "${DEPLOY_SCRIPT}" "_rclpy_pybind11"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "RMW_MDDS_FULL_OVERLAY"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "rmw_mdds_dynamic_loan_probe"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "rmw_mdds_broker_dynamic_loan_probe"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "broker_dynamic_loan_board_runner.sh"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "librosidl_runtime_c.so"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "libstd_msgs__rosidl_typesupport_introspection_cpp.so"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "librmw_fastrtps_shared_cpp.so"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "librmw_cyclonedds_cpp.so"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "libddsc.so"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "RESULT\\|rmw_mdds_full_overlay_deploy\\|PASS"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "hdc_send_verify"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "incoming"
require_contains "${FULL_OVERLAY_DEPLOY_SCRIPT}" "backup"
require_contains "${BOARD_TEST_RMW_SCRIPT}" "test_rmw_implementation"
require_contains "${BOARD_TEST_RMW_SCRIPT}" "libtest_msgs"
require_contains "${BOARD_TEST_RMW_SCRIPT}" "libmemory_tools"
require_contains "${BOARD_TEST_RMW_SCRIPT}" "hdc_send_verify"
require_contains "${BOARD_TEST_RMW_SCRIPT}" "RESULT\|rmw_mdds_test_rmw_board\|PASS"
require_contains "${BOARD_TEST_RMW_RUNNER}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${BOARD_TEST_RMW_RUNNER}" "RMW_MDDS_BROKER_SOCKET"
require_contains "${BOARD_TEST_RMW_RUNNER}" "test_subscription_allocator"
require_contains "${BOARD_TEST_RMW_RUNNER}" "SKIP_ONLY"
require_contains "${BOARD_TEST_RMW_RUNNER}" "TEST_RMW_GREEN_SUMMARY"
require_contains "${BOARD_TEST_RMW_RUNNER}" "BOARD_RC=0"
require_contains "${BOARD_TEST_RMW_RUNNER}" "start_suite_broker"
require_contains "${BOARD_TEST_RMW_RUNNER}" "suite\.broker\.log"
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_SCRIPT}" "rmw_mdds_broker_dynamic_loan_probe"
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_SCRIPT}" "--self-test"
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_SCRIPT}" "--remote-subscriber"
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_SCRIPT}" "--remote-publisher"
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_SCRIPT}" "remote_shapes=6"
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_SCRIPT}" "rmw_mdds_broker_dynamic_loan_board_ok"
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_RUNNER}" "RMW_IMPLEMENTATION=rmw_mdds_cpp"
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_RUNNER}" "RMW_MDDS_BROKER=1"
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_RUNNER}" "pool_count"
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_RUNNER}" "BOARD_RC="
require_contains "${DYNAMIC_BROKER_LOAN_BOARD_RUNNER}" "mw_mdds_broker --socket"
require_absent "${FULL_OVERLAY_DEPLOY_SCRIPT}" "awk"
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
require_no_hardcoded_device_ids "${FULL_OVERLAY_DEPLOY_SCRIPT}"
require_no_hardcoded_device_ids "${BOARD_TEST_RMW_SCRIPT}"
require_no_hardcoded_device_ids "${BOARD_TEST_RMW_RUNNER}"
require_no_hardcoded_device_ids "${DYNAMIC_BROKER_LOAN_BOARD_SCRIPT}"
require_no_hardcoded_device_ids "${DYNAMIC_BROKER_LOAN_BOARD_RUNNER}"
require_no_hardcoded_device_ids "${FULLSTACK_TSAN_SCRIPT}"
require_no_hardcoded_device_ids "${TSAN_COMPAT_BUILD_SCRIPT}"
require_no_hardcoded_device_ids "${TSAN_COMPAT_SOURCE}"
require_no_hardcoded_device_ids "${RMW_IMPLEMENTATION_QOS_PATCH}"

echo "rmw_mdds_script_contracts_ok"
