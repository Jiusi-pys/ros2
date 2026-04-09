#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SMOKE_INSTALL_DIR="${ROS2_OHOS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-arm64-rcl7}"
ROS2_PREFIX="${ROS2_OHOS_RMW_PREFIX:-${ROOT_DIR}/install/ohos-ros2}"
FASTDDS_PREFIX="${ROS2_OHOS_FASTDDS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-fastdds}"
COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-/home/kaihong/M-DDS_4.1/command-line-tools}"
LIBCXX_SHARED="${ROS2_OHOS_LIBCXX_SHARED:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/llvm/lib/aarch64-linux-ohos/libc++_shared.so}"

runtime_libs=(
  "${SMOKE_INSTALL_DIR}/lib/libros2_rcpputils.so"
  "${SMOKE_INSTALL_DIR}/lib/libros2_rcutils.so"
  "${ROS2_PREFIX}/lib/"*.so
  "${FASTDDS_PREFIX}/lib/libfastcdr.so.2"
  "${FASTDDS_PREFIX}/lib/libfastrtps.so.2.14"
  "${ROS2_PREFIX}/opt/libyaml_vendor/lib/libyaml.so"
  "${ROS2_PREFIX}/opt/spdlog_vendor/lib/libspdlog.so.1.12"
  "${LIBCXX_SHARED}"
)

for artifact in "${runtime_libs[@]}"; do
  if [[ ! -f "${artifact}" ]]; then
    echo "Missing runtime artifact: ${artifact}" >&2
    exit 1
  fi
done

printf '%s\n' "${runtime_libs[@]}" | paste -sd: -
