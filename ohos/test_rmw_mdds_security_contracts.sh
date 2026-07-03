#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RMW_PACKAGE_DIR="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

require_absent() {
  local label="$1"
  local pattern="$2"
  shift 2
  local hits
  hits="$(grep -RInE -- "${pattern}" "$@" 2>/dev/null || true)"
  [[ -z "${hits}" ]] || fail "${label}: ${hits}"
}

SCRIPT_SYNTAX_TARGETS=(
  "${ROOT_DIR}/ohos/test_rmw_mdds_delivery_contracts.sh"
  "${ROOT_DIR}/ohos/test_rmw_mdds_script_contracts.sh"
  "${ROOT_DIR}/ohos/test_rmw_mdds_security_contracts.sh"
  "${ROOT_DIR}/ohos/test_rmw_mdds_sros2_policy_contracts.sh"
  "${ROOT_DIR}/ohos/tools"/*.sh
)

SCRIPT_CONTENT_TARGETS=(
  "${ROOT_DIR}/ohos/test_rmw_mdds_delivery_contracts.sh"
  "${ROOT_DIR}/ohos/test_rmw_mdds_script_contracts.sh"
  "${ROOT_DIR}/ohos/test_rmw_mdds_sros2_policy_contracts.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_pubsub.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_broker_pubsub.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_broker_service.sh"
  "${ROOT_DIR}/ohos/tools/rmw_mdds_broker_ctl.sh"
  "${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_fastdds.sh"
  "${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds.sh"
  "${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_service.sh"
  "${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_sros2_policy.sh"
  "${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_service_gw.sh"
  "${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_action_gw.sh"
  "${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_lifecycle_gw.sh"
  "${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_params_gw.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_doctor.sh"
  "${ROOT_DIR}/ohos/tools/deploy_rmw_mdds_delta.sh"
  "${ROOT_DIR}/ohos/tools/hdc_send_verify.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_conformance.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_type_description_probe.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_pubsub.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_service.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_action.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_params.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_lifecycle.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_graph.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_qos.sh"
  "${ROOT_DIR}/ohos/tools/run_rmw_mdds_host_cli_message_info.sh"
)

SOURCE_PATHS=(
  "${RMW_PACKAGE_DIR}/src"
  "${RMW_PACKAGE_DIR}/include"
)

for path in "${SCRIPT_SYNTAX_TARGETS[@]}"; do
  [[ -e "${path}" ]] || continue
  bash -n "${path}"
done

require_absent "hardcoded lab device id" '3e01ff[0-9a-f]+' "${SCRIPT_CONTENT_TARGETS[@]}"
require_absent "shell eval is forbidden" '(^|[[:space:];])eval([[:space:];]|$)' "${SCRIPT_CONTENT_TARGETS[@]}"
require_absent "curl or wget piped to shell is forbidden" '(curl|wget)[^|;]*\|[[:space:]]*(sh|bash)' "${SCRIPT_CONTENT_TARGETS[@]}"
require_absent "world-writable chmod is forbidden" 'chmod[[:space:]]+777' "${SCRIPT_CONTENT_TARGETS[@]}"
require_absent "remote system/vendor writes are forbidden" \
  'file[[:space:]]+send[^;&|]*(/system|/vendor)|send[^;&|]*(/system|/vendor)|mount[[:space:]]+-o[[:space:]]+rw|remount' \
  "${SCRIPT_CONTENT_TARGETS[@]}"
require_absent "unscoped hdc shell/file command is forbidden" \
  '(^|[[:space:]])(hdc|hdc_std)[[:space:]]+(shell|file|install|uninstall)' \
  "${SCRIPT_CONTENT_TARGETS[@]}"
require_absent "production source must not spawn shell commands" \
  '(^|[^[:alnum:]_])(std::system|system|popen|execl|execle|execlp|execv|execve|execvp)[[:space:]]*\(' \
  "${SOURCE_PATHS[@]}"

echo "rmw_mdds_security_contracts_ok"
