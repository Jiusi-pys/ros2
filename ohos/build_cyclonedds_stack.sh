#!/usr/bin/env bash
# Cross-compile the Eclipse CycloneDDS core (libddsc) for KaihongOS/OHOS
# (aarch64-linux-ohos, musl) into a standalone prefix install/ohos-cyclonedds.
#
# Mirrors ohos/build_fastdds_stack.sh but for the CycloneDDS RMW backend.
# Core ddsc needs no idlc (built-in DDS topic descriptors are hand-written .c),
# so cross-compilation is self-contained. Security (OpenSSL) and iceoryx SHM are
# disabled to keep the dependency closure minimal for the first migration.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS_4.1"
if [[ ! -d "${DEFAULT_OHOS_ROOT}/command-line-tools" && -d "/home/kaihong/M-DDS/command-line-tools" ]]; then
  DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS"
fi

COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-${DEFAULT_OHOS_ROOT}/command-line-tools}"

CMAKE_BIN="${ROS2_OHOS_CMAKE:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/cmake}"
NINJA_BIN="${ROS2_OHOS_NINJA:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/ninja}"

OHOS_ARCH="${ROS2_OHOS_ARCH:-arm64-v8a}"
OHOS_STL="${ROS2_OHOS_STL:-c++_static}"
BUILD_TYPE="${ROS2_OHOS_BUILD_TYPE:-Release}"

STACK_INSTALL_DIR="${ROS2_OHOS_CYCLONEDDS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-cyclonedds}"
CYCLONEDDS_BUILD_DIR="${ROS2_OHOS_CYCLONEDDS_BUILD_DIR:-${ROOT_DIR}/build/ohos-cyclonedds}"

TOOLCHAIN_FILE="${ROOT_DIR}/ohos/cmake/kaihongos.toolchain.cmake"

if [[ ! -x "${CMAKE_BIN}" ]]; then echo "cmake not found at ${CMAKE_BIN}" >&2; exit 1; fi
if [[ ! -x "${NINJA_BIN}" ]]; then echo "ninja not found at ${NINJA_BIN}" >&2; exit 1; fi

COMMON_ARGS=(
  -G Ninja
  "-DCMAKE_MAKE_PROGRAM=${NINJA_BIN}"
  "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
  "-DROS2_OHOS_COMMAND_LINE_TOOLS_ROOT=${COMMAND_LINE_TOOLS_ROOT}"
  "-DOHOS_ARCH=${OHOS_ARCH}"
  "-DOHOS_STL=${OHOS_STL}"
  "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
)

"${CMAKE_BIN}" -S "${ROOT_DIR}/src/eclipse-cyclonedds/cyclonedds" -B "${CYCLONEDDS_BUILD_DIR}" \
  "${COMMON_ARGS[@]}" \
  "-DCMAKE_INSTALL_PREFIX=${STACK_INSTALL_DIR}" \
  -DBUILD_SHARED_LIBS=ON \
  -DBUILD_TESTING=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_IDLC=OFF \
  -DBUILD_DDSPERF=OFF \
  -DENABLE_SECURITY=NO \
  -DENABLE_LTO=OFF \
  -DENABLE_SSL=NO
"${CMAKE_BIN}" --build "${CYCLONEDDS_BUILD_DIR}" --target install -- -j"$(nproc)"

echo "CycloneDDS standalone stack built:"
echo "  stack prefix: ${STACK_INSTALL_DIR}"
echo "  core lib    : ${STACK_INSTALL_DIR}/lib/libddsc.so"
