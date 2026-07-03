#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OVERLAY_PREFIX="${ROS2_OHOS_COLCON_INSTALL_BASE:-${ROOT_DIR}/install/ohos-colcon-rk3588a}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

require_file() {
  local file="$1"
  [[ -f "${file}" ]] || fail "missing artifact ${file}"
}

require_no_direct_fastrtps_rmw_dependency() {
  local artifact="$1"
  local deps
  require_file "${artifact}"
  deps="$(readelf -d "${artifact}" 2>/dev/null | sed -n '/NEEDED/p' || true)"
  if grep -Eq 'librmw_fastrtps_(cpp|shared_cpp)\.so' <<<"${deps}"; then
    fail "${artifact} directly depends on Fast DDS RMW libraries: ${deps}"
  fi
}

require_runtime_selectable_rmw_implementation_config() {
  local config="${OVERLAY_PREFIX}/share/rmw_implementation/cmake/rmw_implementation-extras.cmake"
  require_file "${config}"
  grep -q 'if(OFF)' "${config}" ||
    fail "rmw_implementation config still disables runtime selection: ${config}"
}

require_runtime_selectable_rmw_implementation_config
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/librmw_implementation.so"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/librcl.so"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/librcl_lifecycle.so"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/librclcpp.so"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/demo_nodes_cpp/add_two_ints_server"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/action_tutorials_cpp/fibonacci_action_server"
require_no_direct_fastrtps_rmw_dependency \
  "${OVERLAY_PREFIX}/lib/python3.12/site-packages/rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so"

echo "rmw_mdds_artifact_contracts_ok"
