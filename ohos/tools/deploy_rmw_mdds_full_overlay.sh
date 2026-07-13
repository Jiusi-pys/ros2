#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/tools/deploy_rmw_mdds_full_overlay.sh"
# language: "shell"
# summary: "Atomically deploys an ABI-consistent ROS 2/rmw_mdds overlay and verifies key artifacts on RK3588A boards."
# symbols: ["capture_hdc_shell", "require_local_file", "send_verified", "resolve_bridge_artifacts"]
# generated_by: "codex"
# codex-file-meta: end

set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: deploy_rmw_mdds_full_overlay.sh <device-id> [device-id ...]

Atomically deploys the complete allocator-aware ROS 2/rmw_mdds overlay. This
mode is required when generated message layouts or rosidl runtime ABI change;
the smaller delta deployment is intentionally insufficient for that case.

Environment:
  HDC_BIN                         HDC executable, default: hdc
  RMW_MDDS_FULL_OVERLAY_PREFIX    Local overlay, default: install/ohos-colcon-rk3588a
  ROS2_OHOS_REMOTE_PREFIX         Remote overlay, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_FULL_OVERLAY_TARBALL   Remote archive, default: /data/local/tmp/rmw_mdds_full_overlay.tgz
  RMW_MDDS_REMOTE_BRIDGE_ALIAS    Explicit bridge alias, default: /data/local/tmp/libmdds_bridge_shared.z.so
  RMW_MDDS_HDC_TIMEOUT_SECONDS    HDC shell timeout, default: 300
  RMW_MDDS_HDC_SEND_TIMEOUT       HDC file-send timeout, default: 300s
  MDDS_BRIDGE_SHARED_SO           Local libmdds_bridge_shared.z.so
  MDDS_SOFTBUS_CLIENT_SO          Local libsoftbus_client.z.so
  RMW_MDDS_REQUIRE_BRIDGE         Require the bridge artifact, default: 1
EOF
}

