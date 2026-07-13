#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

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

require_source_file() {
  local file="$1"
  [[ -f "${file}" ]] || fail "missing source ${file}"
}

require_contains() {
  local file="$1"
  local pattern="$2"
  grep -qE -- "${pattern}" "${file}" || fail "${file} missing pattern ${pattern}"
}

require_visible_to_root_git() {
  local path="$1"
  [[ -e "${ROOT_DIR}/${path}" ]] || fail "missing ${path}"
  if ! git -C "${ROOT_DIR}" ls-files --error-unmatch "${path}" >/dev/null 2>&1 &&
      ! git -C "${ROOT_DIR}" ls-files --others --exclude-standard -- "${path}" | grep -qxF "${path}"; then
    fail "${path} is not visible to root git status"
  fi
}

require_no_child_git() {
  local path="$1"
  [[ ! -e "${ROOT_DIR}/${path}/.git" ]] || fail "${path} unexpectedly has a child .git"
}

require_no_hidden_package_sources() {
  local hidden
  hidden="$(
    git -C "${ROOT_DIR}" ls-files --others -i --exclude-standard -- "src/ros2/rmw_mdds/rmw_mdds_cpp" |
      grep -v '^src/ros2/rmw_mdds/rmw_mdds_cpp/src/log/' || true
  )"
  [[ -z "${hidden}" ]] || fail "rmw_mdds_cpp has hidden non-log files: ${hidden}"
}

require_no_auto_ld_preload_hook() {
  local package_dir="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp"
  if grep -RInE 'ament_environment_hooks|LD_PRELOAD|RMW_MDDS_NO_AUTO_PRELOAD' \
      "${package_dir}/CMakeLists.txt" "${package_dir}/hook" >/tmp/rmw_mdds_ld_preload_hits.$$ 2>/dev/null; then
    local hits
    hits="$(cat /tmp/rmw_mdds_ld_preload_hits.$$)"
    rm -f /tmp/rmw_mdds_ld_preload_hits.$$
    fail "rmw_mdds_cpp still installs or carries an automatic LD_PRELOAD hook: ${hits}"
  fi
  rm -f /tmp/rmw_mdds_ld_preload_hits.$$
}

require_no_loaned_publish_copy_fallback() {
  local source_file="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/src/rmw_unsupported.cpp"
  local body
  body="$(
    awk '
      /^rmw_ret_t rmw_publish_loaned_message\(/ { in_body = 1 }
      in_body { print }
      in_body && /^}$/ { exit }
    ' "${source_file}"
  )"
  [[ -n "${body}" ]] || fail "rmw_publish_loaned_message body not found"
  if grep -Eq 'EncodeMddsIntoBuffer|BorrowLoanedSample' <<<"${body}"; then
    fail "rmw_publish_loaned_message still has a bridge payload copy fallback in the loaned publish path"
  fi
}

require_no_unfiltered_bridge_loaned_take_payload_copy() {
  local source_file="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/src/rmw_unsupported.cpp"
  local body
  body="$(
    awk '
      /^rmw_ret_t TryTakeBridgeLoanedMessage\(/ { in_body = 1 }
      in_body { print }
      in_body && /^}$/ { exit }
    ' "${source_file}"
  )"
  [[ -n "${body}" ]] || fail "TryTakeBridgeLoanedMessage body not found"
  if grep -Eq 'std::vector<uint8_t> payload|payload\.assign' <<<"${body}"; then
    fail "TryTakeBridgeLoanedMessage still copies bridge loaned payload before checking whether a filter is active"
  fi
  if grep -Eq 'DecodeMdds|AllocateMessage|SubscriberTakeLoanedWithStorage' <<<"${body}"; then
    fail "TryTakeBridgeLoanedMessage is not a raw bridge loaned take path"
  fi
  grep -q 'SupportsRawLoanedMessage' <<<"${body}" ||
    fail "TryTakeBridgeLoanedMessage does not require raw fixed-size loaned message support"
}

