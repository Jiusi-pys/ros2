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

require_aarch64_elf() {
  local artifact="$1"
  require_file "${artifact}"
  readelf -h "${artifact}" 2>/dev/null | grep -Eq 'Machine:[[:space:]]+AArch64' ||
    fail "${artifact} is not an AArch64 ELF artifact"
}

require_allocator_aware_generated_messages() {
  local allocator_header="${OVERLAY_PREFIX}/include/rosidl_runtime_cpp/rosidl_runtime_cpp/message_allocator.hpp"
  local string_header="${OVERLAY_PREFIX}/include/std_msgs/std_msgs/msg/detail/string__struct.hpp"
  local sequence_header="${OVERLAY_PREFIX}/include/std_msgs/std_msgs/msg/detail/int32_multi_array__struct.hpp"
  require_file "${allocator_header}"
  require_file "${string_header}"
  require_file "${sequence_header}"
  grep -q 'class MessageAllocator<void>' "${allocator_header}" ||
    fail "rosidl_runtime_cpp allocator specialization is missing"
  grep -q 'rebind_alloc<char>' "${string_header}" ||
    fail "std_msgs/String does not bind dynamic storage to the message allocator"
  grep -q 'String_<rosidl_runtime_cpp::MessageAllocator<void>>' "${string_header}" ||
    fail "std_msgs/String default alias is not allocator-aware"
  grep -q 'Int32MultiArray_<rosidl_runtime_cpp::MessageAllocator<void>>' "${sequence_header}" ||
    fail "std_msgs/Int32MultiArray default alias is not allocator-aware"
}

require_no_legacy_rmw_dds_message_allocator() {
  local artifact="$1"
  local symbols
  require_file "${artifact}"
  symbols="$(readelf -Ws "${artifact}" 2>/dev/null | c++filt || true)"
  if grep -Fq 'rmw_dds_common::msg::ParticipantEntitiesInfo_<std::__n1::allocator<void> >' <<<"${symbols}"; then
    fail "${artifact} still references the legacy rmw_dds_common message allocator ABI"
  fi
}

require_runtime_selectable_rmw_implementation_config() {
  local config="${OVERLAY_PREFIX}/share/rmw_implementation/cmake/rmw_implementation-extras.cmake"
  require_file "${config}"
  grep -q 'if(OFF)' "${config}" ||
    fail "rmw_implementation config still disables runtime selection: ${config}"
}

require_runtime_selectable_rmw_implementation_config
require_allocator_aware_generated_messages
require_aarch64_elf "${OVERLAY_PREFIX}/lib/librosidl_runtime_c.so"
require_aarch64_elf "${OVERLAY_PREFIX}/lib/librmw_mdds_cpp.so"
require_aarch64_elf "${OVERLAY_PREFIX}/lib/libstd_msgs__rosidl_typesupport_fastrtps_cpp.so"
require_aarch64_elf "${OVERLAY_PREFIX}/lib/libstd_msgs__rosidl_typesupport_introspection_cpp.so"
require_aarch64_elf \
  "${OVERLAY_PREFIX}/lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_dynamic_loan_probe"
require_aarch64_elf \
  "${OVERLAY_PREFIX}/lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_broker_dynamic_loan_probe"
[[ -x "${OVERLAY_PREFIX}/lib/rmw_mdds_dynamic_loan_probe/broker_dynamic_loan_board_runner.sh" ]] ||
  fail "dynamic broker loan board runner is missing or not executable"
for rmw_artifact in \
  librmw_fastrtps_shared_cpp.so \
  librmw_fastrtps_cpp.so \
  librmw_fastrtps_dynamic_cpp.so \
  librmw_cyclonedds_cpp.so; do
  require_aarch64_elf "${OVERLAY_PREFIX}/lib/${rmw_artifact}"
  require_no_legacy_rmw_dds_message_allocator "${OVERLAY_PREFIX}/lib/${rmw_artifact}"
done
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/librmw_implementation.so"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/librcl.so"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/librcl_lifecycle.so"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/librclcpp.so"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/librosbag2_cpp.so"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/librosbag2_transport.so"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/demo_nodes_cpp/add_two_ints_server"
require_no_direct_fastrtps_rmw_dependency "${OVERLAY_PREFIX}/lib/action_tutorials_cpp/fibonacci_action_server"
require_no_direct_fastrtps_rmw_dependency \
  "${OVERLAY_PREFIX}/lib/python3.12/site-packages/rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so"

ROSBAG2_PY_DIR="${OVERLAY_PREFIX}/lib/python3.12/site-packages/rosbag2_py"
for rosbag2_py_module in \
  _compression_options.so \
  _info.so \
  _message_definitions.so \
  _reader.so \
  _reindexer.so \
  _storage.so \
  _transport.so \
  _writer.so; do
  require_no_direct_fastrtps_rmw_dependency "${ROSBAG2_PY_DIR}/${rosbag2_py_module}"
done

echo "rmw_mdds_artifact_contracts_ok"
