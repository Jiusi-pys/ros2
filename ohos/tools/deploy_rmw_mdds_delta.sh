#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: deploy_rmw_mdds_delta.sh <device-id> [device-id ...]

Deploys the current rmw_mdds_cpp runtime delta and runtime-selectable rosbag2
libraries into the ROS 2 overlay on one or more RK3588A/OHOS boards.

Environment:
  HDC_BIN                         HDC executable, default: hdc
  RMW_MDDS_DEPLOY_PREFIX          Local source prefix, default: newest rmw_mdds-capable OHOS prefix
  ROS2_OHOS_REMOTE_PREFIX         Remote ROS 2 overlay, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_REMOTE_TARBALL         Remote temporary archive path, default: /data/local/tmp/rmw_mdds_cpp_delta.tgz
  RMW_MDDS_REMOTE_BRIDGE_ALIAS    Runtime bridge path used by legacy/manual env scripts,
                                  default: /data/local/tmp/libmdds_bridge_shared.z.so
  RMW_MDDS_HDC_TIMEOUT_SECONDS    HDC shell timeout, default: 120
  MDDS_BRIDGE_SHARED_SO           Local libmdds_bridge_shared.z.so (DSoftBus data-plane
                                  bridge built by the DSoftBus/enhance/mdds tree). Default:
                                  newest libmdds_bridge_shared.z.so under the dsoftbus build
                                  output. Deployed to <remote-prefix>/lib so the bridge
                                  backend's bare-name dlopen() resolves it. Required for
                                  rmw_mdds-over-DSoftBus (broker+bridge default-on); if absent
                                  the rmw_mdds delta still deploys but the DSoftBus data plane
                                  stays disabled (local-loopback only).
  MDDS_SOFTBUS_CLIENT_SO          Local libsoftbus_client.z.so matching the DSoftBus bridge.
                                  Default: newest libsoftbus_client.z.so under the dsoftbus
                                  build output. Deployed beside the bridge so protected
                                  transport can resolve current DSoftBus client exports.
EOF
}