run_security_enforce_fail_closed_gate() {
  local output
  if ! output="$(
      set +u
      # shellcheck source=/dev/null
      source "${ROOT_DIR}/install/setup.bash"
      set -u
      "${ROOT_DIR}/build/rmw_mdds_cpp/test_pubsub_inproc" \
        --gtest_filter='RmwMddsPubSub.SecurityEnforceInitFailsClosedWithoutPolicyEnforcement'
    )"; then
    fail "security enforce fail-closed gate failed: ${output}"
  fi
  grep -q '\[  PASSED  \] 1 test' <<<"${output}" ||
    fail "security enforce fail-closed gate did not pass exactly one test: ${output}"
  echo "RESULT|rmw_mdds_security_enforce_fail_closed|PASS"
}

HOST_CONFORMANCE_SCRIPT="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_conformance.sh"
SECURITY_CONTRACT_SCRIPT="${ROOT_DIR}/ohos/test_rmw_mdds_security_contracts.sh"
SROS2_POLICY_CONTRACT_SCRIPT="${ROOT_DIR}/ohos/test_rmw_mdds_sros2_policy_contracts.sh"
BOARD_SROS2_POLICY_PROBE="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_sros2_policy.sh"
BOARD_SROS2_PROTECTED_PROBE="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_sros2_protected.sh"
PROTECTED_TRANSPORT_PROBE_SOURCE="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/src/bridge_protected_transport_probe.cpp"
ZERO_COPY_CONTRACT_SCRIPT="${ROOT_DIR}/ohos/test_rmw_mdds_zero_copy_contracts.sh"
TYPE_DESCRIPTION_PROBE="${ROOT_DIR}/ohos/tools/run_rmw_mdds_type_description_probe.sh"
HOST_CLI_PUBSUB_PROBE="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_pubsub.sh"
HOST_CLI_SERVICE_PROBE="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_service.sh"
HOST_CLI_ACTION_PROBE="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_action.sh"
HOST_CLI_PARAMS_PROBE="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_params.sh"
HOST_CLI_LIFECYCLE_PROBE="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_lifecycle.sh"
HOST_CLI_GRAPH_PROBE="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_graph.sh"
HOST_CLI_QOS_PROBE="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_qos.sh"
HOST_CLI_TRANSIENT_LOCAL_PROBE="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_transient_local.sh"
HOST_CLI_MESSAGE_INFO_PROBE="${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_message_info.sh"
ARTIFACT_CONTRACT_SCRIPT="${ROOT_DIR}/ohos/test_rmw_mdds_artifact_contracts.sh"
ACTION_BAG_CONTRACT_SCRIPT="${ROOT_DIR}/ohos/test_rmw_mdds_action_bag_contracts.sh"

require_no_child_git "src/ros2/rmw_mdds"

require_visible_to_root_git "src/ros2/rmw_mdds/rmw_mdds_cpp/CMakeLists.txt"
require_visible_to_root_git "src/ros2/rmw_mdds/rmw_mdds_cpp/src/rmw_publisher.cpp"
require_visible_to_root_git "src/ros2/rmw_mdds/rmw_mdds_cpp/test/test_bridge_loaned_rmw.cpp"
require_visible_to_root_git "src/ros2/rmw_mdds/rmw_mdds_cpp/test/test_bridge_loaned_take_rmw.cpp"
require_file "${HOST_CONFORMANCE_SCRIPT}"
require_contains "${HOST_CONFORMANCE_SCRIPT}" 'source "\$\{ROOT_DIR\}/install/setup.bash"'
require_contains "${HOST_CONFORMANCE_SCRIPT}" 'set \+u'
require_contains "${HOST_CONFORMANCE_SCRIPT}" 'set -u'
require_contains "${HOST_CONFORMANCE_SCRIPT}" 'ctest --test-dir "\$\{ROOT_DIR\}/build/rmw_mdds_cpp"'
require_contains "${HOST_CONFORMANCE_SCRIPT}" 'ctest --test-dir "\$\{ROOT_DIR\}/build/test_rmw_implementation"'
require_contains "${HOST_CONFORMANCE_SCRIPT}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${HOST_CONFORMANCE_SCRIPT}" 'RESULT\|rmw_mdds_host_conformance\|PASS\|package'
require_contains "${HOST_CONFORMANCE_SCRIPT}" 'RESULT\|rmw_mdds_host_conformance\|PASS\|upstream'
require_contains "${ROOT_DIR}/ohos/test_rmw_mdds_delivery_contracts.sh" \
  'SecurityEnforceInitFailsClosedWithoutPolicyEnforcement'
