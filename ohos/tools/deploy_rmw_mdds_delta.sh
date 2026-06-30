#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: deploy_rmw_mdds_delta.sh <device-id> [device-id ...]

Deploys the current rmw_mdds_cpp runtime delta into the ROS 2 overlay on one or
more RK3588A/OHOS boards.

Environment:
  HDC_BIN                         HDC executable, default: hdc
  RMW_MDDS_DEPLOY_PREFIX          Local source prefix, default: newest rmw_mdds-capable OHOS prefix
  ROS2_OHOS_REMOTE_PREFIX         Remote ROS 2 overlay, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_REMOTE_TARBALL         Remote temporary archive path, default: /data/local/tmp/rmw_mdds_cpp_delta.tgz
  RMW_MDDS_HDC_TIMEOUT_SECONDS    HDC shell timeout, default: 120
  MDDS_BRIDGE_SHARED_SO           Local libmdds_bridge_shared.z.so (DSoftBus data-plane
                                  bridge built by the DSoftBus/enhance/mdds tree). Default:
                                  newest libmdds_bridge_shared.z.so under the dsoftbus build
                                  output. Deployed to <remote-prefix>/lib so the bridge
                                  backend's bare-name dlopen() resolves it. Required for
                                  rmw_mdds-over-DSoftBus (broker+bridge default-on); if absent
                                  the rmw_mdds delta still deploys but the DSoftBus data plane
                                  stays disabled (local-loopback only).
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
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-120}"
HDC_SEND_VERIFY="${ROOT_DIR}/ohos/tools/hdc_send_verify.sh"

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
  include/rmw_mdds_cpp \
  share/rmw_mdds_cpp \
  share/ament_index/resource_index/package_run_dependencies/rmw_mdds_cpp \
  share/ament_index/resource_index/packages/rmw_mdds_cpp \
  share/ament_index/resource_index/parent_prefix_path/rmw_mdds_cpp \
  share/ament_index/resource_index/rmw_typesupport/rmw_mdds_cpp \
  share/ament_index/resource_index/rmw_typesupport_c/rmw_mdds_cpp \
  share/ament_index/resource_index/rmw_typesupport_cpp/rmw_mdds_cpp

LOCAL_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/librmw_mdds_cpp.so" | cut -d ' ' -f 1)"
LOCAL_BROKER_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker" | cut -d ' ' -f 1)"

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

for device_id in "$@"; do
  OHOS_HDC_BIN="${HDC_BIN}" "${HDC_SEND_VERIFY}" "${device_id}" "${TMP_TARBALL}" "${REMOTE_TARBALL}" >/dev/null
  extract_output="$(
    capture_hdc_shell "${device_id}" \
      "mkdir -p '${REMOTE_PREFIX}' && tar xzf '${REMOTE_TARBALL}' -C '${REMOTE_PREFIX}' && test -e '${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so' && test -e '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker' && chmod +x '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker' && if command -v sha256sum >/dev/null 2>&1; then sha256sum '${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so' '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker'; else ls -l '${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so' '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker'; fi"
  )"
  printf '%s\n' "${extract_output}"
  if command -v sha256sum >/dev/null 2>&1 &&
      grep -q "${LOCAL_SHA}" <<< "${extract_output}" &&
      grep -q "${LOCAL_BROKER_SHA}" <<< "${extract_output}"; then
    echo "RESULT|rmw_mdds_deploy|PASS|device=${device_id}|sha=${LOCAL_SHA}|broker_sha=${LOCAL_BROKER_SHA}"
  elif grep -q "librmw_mdds_cpp.so" <<< "${extract_output}" &&
      grep -q "rmw_mdds_broker" <<< "${extract_output}"; then
    echo "RESULT|rmw_mdds_deploy|PASS|device=${device_id}|remote_file_present"
  else
    echo "RESULT|rmw_mdds_deploy|FAIL|device=${device_id}" >&2
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
  else
    echo "RESULT|mdds_bridge_deploy|SKIP|device=${device_id}|reason=bridge_so_not_found"
  fi
done

echo "rmw_mdds_deploy_ok"
