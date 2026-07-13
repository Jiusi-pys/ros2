#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/tools/run_rmw_mdds_test_rmw_board.sh"
# language: "shell"
# summary: "Bundles, deploys, and runs current AArch64 test_rmw_implementation artifacts on RK3588A boards."
# symbols: ["capture_hdc_shell", "send_verified", "require_local_file"]
# generated_by: "codex"
# codex-file-meta: end

set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_test_rmw_board.sh <device-id> [device-id ...]

Environment:
  HDC_BIN                         HDC executable, default: hdc
  ROS2_OHOS_BUILD_BASE            Build base containing test_rmw_implementation
  ROS2_OHOS_INSTALL_BASE          Current AArch64 overlay install prefix
  ROS2_OHOS_REMOTE_PREFIX         Board runtime prefix
  RMW_MDDS_REMOTE_TEST_ROOT       Board test root
  RMW_MDDS_TEST_TIMEOUT_SECONDS   Per-program timeout, default: 240
  RMW_MDDS_HDC_TIMEOUT_SECONDS    Host shell timeout, default: 5400
EOF
}

if [[ $# -lt 1 ]]; then
  usage
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HDC_BIN="${HDC_BIN:-hdc}"
BUILD_BASE="${ROS2_OHOS_BUILD_BASE:-${ROOT_DIR}/build/ohos-colcon-rk3588a-clean}"
INSTALL_BASE="${ROS2_OHOS_INSTALL_BASE:-${ROOT_DIR}/install/ohos-colcon-rk3588a-clean}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
REMOTE_TEST_ROOT="${RMW_MDDS_REMOTE_TEST_ROOT:-/data/local/tmp/rmw_mdds_test_rmw_current}"
REMOTE_TARBALL="${REMOTE_TEST_ROOT}.tgz"
TEST_TIMEOUT_SECONDS="${RMW_MDDS_TEST_TIMEOUT_SECONDS:-240}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-5400}"
HDC_SEND_TIMEOUT="${RMW_MDDS_HDC_SEND_TIMEOUT:-300s}"
HDC_SEND_VERIFY="${ROOT_DIR}/ohos/tools/hdc_send_verify.sh"
BOARD_RUNNER="${ROOT_DIR}/ohos/tools/rmw_mdds_test_rmw_board_runner.sh"

TESTS=(
  test_client
  test_create_destroy_node
  test_duration_infinite
  test_event
  test_graph_api
  test_init_options
  test_init_shutdown
  test_publisher
  test_publisher_allocator
  test_qos_profile_check_compatible
  test_serialize_deserialize
  test_service
  test_subscription
  test_subscription_allocator
  test_unique_identifiers
  test_wait_set
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
  if [[ ${status} -ne 0 && ${status} -ne 139 ]]; then
    echo "HOST_HDC_STATUS=${status}"
  fi
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

require_local_file() {
  local path="$1"
  [[ -f "${path}" ]] || {
    echo "Missing local file: ${path}" >&2
    exit 1
  }
}

require_local_file "${HDC_SEND_VERIFY}"
require_local_file "${BOARD_RUNNER}"
require_local_file "${INSTALL_BASE}/lib/librmw_mdds_cpp.so"
require_local_file "${INSTALL_BASE}/lib/libmemory_tools.so"

TMP_STAGE="$(mktemp -d /tmp/rmw_mdds_test_rmw.XXXXXX)"
TMP_TARBALL="$(mktemp /tmp/rmw_mdds_test_rmw.XXXXXX.tgz)"
trap 'rm -rf "${TMP_STAGE}"; rm -f "${TMP_TARBALL}"' EXIT
mkdir -p "${TMP_STAGE}/bin" "${TMP_STAGE}/lib"
cp "${BOARD_RUNNER}" "${TMP_STAGE}/runner.sh"
chmod 755 "${TMP_STAGE}/runner.sh"

for test_name in "${TESTS[@]}"; do
  test_binary="${BUILD_BASE}/test_rmw_implementation/${test_name}"
  require_local_file "${test_binary}"
  readelf -h "${test_binary}" | grep -qE 'Machine:[[:space:]]+AArch64' || {
    echo "Not an AArch64 test binary: ${test_binary}" >&2
    exit 1
  }
  cp "${test_binary}" "${TMP_STAGE}/bin/${test_name}"
  chmod 755 "${TMP_STAGE}/bin/${test_name}"
done

mapfile -t TEST_LIBS < <(
  find "${INSTALL_BASE}/lib" -maxdepth 1 -type f \
    \( -name 'libtest_msgs*.so*' -o -name 'libmemory_tools*.so*' \) -print | sort
)
if [[ ${#TEST_LIBS[@]} -lt 10 ]]; then
  echo "Incomplete test_rmw_implementation library closure: found ${#TEST_LIBS[@]} files" >&2
  exit 1
fi
for library in "${TEST_LIBS[@]}"; do
  cp "${library}" "${TMP_STAGE}/lib/"
done

(
  cd "${TMP_STAGE}"
  find bin lib -type f -print0 | sort -z | xargs -0 sha256sum
  sha256sum runner.sh
) >"${TMP_STAGE}/MANIFEST.sha256"
tar -C "${TMP_STAGE}" -czf "${TMP_TARBALL}" .
BUNDLE_SHA="$(sha256sum "${TMP_TARBALL}" | cut -d ' ' -f 1)"

for device_id in "$@"; do
  send_verified "${device_id}" "${TMP_TARBALL}" "${REMOTE_TARBALL}"
  deploy_output="$(
    capture_hdc_shell "${device_id}" \
      "test \"\$(sha256sum '${REMOTE_TARBALL}' | cut -d ' ' -f 1)\" = '${BUNDLE_SHA}' && rm -rf '${REMOTE_TEST_ROOT}.incoming' && mkdir -p '${REMOTE_TEST_ROOT}.incoming' && tar xzf '${REMOTE_TARBALL}' -C '${REMOTE_TEST_ROOT}.incoming' && cd '${REMOTE_TEST_ROOT}.incoming' && sha256sum -c MANIFEST.sha256 >/dev/null && cd / && rm -rf '${REMOTE_TEST_ROOT}' && mv '${REMOTE_TEST_ROOT}.incoming' '${REMOTE_TEST_ROOT}' && chmod 755 '${REMOTE_TEST_ROOT}/runner.sh' '${REMOTE_TEST_ROOT}'/bin/* && echo TEST_RMW_BUNDLE_OK"
  )"
  printf '%s\n' "${deploy_output}"
  grep -q '^TEST_RMW_BUNDLE_OK$' <<<"${deploy_output}" || {
    echo "RESULT|rmw_mdds_test_rmw_board|FAIL|device=${device_id}|stage=deploy" >&2
    exit 1
  }

  run_output="$(
    capture_hdc_shell "${device_id}" \
      "ROS2_OHOS_REMOTE_PREFIX='${REMOTE_PREFIX}' RMW_MDDS_TEST_ROOT='${REMOTE_TEST_ROOT}' RMW_MDDS_TEST_TIMEOUT_SECONDS='${TEST_TIMEOUT_SECONDS}' sh '${REMOTE_TEST_ROOT}/runner.sh'"
  )"
  printf '%s\n' "${run_output}"
  grep -q '^RESULT|rmw_mdds_test_rmw_board|PASS|' <<<"${run_output}" || {
    echo "RESULT|rmw_mdds_test_rmw_board|FAIL|device=${device_id}|stage=run" >&2
    exit 1
  }
  grep -q '^BOARD_RC=0$' <<<"${run_output}" || {
    echo "RESULT|rmw_mdds_test_rmw_board|FAIL|device=${device_id}|stage=board_rc" >&2
    exit 1
  }
  echo "RESULT|rmw_mdds_test_rmw_board|PASS|device=${device_id}|bundle_sha=${BUNDLE_SHA}"
done

echo "rmw_mdds_test_rmw_board_ok"