require_contains "${ROOT_DIR}/ohos/test_rmw_mdds_delivery_contracts.sh" \
  'RESULT\|rmw_mdds_security_enforce_fail_closed\|PASS'
require_file "${SECURITY_CONTRACT_SCRIPT}"
require_file "${SROS2_POLICY_CONTRACT_SCRIPT}"
require_contains "${SROS2_POLICY_CONTRACT_SCRIPT}" 'RESULT\|rmw_mdds_sros2_policy_contracts\|PASS'
require_file "${BOARD_SROS2_POLICY_PROBE}"
require_contains "${BOARD_SROS2_POLICY_PROBE}" 'RESULT\|board_sros2_authorized_pubsub\|PASS'
require_contains "${BOARD_SROS2_POLICY_PROBE}" 'RESULT\|board_sros2_unauthorized_publish\|PASS'
require_file "${BOARD_SROS2_PROTECTED_PROBE}"
require_contains "${BOARD_SROS2_PROTECTED_PROBE}" 'RESULT\|board_sros2_signed_policy\|PASS'
require_contains "${BOARD_SROS2_PROTECTED_PROBE}" 'RESULT\|board_sros2_protected_authorized_pubsub\|PASS'
require_contains "${BOARD_SROS2_PROTECTED_PROBE}" 'RESULT\|board_sros2_protected_unauthorized_publish\|PASS'
require_source_file "${PROTECTED_TRANSPORT_PROBE_SOURCE}"
require_contains "${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/CMakeLists.txt" \
  'rmw_mdds_bridge_protected_transport_probe'
require_contains "${BOARD_SROS2_PROTECTED_PROBE}" 'RMW_MDDS_PROTECTED_TRANSPORT_PROBE'
require_contains "${BOARD_SROS2_PROTECTED_PROBE}" 'PROTECTED_TRANSPORT_ACTIVATION_STATUS'
require_contains "${BOARD_SROS2_PROTECTED_PROBE}" \
  'RESULT\|board_sros2_protected_transport\|PASS\|device=\$\{device_id\}\|activation_status=0\|authenticated=1\|encrypted=1'
require_file "${ZERO_COPY_CONTRACT_SCRIPT}"
require_contains "${ZERO_COPY_CONTRACT_SCRIPT}" 'RESULT\|rmw_mdds_zero_copy_contracts\|PASS'
require_file "${ARTIFACT_CONTRACT_SCRIPT}"
require_file "${ACTION_BAG_CONTRACT_SCRIPT}"
require_contains "${ACTION_BAG_CONTRACT_SCRIPT}" \
  'RESULT\|rmw_mdds_action_bag_contracts\|PASS'