if [[ $# -lt 1 ]]; then
  usage
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HDC_BIN="${HDC_BIN:-hdc}"
LOCAL_PREFIX="${RMW_MDDS_FULL_OVERLAY_PREFIX:-${ROOT_DIR}/install/ohos-colcon-rk3588a}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
REMOTE_TARBALL="${RMW_MDDS_FULL_OVERLAY_TARBALL:-/data/local/tmp/rmw_mdds_full_overlay.tgz}"
REMOTE_BRIDGE_ALIAS="${RMW_MDDS_REMOTE_BRIDGE_ALIAS:-/data/local/tmp/libmdds_bridge_shared.z.so}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-300}"
HDC_SEND_TIMEOUT="${RMW_MDDS_HDC_SEND_TIMEOUT:-300s}"
REQUIRE_BRIDGE="${RMW_MDDS_REQUIRE_BRIDGE:-1}"
HDC_SEND_VERIFY="${ROOT_DIR}/ohos/tools/hdc_send_verify.sh"

KEY_ARTIFACTS=(
  lib/librosidl_runtime_c.so
  lib/librmw_mdds_cpp.so
  lib/librmw_fastrtps_shared_cpp.so
  lib/librmw_fastrtps_cpp.so
  lib/librmw_fastrtps_dynamic_cpp.so
  lib/librmw_cyclonedds_cpp.so
  lib/libddsc.so.0.10.5
  lib/librclcpp.so
  lib/libstd_msgs__rosidl_typesupport_introspection_cpp.so
  lib/rmw_mdds_cpp/rmw_mdds_broker
  lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_dynamic_loan_probe
  lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_broker_dynamic_loan_probe
  lib/rmw_mdds_dynamic_loan_probe/broker_dynamic_loan_board_runner.sh
  lib/demo_nodes_cpp/add_two_ints_server
  lib/action_tutorials_cpp/fibonacci_action_server
  lib/librosbag2_transport.so
  lib/python3.12/site-packages/rosbag2_py/_transport.so
)

capture_hdc_shell() {
  local device_id="$1"
  local command="$2"
  local output_file
  local status
  output_file="$(mktemp)"
  set +e
  timeout "${HDC_TIMEOUT_SECONDS}s" "${HDC_BIN}" -t "${device_id}" shell "${command}" >"${output_file}" 2>&1
  status=$?
  set -e
  cat "${output_file}"
  rm -f "${output_file}"
  [[ ${status} -eq 0 || ${status} -eq 139 ]]
}

require_local_file() {
  local path="$1"
  [[ -f "${path}" ]] || {
    echo "Missing local file: ${path}" >&2
    exit 1
  }
}

send_verified() {
  local device_id="$1"
  local local_path="$2"
  local remote_path="$3"
  OHOS_HDC_BIN="${HDC_BIN}" \
    OHOS_HDC_TIMEOUT_SEND="${HDC_SEND_TIMEOUT}" \
    OHOS_HDC_VERIFY_TIMEOUT_SECONDS="${HDC_TIMEOUT_SECONDS}" \
    "${HDC_SEND_VERIFY}" "${device_id}" "${local_path}" "${remote_path}" >/dev/null
}

resolve_bridge_artifacts() {
  BRIDGE_SO="${MDDS_BRIDGE_SHARED_SO:-}"
  SOFTBUS_CLIENT_SO="${MDDS_SOFTBUS_CLIENT_SO:-}"

  if [[ -z "${BRIDGE_SO}" ]]; then
    for candidate in \
      "${HOME}/M-DDS/OpenHarmony_lyl/out/arm64/targets/communication/dsoftbus/libmdds_bridge_shared.z.so" \
      "/home/kaihong/M-DDS/OpenHarmony_lyl/out/arm64/targets/communication/dsoftbus/libmdds_bridge_shared.z.so"; do
      if [[ -f "${candidate}" ]]; then
        BRIDGE_SO="${candidate}"
        break
      fi
    done
  fi

  if [[ -z "${SOFTBUS_CLIENT_SO}" ]]; then
    for candidate in \
      "${HOME}/M-DDS/OpenHarmony_lyl/out/arm64/targets/communication/dsoftbus/libsoftbus_client.z.so" \
      "/home/kaihong/M-DDS/OpenHarmony_lyl/out/arm64/targets/communication/dsoftbus/libsoftbus_client.z.so"; do
      if [[ -f "${candidate}" ]]; then
        SOFTBUS_CLIENT_SO="${candidate}"
        break
      fi
    done
  fi

  if [[ "${REQUIRE_BRIDGE}" == "1" ]]; then
    require_local_file "${BRIDGE_SO}"
  fi
}

require_local_file "${HDC_SEND_VERIFY}"
for artifact in "${KEY_ARTIFACTS[@]}"; do
  require_local_file "${LOCAL_PREFIX}/${artifact}"
done
resolve_bridge_artifacts

TMP_TARBALL="$(mktemp /tmp/rmw_mdds_full_overlay.XXXXXX.tgz)"
TMP_MANIFEST="$(mktemp /tmp/rmw_mdds_full_overlay.XXXXXX.sha256)"
trap 'rm -f "${TMP_TARBALL}" "${TMP_MANIFEST}"' EXIT

(
  cd "${LOCAL_PREFIX}"
  sha256sum "${KEY_ARTIFACTS[@]}"
) >"${TMP_MANIFEST}"

tar -C "${LOCAL_PREFIX}" -czf "${TMP_TARBALL}" .
LOCAL_TARBALL_SHA="$(sha256sum "${TMP_TARBALL}" | cut -d ' ' -f 1)"
LOCAL_RMW_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/librmw_mdds_cpp.so" | cut -d ' ' -f 1)"
LOCAL_DYNAMIC_PROBE_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_dynamic_loan_probe" | cut -d ' ' -f 1)"
LOCAL_BROKER_DYNAMIC_PROBE_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_broker_dynamic_loan_probe" | cut -d ' ' -f 1)"
LOCAL_BROKER_DYNAMIC_RUNNER_SHA="$(sha256sum "${LOCAL_PREFIX}/lib/rmw_mdds_dynamic_loan_probe/broker_dynamic_loan_board_runner.sh" | cut -d ' ' -f 1)"

for device_id in "$@"; do
  remote_incoming="${REMOTE_PREFIX}.incoming"
  remote_backup="${REMOTE_PREFIX}.backup"
  remote_manifest="${REMOTE_PREFIX}/.rmw_mdds_full_overlay.sha256"

  send_verified "${device_id}" "${TMP_TARBALL}" "${REMOTE_TARBALL}"
  tarball_output="$(
    capture_hdc_shell "${device_id}" \
      "test -f '${REMOTE_TARBALL}' && test \"\$(sha256sum '${REMOTE_TARBALL}' | cut -d ' ' -f 1)\" = '${LOCAL_TARBALL_SHA}' && echo RMW_MDDS_FULL_OVERLAY_ARCHIVE_OK"
  )"
  printf '%s\n' "${tarball_output}"
  grep -q '^RMW_MDDS_FULL_OVERLAY_ARCHIVE_OK$' <<<"${tarball_output}" || {
    echo "RESULT|rmw_mdds_full_overlay_deploy|FAIL|device=${device_id}|stage=archive_verify" >&2
    exit 1
  }

  swap_output="$(
    capture_hdc_shell "${device_id}" \
      "rm -rf '${remote_incoming}' '${remote_backup}' && mkdir -p '${remote_incoming}' && tar xzf '${REMOTE_TARBALL}' -C '${remote_incoming}' && test -f '${remote_incoming}/lib/librmw_mdds_cpp.so' && test -f '${remote_incoming}/lib/librosidl_runtime_c.so' && test -f '${remote_incoming}/lib/libstd_msgs__rosidl_typesupport_introspection_cpp.so' && test -x '${remote_incoming}/lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_dynamic_loan_probe' && test -x '${remote_incoming}/lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_broker_dynamic_loan_probe' && test -x '${remote_incoming}/lib/rmw_mdds_dynamic_loan_probe/broker_dynamic_loan_board_runner.sh' && if test -e '${REMOTE_PREFIX}'; then mv '${REMOTE_PREFIX}' '${remote_backup}'; fi && if mv '${remote_incoming}' '${REMOTE_PREFIX}'; then rm -rf '${remote_backup}'; else if test -e '${remote_backup}'; then mv '${remote_backup}' '${REMOTE_PREFIX}'; fi; exit 1; fi && echo RMW_MDDS_FULL_OVERLAY_SWAP_OK"
  )"
  printf '%s\n' "${swap_output}"
  grep -q '^RMW_MDDS_FULL_OVERLAY_SWAP_OK$' <<<"${swap_output}" || {
    echo "RESULT|rmw_mdds_full_overlay_deploy|FAIL|device=${device_id}|stage=atomic_swap" >&2
    exit 1
  }

  send_verified "${device_id}" "${TMP_MANIFEST}" "${remote_manifest}"
  manifest_output="$(
    capture_hdc_shell "${device_id}" \
      "cd '${REMOTE_PREFIX}' && sha256sum -c '.rmw_mdds_full_overlay.sha256' && echo RMW_MDDS_FULL_OVERLAY_MANIFEST_OK"
  )"
  printf '%s\n' "${manifest_output}"
  grep -q '^RMW_MDDS_FULL_OVERLAY_MANIFEST_OK$' <<<"${manifest_output}" || {
    echo "RESULT|rmw_mdds_full_overlay_deploy|FAIL|device=${device_id}|stage=artifact_verify" >&2
    exit 1
  }

  if [[ -n "${BRIDGE_SO}" && -f "${BRIDGE_SO}" ]]; then
    remote_bridge="${REMOTE_PREFIX}/lib/libmdds_bridge_shared.z.so"
    bridge_sha="$(sha256sum "${BRIDGE_SO}" | cut -d ' ' -f 1)"
    send_verified "${device_id}" "${BRIDGE_SO}" "${remote_bridge}"
    if [[ "${REMOTE_BRIDGE_ALIAS}" != "${remote_bridge}" ]]; then
      send_verified "${device_id}" "${BRIDGE_SO}" "${REMOTE_BRIDGE_ALIAS}"
    fi
    bridge_output="$(
      capture_hdc_shell "${device_id}" \
        "chmod 755 '${remote_bridge}' '${REMOTE_BRIDGE_ALIAS}' && test \"\$(sha256sum '${remote_bridge}' | cut -d ' ' -f 1)\" = '${bridge_sha}' && test \"\$(sha256sum '${REMOTE_BRIDGE_ALIAS}' | cut -d ' ' -f 1)\" = '${bridge_sha}' && echo RMW_MDDS_FULL_OVERLAY_BRIDGE_OK"
    )"
    printf '%s\n' "${bridge_output}"
    grep -q '^RMW_MDDS_FULL_OVERLAY_BRIDGE_OK$' <<<"${bridge_output}" || {
      echo "RESULT|rmw_mdds_full_overlay_deploy|FAIL|device=${device_id}|stage=bridge_verify" >&2
      exit 1
    }
  fi

  if [[ -n "${SOFTBUS_CLIENT_SO}" && -f "${SOFTBUS_CLIENT_SO}" ]]; then
    remote_softbus="${REMOTE_PREFIX}/lib/libsoftbus_client.z.so"
    softbus_sha="$(sha256sum "${SOFTBUS_CLIENT_SO}" | cut -d ' ' -f 1)"
    send_verified "${device_id}" "${SOFTBUS_CLIENT_SO}" "${remote_softbus}"
    softbus_output="$(
      capture_hdc_shell "${device_id}" \
        "chmod 755 '${remote_softbus}' && test \"\$(sha256sum '${remote_softbus}' | cut -d ' ' -f 1)\" = '${softbus_sha}' && echo RMW_MDDS_FULL_OVERLAY_SOFTBUS_OK"
    )"
    printf '%s\n' "${softbus_output}"
    grep -q '^RMW_MDDS_FULL_OVERLAY_SOFTBUS_OK$' <<<"${softbus_output}" || {
      echo "RESULT|rmw_mdds_full_overlay_deploy|FAIL|device=${device_id}|stage=softbus_verify" >&2
      exit 1
    }
  fi

  echo "RESULT|rmw_mdds_full_overlay_deploy|PASS|device=${device_id}|rmw_sha=${LOCAL_RMW_SHA}|dynamic_probe_sha=${LOCAL_DYNAMIC_PROBE_SHA}|broker_dynamic_probe_sha=${LOCAL_BROKER_DYNAMIC_PROBE_SHA}|broker_dynamic_runner_sha=${LOCAL_BROKER_DYNAMIC_RUNNER_SHA}|archive_sha=${LOCAL_TARBALL_SHA}"
done

echo "rmw_mdds_full_overlay_deploy_ok"