if [[ $# -lt 1 ]]; then
  usage
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HDC_BIN="${HDC_BIN:-hdc}"
DEFAULT_LOCAL_PREFIX="${ROOT_DIR}/install/ohos-colcon-rk3588a"
if [[ ! -e "${DEFAULT_LOCAL_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker" &&
    -e "${ROOT_DIR}/install/ohos-ros2/lib/rmw_mdds_cpp/rmw_mdds_broker" ]]; then
  DEFAULT_LOCAL_PREFIX="${ROOT_DIR}/install/ohos-ros2"
fi
LOCAL_PREFIX="${RMW_MDDS_DEPLOY_PREFIX:-${DEFAULT_LOCAL_PREFIX}}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
REMOTE_TARBALL="${RMW_MDDS_REMOTE_TARBALL:-/data/local/tmp/rmw_mdds_cpp_delta.tgz}"
REMOTE_BRIDGE_ALIAS="${RMW_MDDS_REMOTE_BRIDGE_ALIAS:-/data/local/tmp/libmdds_bridge_shared.z.so}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-120}"
HDC_SEND_VERIFY="${ROOT_DIR}/ohos/tools/hdc_send_verify.sh"
ROSBAG2_PY_REL="lib/python3.12/site-packages/rosbag2_py"
ROS2BAG_REL="lib/python3.12/site-packages/ros2bag"
ROSBAG2_SQLITE_CLI_REL="lib/python3.12/site-packages/ros2bag_sqlite3_cli"
ACTION_BAG_PROBE_REL="lib/rmw_mdds_action_bag_probe/rmw_mdds_action_bag_probe"

capture_hdc_shell() {
  local device_id="$1"
  local command="$2"
  local output_file
  output_file="$(mktemp)"
  set +e
  timeout "${HDC_TIMEOUT_SECONDS}s" "${HDC_BIN}" -t "${device_id}" shell "${command}" >"${output_file}" 2>&1
  local status=$?
  set -e
  cat "${output_file}"
  rm -f "${output_file}"
  # Some local HDC builds complete the device command and then exit 139.
  [[ ${status} -eq 0 || ${status} -eq 139 ]]
}

require_local_file() {
  local path="$1"
  [[ -f "${path}" ]] || { echo "Missing local file: ${path}" >&2; exit 1; }
}

require_local_dir() {
  local path="$1"
  [[ -d "${path}" ]] || { echo "Missing local directory: ${path}" >&2; exit 1; }
}

require_local_file "${LOCAL_PREFIX}/lib/librmw_mdds_cpp.so"
require_local_file "${LOCAL_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker"
require_local_file "${LOCAL_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_bridge_protected_transport_probe"
require_local_file "${LOCAL_PREFIX}/lib/librosbag2_cpp.so"
require_local_file "${LOCAL_PREFIX}/lib/librosbag2_transport.so"
require_local_file "${LOCAL_PREFIX}/lib/librcl_action.so"
require_local_file "${LOCAL_PREFIX}/lib/librclcpp_action.so"
require_local_file "${LOCAL_PREFIX}/lib/librosbag2_storage.so"
require_local_file "${LOCAL_PREFIX}/lib/librosbag2_storage_sqlite3.so"
require_local_file "${LOCAL_PREFIX}/${ACTION_BAG_PROBE_REL}"
require_local_dir "${LOCAL_PREFIX}/${ROSBAG2_PY_REL}"
require_local_file "${LOCAL_PREFIX}/${ROSBAG2_PY_REL}/_transport.so"
require_local_dir "${LOCAL_PREFIX}/${ROS2BAG_REL}"
require_local_dir "${LOCAL_PREFIX}/${ROSBAG2_SQLITE_CLI_REL}"
require_local_file "${LOCAL_PREFIX}/${ROS2BAG_REL}/verb/record.py"
require_local_file "${LOCAL_PREFIX}/share/ament_index/resource_index/rosbag2_storage__pluginlib__plugin/rosbag2_storage_sqlite3"
require_local_dir "${LOCAL_PREFIX}/share/rmw_mdds_cpp"
require_local_file "${LOCAL_PREFIX}/share/ament_index/resource_index/rmw_typesupport/rmw_mdds_cpp"
require_local_file "${LOCAL_PREFIX}/share/ament_index/resource_index/rmw_typesupport_c/rmw_mdds_cpp"
require_local_file "${LOCAL_PREFIX}/share/ament_index/resource_index/rmw_typesupport_cpp/rmw_mdds_cpp"
require_local_file "${HDC_SEND_VERIFY}"

TMP_TARBALL="$(mktemp /tmp/rmw_mdds_cpp_delta.XXXXXX.tgz)"
trap 'rm -f "${TMP_TARBALL}"' EXIT

tar -C "${LOCAL_PREFIX}" -czf "${TMP_TARBALL}" \
  lib/librmw_mdds_cpp.so \
  lib/rmw_mdds_cpp/rmw_mdds_broker \
  lib/rmw_mdds_cpp/rmw_mdds_bridge_protected_transport_probe \
  lib/librosbag2_cpp.so \
  lib/librosbag2_transport.so \
  lib/librcl_action.so \
  lib/librclcpp_action.so \
  lib/librosbag2_storage.so \
  lib/librosbag2_storage_sqlite3.so \
  "${ACTION_BAG_PROBE_REL}" \
  "${ROSBAG2_PY_REL}" \
  "${ROS2BAG_REL}" \
  "${ROSBAG2_SQLITE_CLI_REL}" \
  include/rmw_mdds_cpp \
  share/rmw_mdds_action_bag_probe \
  share/rosbag2_storage_sqlite3 \
  share/rmw_mdds_cpp \
  share/ament_index/resource_index/package_run_dependencies/rmw_mdds_cpp \
  share/ament_index/resource_index/packages/rmw_mdds_cpp \
  share/ament_index/resource_index/parent_prefix_path/rmw_mdds_cpp \
  share/ament_index/resource_index/rmw_typesupport/rmw_mdds_cpp \
  share/ament_index/resource_index/rmw_typesupport_c/rmw_mdds_cpp \
  share/ament_index/resource_index/rmw_typesupport_cpp/rmw_mdds_cpp \
  share/ament_index/resource_index/packages/rmw_mdds_action_bag_probe \
  share/ament_index/resource_index/package_run_dependencies/rmw_mdds_action_bag_probe \
  share/ament_index/resource_index/parent_prefix_path/rmw_mdds_action_bag_probe \
  share/ament_index/resource_index/packages/rosbag2_storage_sqlite3 \
  share/ament_index/resource_index/package_run_dependencies/rosbag2_storage_sqlite3 \
  share/ament_index/resource_index/parent_prefix_path/rosbag2_storage_sqlite3 \
  share/ament_index/resource_index/rosbag2_storage__pluginlib__plugin/rosbag2_storage_sqlite3

LOCAL_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/librmw_mdds_cpp.so" | cut -d ' ' -f 1)"
LOCAL_BROKER_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker" | cut -d ' ' -f 1)"
LOCAL_PROTECTED_PROBE_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_bridge_protected_transport_probe" | cut -d ' ' -f 1)"
LOCAL_ROSBAG2_CPP_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/librosbag2_cpp.so" | cut -d ' ' -f 1)"
LOCAL_ROSBAG2_TRANSPORT_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/librosbag2_transport.so" | cut -d ' ' -f 1)"
LOCAL_ROSBAG2_PY_TRANSPORT_SHA="$(sha256sum "${LOCAL_PREFIX}/${ROSBAG2_PY_REL}/_transport.so" | cut -d ' ' -f 1)"
LOCAL_RCL_ACTION_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/librcl_action.so" | cut -d ' ' -f 1)"
LOCAL_RCLCPP_ACTION_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/librclcpp_action.so" | cut -d ' ' -f 1)"
LOCAL_ROSBAG2_STORAGE_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/librosbag2_storage.so" | cut -d ' ' -f 1)"
LOCAL_ROSBAG2_SQLITE_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/librosbag2_storage_sqlite3.so" | cut -d ' ' -f 1)"
LOCAL_ACTION_BAG_PROBE_SHA="$(sha256sum "${LOCAL_PREFIX}/${ACTION_BAG_PROBE_REL}" | cut -d ' ' -f 1)"
LOCAL_ROS2BAG_RECORD_SHA="$(sha256sum "${LOCAL_PREFIX}/${ROS2BAG_REL}/verb/record.py" | cut -d ' ' -f 1)"

# Resolve the DSoftBus data-plane bridge library. It is built by the DSoftBus tree
# (enhance/mdds), not this ROS 2 tree, so it is configurable and may be absent.
BRIDGE_SO_NAME="libmdds_bridge_shared.z.so"
BRIDGE_SO="${MDDS_BRIDGE_SHARED_SO:-}"
if [[ -z "${BRIDGE_SO}" ]]; then
  for cand in \
    "${HOME}/M-DDS/OpenHarmony_lyl/out/arm64/targets/communication/dsoftbus/${BRIDGE_SO_NAME}" \
    "/home/kaihong/M-DDS/OpenHarmony_lyl/out/arm64/targets/communication/dsoftbus/${BRIDGE_SO_NAME}"; do
    if [[ -f "${cand}" ]]; then BRIDGE_SO="${cand}"; break; fi
  done
fi
if [[ -n "${BRIDGE_SO}" && -f "${BRIDGE_SO}" ]]; then
  BRIDGE_SHA="$(sha256sum "${BRIDGE_SO}" | cut -d ' ' -f 1)"
else
  BRIDGE_SHA=""
  echo "WARN: ${BRIDGE_SO_NAME} not found (set MDDS_BRIDGE_SHARED_SO); the rmw_mdds delta will" >&2
  echo "WARN: deploy but the DSoftBus data plane stays OFF (broker local-loopback only)." >&2
fi

SOFTBUS_CLIENT_SO_NAME="libsoftbus_client.z.so"
SOFTBUS_CLIENT_SO="${MDDS_SOFTBUS_CLIENT_SO:-}"
if [[ -z "${SOFTBUS_CLIENT_SO}" ]]; then
  for cand in \
    "${HOME}/M-DDS/OpenHarmony_lyl/out/arm64/targets/communication/dsoftbus/${SOFTBUS_CLIENT_SO_NAME}"; do
    if [[ -f "${cand}" ]]; then SOFTBUS_CLIENT_SO="${cand}"; break; fi
  done
fi
if [[ -n "${SOFTBUS_CLIENT_SO}" && -f "${SOFTBUS_CLIENT_SO}" ]]; then
  SOFTBUS_CLIENT_SHA="$(sha256sum "${SOFTBUS_CLIENT_SO}" | cut -d ' ' -f 1)"
else
  SOFTBUS_CLIENT_SHA=""
  echo "WARN: ${SOFTBUS_CLIENT_SO_NAME} not found (set MDDS_SOFTBUS_CLIENT_SO); bridge runtime" >&2
  echo "WARN: will use the board image's DSoftBus client library." >&2
fi

for device_id in "$@"; do
  OHOS_HDC_BIN="${HDC_BIN}" "${HDC_SEND_VERIFY}" "${device_id}" "${TMP_TARBALL}" "${REMOTE_TARBALL}" >/dev/null
  extract_output="$(
    capture_hdc_shell "${device_id}" \
      "mkdir -p '${REMOTE_PREFIX}' && tar xzf '${REMOTE_TARBALL}' -C '${REMOTE_PREFIX}' && test -e '${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so' && test -e '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker' && test -e '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_bridge_protected_transport_probe' && test -e '${REMOTE_PREFIX}/lib/librosbag2_cpp.so' && test -e '${REMOTE_PREFIX}/lib/librosbag2_transport.so' && test -e '${REMOTE_PREFIX}/${ROSBAG2_PY_REL}/_transport.so' && chmod +x '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker' '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_bridge_protected_transport_probe' && if command -v sha256sum >/dev/null 2>&1; then sha256sum '${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so' '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker' '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_bridge_protected_transport_probe' '${REMOTE_PREFIX}/lib/librosbag2_cpp.so' '${REMOTE_PREFIX}/lib/librosbag2_transport.so' '${REMOTE_PREFIX}/${ROSBAG2_PY_REL}/_transport.so'; else ls -l '${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so' '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker' '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_bridge_protected_transport_probe' '${REMOTE_PREFIX}/lib/librosbag2_cpp.so' '${REMOTE_PREFIX}/lib/librosbag2_transport.so' '${REMOTE_PREFIX}/${ROSBAG2_PY_REL}/_transport.so'; fi"
  )"
  printf '%s\n' "${extract_output}"
  if command -v sha256sum >/dev/null 2>&1 &&
      grep -q "${LOCAL_SHA}" <<< "${extract_output}" &&
      grep -q "${LOCAL_BROKER_SHA}" <<< "${extract_output}" &&
      grep -q "${LOCAL_PROTECTED_PROBE_SHA}" <<< "${extract_output}" &&
      grep -q "${LOCAL_ROSBAG2_CPP_SHA}" <<< "${extract_output}" &&
      grep -q "${LOCAL_ROSBAG2_TRANSPORT_SHA}" <<< "${extract_output}" &&
      grep -q "${LOCAL_ROSBAG2_PY_TRANSPORT_SHA}" <<< "${extract_output}"; then
    echo "RESULT|rmw_mdds_deploy|PASS|device=${device_id}|sha=${LOCAL_SHA}|broker_sha=${LOCAL_BROKER_SHA}|protected_probe_sha=${LOCAL_PROTECTED_PROBE_SHA}|rosbag2_cpp_sha=${LOCAL_ROSBAG2_CPP_SHA}|rosbag2_transport_sha=${LOCAL_ROSBAG2_TRANSPORT_SHA}|rosbag2_py_transport_sha=${LOCAL_ROSBAG2_PY_TRANSPORT_SHA}"
  elif grep -q "librmw_mdds_cpp.so" <<< "${extract_output}" &&
      grep -q "rmw_mdds_broker" <<< "${extract_output}" &&
      grep -q "rmw_mdds_bridge_protected_transport_probe" <<< "${extract_output}" &&
      grep -q "librosbag2_cpp.so" <<< "${extract_output}" &&
      grep -q "librosbag2_transport.so" <<< "${extract_output}" &&
      grep -q "${ROSBAG2_PY_REL}/_transport.so" <<< "${extract_output}"; then
    echo "RESULT|rmw_mdds_deploy|PASS|device=${device_id}|remote_file_present"
  else
    echo "RESULT|rmw_mdds_deploy|FAIL|device=${device_id}" >&2
    exit 1
  fi

  action_bag_output="$(
    capture_hdc_shell "${device_id}" \
      "test -e '${REMOTE_PREFIX}/lib/librcl_action.so' && test -e '${REMOTE_PREFIX}/lib/librclcpp_action.so' && test -e '${REMOTE_PREFIX}/lib/librosbag2_storage.so' && test -e '${REMOTE_PREFIX}/lib/librosbag2_storage_sqlite3.so' && test -e '${REMOTE_PREFIX}/${ACTION_BAG_PROBE_REL}' && test -e '${REMOTE_PREFIX}/${ROS2BAG_REL}/verb/record.py' && chmod +x '${REMOTE_PREFIX}/${ACTION_BAG_PROBE_REL}' && if command -v sha256sum >/dev/null 2>&1; then sha256sum '${REMOTE_PREFIX}/lib/librcl_action.so' '${REMOTE_PREFIX}/lib/librclcpp_action.so' '${REMOTE_PREFIX}/lib/librosbag2_storage.so' '${REMOTE_PREFIX}/lib/librosbag2_storage_sqlite3.so' '${REMOTE_PREFIX}/${ACTION_BAG_PROBE_REL}' '${REMOTE_PREFIX}/${ROS2BAG_REL}/verb/record.py'; else ls -l '${REMOTE_PREFIX}/lib/librcl_action.so' '${REMOTE_PREFIX}/lib/librclcpp_action.so' '${REMOTE_PREFIX}/lib/librosbag2_storage.so' '${REMOTE_PREFIX}/lib/librosbag2_storage_sqlite3.so' '${REMOTE_PREFIX}/${ACTION_BAG_PROBE_REL}' '${REMOTE_PREFIX}/${ROS2BAG_REL}/verb/record.py'; fi"
  )"
  printf '%s\n' "${action_bag_output}"
  if command -v sha256sum >/dev/null 2>&1 &&
      grep -q "${LOCAL_RCL_ACTION_SHA}" <<< "${action_bag_output}" &&
      grep -q "${LOCAL_RCLCPP_ACTION_SHA}" <<< "${action_bag_output}" &&
      grep -q "${LOCAL_ROSBAG2_STORAGE_SHA}" <<< "${action_bag_output}" &&
      grep -q "${LOCAL_ROSBAG2_SQLITE_SHA}" <<< "${action_bag_output}" &&
      grep -q "${LOCAL_ACTION_BAG_PROBE_SHA}" <<< "${action_bag_output}" &&
      grep -q "${LOCAL_ROS2BAG_RECORD_SHA}" <<< "${action_bag_output}"; then
    echo "RESULT|rmw_mdds_action_bag_deploy|PASS|device=${device_id}|rcl_action_sha=${LOCAL_RCL_ACTION_SHA}|rclcpp_action_sha=${LOCAL_RCLCPP_ACTION_SHA}|storage_sha=${LOCAL_ROSBAG2_STORAGE_SHA}|sqlite_sha=${LOCAL_ROSBAG2_SQLITE_SHA}|probe_sha=${LOCAL_ACTION_BAG_PROBE_SHA}|record_cli_sha=${LOCAL_ROS2BAG_RECORD_SHA}"
  elif grep -q "librcl_action.so" <<< "${action_bag_output}" &&
      grep -q "librclcpp_action.so" <<< "${action_bag_output}" &&
      grep -q "librosbag2_storage.so" <<< "${action_bag_output}" &&
      grep -q "librosbag2_storage_sqlite3.so" <<< "${action_bag_output}" &&
      grep -q "rmw_mdds_action_bag_probe" <<< "${action_bag_output}" &&
      grep -q "record.py" <<< "${action_bag_output}"; then
    echo "RESULT|rmw_mdds_action_bag_deploy|PASS|device=${device_id}|remote_files_present"
  else
    echo "RESULT|rmw_mdds_action_bag_deploy|FAIL|device=${device_id}" >&2
    exit 1
  fi

  # DSoftBus data-plane bridge: deploy to <prefix>/lib so the bridge backend's
  # bare-name dlopen("libmdds_bridge_shared.z.so") resolves it (that dir is on
  # LD_LIBRARY_PATH), enabling broker+bridge over DSoftBus without RMW_MDDS_BRIDGE_LIBRARY.
  if [[ -n "${BRIDGE_SHA}" ]]; then
    BRIDGE_REMOTE="${REMOTE_PREFIX}/lib/${BRIDGE_SO_NAME}"
    OHOS_HDC_BIN="${HDC_BIN}" "${HDC_SEND_VERIFY}" "${device_id}" "${BRIDGE_SO}" "${BRIDGE_REMOTE}" >/dev/null
    bridge_output="$(
      capture_hdc_shell "${device_id}" \
        "test -e '${BRIDGE_REMOTE}' && chmod 755 '${BRIDGE_REMOTE}' && if command -v sha256sum >/dev/null 2>&1; then sha256sum '${BRIDGE_REMOTE}'; else ls -l '${BRIDGE_REMOTE}'; fi"
    )"
    printf '%s\n' "${bridge_output}"
    if command -v sha256sum >/dev/null 2>&1 && grep -q "${BRIDGE_SHA}" <<< "${bridge_output}"; then
      echo "RESULT|mdds_bridge_deploy|PASS|device=${device_id}|sha=${BRIDGE_SHA}"
    elif grep -q "${BRIDGE_SO_NAME}" <<< "${bridge_output}"; then
      echo "RESULT|mdds_bridge_deploy|PASS|device=${device_id}|remote_file_present"
    else
      echo "RESULT|mdds_bridge_deploy|FAIL|device=${device_id}" >&2
      exit 1
    fi

    # Keep the explicit runtime path used by rmw_mdds_env.sh in lockstep with
    # the canonical overlay copy. A stale alias can otherwise pass deployment
    # verification while the broker loads a different bridge binary.
    if [[ "${REMOTE_BRIDGE_ALIAS}" != "${BRIDGE_REMOTE}" ]]; then
      OHOS_HDC_BIN="${HDC_BIN}" "${HDC_SEND_VERIFY}" "${device_id}" \
        "${BRIDGE_SO}" "${REMOTE_BRIDGE_ALIAS}" >/dev/null
    fi
    bridge_alias_output="$(
      capture_hdc_shell "${device_id}" \
        "test -e '${REMOTE_BRIDGE_ALIAS}' && chmod 755 '${REMOTE_BRIDGE_ALIAS}' && if command -v sha256sum >/dev/null 2>&1; then sha256sum '${REMOTE_BRIDGE_ALIAS}'; else ls -l '${REMOTE_BRIDGE_ALIAS}'; fi"
    )"
    printf '%s\n' "${bridge_alias_output}"
    if command -v sha256sum >/dev/null 2>&1 && grep -q "${BRIDGE_SHA}" <<< "${bridge_alias_output}"; then
      echo "RESULT|mdds_bridge_runtime_alias_deploy|PASS|device=${device_id}|path=${REMOTE_BRIDGE_ALIAS}|sha=${BRIDGE_SHA}"
    elif grep -q "${BRIDGE_SO_NAME}" <<< "${bridge_alias_output}"; then
      echo "RESULT|mdds_bridge_runtime_alias_deploy|PASS|device=${device_id}|path=${REMOTE_BRIDGE_ALIAS}|remote_file_present"
    else
      echo "RESULT|mdds_bridge_runtime_alias_deploy|FAIL|device=${device_id}|path=${REMOTE_BRIDGE_ALIAS}" >&2
      exit 1
    fi
  else
    echo "RESULT|mdds_bridge_deploy|SKIP|device=${device_id}|reason=bridge_so_not_found"
  fi

  if [[ -n "${SOFTBUS_CLIENT_SHA}" ]]; then
    SOFTBUS_CLIENT_REMOTE="${REMOTE_PREFIX}/lib/${SOFTBUS_CLIENT_SO_NAME}"
    OHOS_HDC_BIN="${HDC_BIN}" "${HDC_SEND_VERIFY}" "${device_id}" "${SOFTBUS_CLIENT_SO}" "${SOFTBUS_CLIENT_REMOTE}" >/dev/null
    softbus_client_output="$(
      capture_hdc_shell "${device_id}" \
        "test -e '${SOFTBUS_CLIENT_REMOTE}' && chmod 755 '${SOFTBUS_CLIENT_REMOTE}' && if command -v sha256sum >/dev/null 2>&1; then sha256sum '${SOFTBUS_CLIENT_REMOTE}'; else ls -l '${SOFTBUS_CLIENT_REMOTE}'; fi"
    )"
    printf '%s\n' "${softbus_client_output}"
    if command -v sha256sum >/dev/null 2>&1 && grep -q "${SOFTBUS_CLIENT_SHA}" <<< "${softbus_client_output}"; then
      echo "RESULT|softbus_client_deploy|PASS|device=${device_id}|sha=${SOFTBUS_CLIENT_SHA}"
    elif grep -q "${SOFTBUS_CLIENT_SO_NAME}" <<< "${softbus_client_output}"; then
      echo "RESULT|softbus_client_deploy|PASS|device=${device_id}|remote_file_present"
    else
      echo "RESULT|softbus_client_deploy|FAIL|device=${device_id}" >&2
      exit 1
    fi
  else
    echo "RESULT|softbus_client_deploy|SKIP|device=${device_id}|reason=softbus_client_so_not_found"
  fi
done

echo "rmw_mdds_deploy_ok"