require_file "${TYPE_DESCRIPTION_PROBE}"
require_contains "${TYPE_DESCRIPTION_PROBE}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${TYPE_DESCRIPTION_PROBE}" '/talker/get_type_description'
require_contains "${TYPE_DESCRIPTION_PROBE}" 'type_description_interfaces/srv/GetTypeDescription'
require_contains "${TYPE_DESCRIPTION_PROBE}" 'RIHS01_'
require_contains "${TYPE_DESCRIPTION_PROBE}" 'RESULT\|rmw_mdds_type_description\|PASS'
require_file "${HOST_CLI_PUBSUB_PROBE}"
require_contains "${HOST_CLI_PUBSUB_PROBE}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${HOST_CLI_PUBSUB_PROBE}" 'ros2 topic echo'
require_contains "${HOST_CLI_PUBSUB_PROBE}" 'ros2 topic pub'
require_contains "${HOST_CLI_PUBSUB_PROBE}" 'RESULT\|rmw_mdds_host_cli_pubsub\|PASS'
require_file "${HOST_CLI_SERVICE_PROBE}"
require_contains "${HOST_CLI_SERVICE_PROBE}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${HOST_CLI_SERVICE_PROBE}" 'add_two_ints_server'
require_contains "${HOST_CLI_SERVICE_PROBE}" 'ros2 service call'
require_contains "${HOST_CLI_SERVICE_PROBE}" 'example_interfaces/srv/AddTwoInts'
require_contains "${HOST_CLI_SERVICE_PROBE}" 'RESULT\|rmw_mdds_host_cli_service\|PASS'
require_file "${HOST_CLI_ACTION_PROBE}"
require_contains "${HOST_CLI_ACTION_PROBE}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${HOST_CLI_ACTION_PROBE}" 'fibonacci_action_server'
require_contains "${HOST_CLI_ACTION_PROBE}" 'ros2 action send_goal'
require_contains "${HOST_CLI_ACTION_PROBE}" 'action_tutorials_interfaces/action/Fibonacci'
require_contains "${HOST_CLI_ACTION_PROBE}" 'ros2 daemon stop'
require_contains "${HOST_CLI_ACTION_PROBE}" 'timeout .*ros2 action list'
require_contains "${HOST_CLI_ACTION_PROBE}" 'RESULT\|rmw_mdds_host_cli_action\|PASS'
require_file "${HOST_CLI_PARAMS_PROBE}"
require_contains "${HOST_CLI_PARAMS_PROBE}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${HOST_CLI_PARAMS_PROBE}" 'parameter_blackboard'
require_contains "${HOST_CLI_PARAMS_PROBE}" 'ros2 param set'
require_contains "${HOST_CLI_PARAMS_PROBE}" 'ros2 param get'
require_contains "${HOST_CLI_PARAMS_PROBE}" 'RESULT\|rmw_mdds_host_cli_params\|PASS'
require_file "${HOST_CLI_LIFECYCLE_PROBE}"
require_contains "${HOST_CLI_LIFECYCLE_PROBE}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${HOST_CLI_LIFECYCLE_PROBE}" 'lifecycle_talker'
require_contains "${HOST_CLI_LIFECYCLE_PROBE}" 'ros2 lifecycle get'
require_contains "${HOST_CLI_LIFECYCLE_PROBE}" 'ros2 lifecycle set'
require_contains "${HOST_CLI_LIFECYCLE_PROBE}" 'run_lifecycle_cli'
require_contains "${HOST_CLI_LIFECYCLE_PROBE}" 'cli_rc >= 128'
require_contains "${HOST_CLI_LIFECYCLE_PROBE}" 'terminated abnormally'
require_contains "${HOST_CLI_LIFECYCLE_PROBE}" 'RESULT\|rmw_mdds_host_cli_lifecycle\|PASS'
require_file "${HOST_CLI_GRAPH_PROBE}"
require_contains "${HOST_CLI_GRAPH_PROBE}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${HOST_CLI_GRAPH_PROBE}" 'demo_nodes_cpp talker'
require_contains "${HOST_CLI_GRAPH_PROBE}" 'demo_nodes_cpp listener'
require_contains "${HOST_CLI_GRAPH_PROBE}" 'ros2 node list'
require_contains "${HOST_CLI_GRAPH_PROBE}" 'ros2 topic list'
require_contains "${HOST_CLI_GRAPH_PROBE}" 'ros2 topic info --verbose'
require_contains "${HOST_CLI_GRAPH_PROBE}" '--no-daemon'
require_contains "${HOST_CLI_GRAPH_PROBE}" 'RESULT\|rmw_mdds_host_cli_graph\|PASS'
require_file "${HOST_CLI_QOS_PROBE}"
require_contains "${HOST_CLI_QOS_PROBE}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${HOST_CLI_QOS_PROBE}" 'ros2 topic echo'
require_contains "${HOST_CLI_QOS_PROBE}" 'ros2 topic pub'
require_contains "${HOST_CLI_QOS_PROBE}" '--qos-reliability best_effort'
require_contains "${HOST_CLI_QOS_PROBE}" 'RESULT\|rmw_mdds_host_cli_qos\|PASS'
require_file "${HOST_CLI_TRANSIENT_LOCAL_PROBE}"
require_contains "${HOST_CLI_TRANSIENT_LOCAL_PROBE}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${HOST_CLI_TRANSIENT_LOCAL_PROBE}" 'ros2 topic pub'
require_contains "${HOST_CLI_TRANSIENT_LOCAL_PROBE}" 'ros2 topic echo'
require_contains "${HOST_CLI_TRANSIENT_LOCAL_PROBE}" '--qos-durability transient_local'
require_contains "${HOST_CLI_TRANSIENT_LOCAL_PROBE}" '--keep-alive'
require_contains "${HOST_CLI_TRANSIENT_LOCAL_PROBE}" 'RESULT\|rmw_mdds_host_cli_transient_local\|PASS'
require_file "${HOST_CLI_MESSAGE_INFO_PROBE}"
require_contains "${HOST_CLI_MESSAGE_INFO_PROBE}" 'RMW_IMPLEMENTATION=rmw_mdds_cpp'
require_contains "${HOST_CLI_MESSAGE_INFO_PROBE}" 'ros2 topic echo'
require_contains "${HOST_CLI_MESSAGE_INFO_PROBE}" '--include-message-info'
require_contains "${HOST_CLI_MESSAGE_INFO_PROBE}" 'ros2 topic pub'
require_contains "${HOST_CLI_MESSAGE_INFO_PROBE}" 'publication_sequence_number'
require_contains "${HOST_CLI_MESSAGE_INFO_PROBE}" 'reception_sequence_number'
require_contains "${HOST_CLI_MESSAGE_INFO_PROBE}" 'RESULT\|rmw_mdds_host_cli_message_info\|PASS'
require_no_hidden_package_sources
require_no_auto_ld_preload_hook
require_no_loaned_publish_copy_fallback
require_no_unfiltered_bridge_loaned_take_payload_copy
"${SECURITY_CONTRACT_SCRIPT}"
"${SROS2_POLICY_CONTRACT_SCRIPT}"
"${ZERO_COPY_CONTRACT_SCRIPT}"
"${ARTIFACT_CONTRACT_SCRIPT}"
"${ACTION_BAG_CONTRACT_SCRIPT}"
run_security_enforce_fail_closed_gate
"${HOST_CONFORMANCE_SCRIPT}"
"${TYPE_DESCRIPTION_PROBE}"
"${HOST_CLI_PUBSUB_PROBE}"
"${HOST_CLI_SERVICE_PROBE}"
"${HOST_CLI_ACTION_PROBE}"
"${HOST_CLI_PARAMS_PROBE}"
"${HOST_CLI_LIFECYCLE_PROBE}"
"${HOST_CLI_GRAPH_PROBE}"
"${HOST_CLI_QOS_PROBE}"
"${HOST_CLI_TRANSIENT_LOCAL_PROBE}"
"${HOST_CLI_MESSAGE_INFO_PROBE}"

echo "rmw_mdds_delivery_contracts_ok"
