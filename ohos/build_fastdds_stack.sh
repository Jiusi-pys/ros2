#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-/home/kaihong/M-DDS_4.1/command-line-tools}"
OPENHARMONY_ROOT="${ROS2_OHOS_OPENHARMONY_ROOT:-/home/kaihong/M-DDS_4.1/OpenHarmony}"
RELEASE_USR_ROOT="${ROS2_OHOS_RELEASE_USR_ROOT:-${OPENHARMONY_ROOT}/out/arm64/khs_3588s_sbc/packages/phone/data/local/release/usr}"

CMAKE_BIN="${ROS2_OHOS_CMAKE:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/cmake}"
NINJA_BIN="${ROS2_OHOS_NINJA:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/ninja}"

OHOS_ARCH="${ROS2_OHOS_ARCH:-arm64-v8a}"
OHOS_STL="${ROS2_OHOS_STL:-c++_static}"
BUILD_TYPE="${ROS2_OHOS_BUILD_TYPE:-Release}"

STACK_INSTALL_DIR="${ROS2_OHOS_FASTDDS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-fastdds}"
SMOKE_INSTALL_DIR="${ROS2_OHOS_FASTDDS_SMOKE_INSTALL_DIR:-${ROOT_DIR}/install/ohos-fastdds-smoke}"

FOONATHAN_BUILD_DIR="${ROS2_OHOS_FOONATHAN_BUILD_DIR:-${ROOT_DIR}/build/ohos-foonathan}"
FASTCDR_BUILD_DIR="${ROS2_OHOS_FASTCDR_BUILD_DIR:-${ROOT_DIR}/build/ohos-fastcdr}"
FASTDDS_BUILD_DIR="${ROS2_OHOS_FASTDDS_BUILD_DIR:-${ROOT_DIR}/build/ohos-fastdds}"
SMOKE_BUILD_DIR="${ROS2_OHOS_FASTDDS_SMOKE_BUILD_DIR:-${ROOT_DIR}/build/ohos-fastdds-basic-example}"
DEPS_INCLUDE_DIR="${ROS2_OHOS_FASTDDS_DEPS_INCLUDE_DIR:-${ROOT_DIR}/build/ohos-fastdds-deps/include}"

TOOLCHAIN_FILE="${ROOT_DIR}/ohos/cmake/kaihongos.toolchain.cmake"

if [[ ! -x "${CMAKE_BIN}" ]]; then
  echo "cmake not found at ${CMAKE_BIN}" >&2
  exit 1
fi

if [[ ! -x "${NINJA_BIN}" ]]; then
  echo "ninja not found at ${NINJA_BIN}" >&2
  exit 1
fi

if [[ ! -f "${RELEASE_USR_ROOT}/include/asio.hpp" ]]; then
  echo "asio.hpp not found under ${RELEASE_USR_ROOT}" >&2
  exit 1
fi

if [[ ! -f "${RELEASE_USR_ROOT}/include/tinyxml2.h" ]]; then
  echo "tinyxml2.h not found under ${RELEASE_USR_ROOT}" >&2
  exit 1
fi

if [[ ! -f "${RELEASE_USR_ROOT}/lib/libtinyxml2.a" ]]; then
  echo "libtinyxml2.a not found under ${RELEASE_USR_ROOT}" >&2
  exit 1
fi

rm -rf "${DEPS_INCLUDE_DIR}"
mkdir -p "${DEPS_INCLUDE_DIR}"
ln -s "${RELEASE_USR_ROOT}/include/asio" "${DEPS_INCLUDE_DIR}/asio"
ln -s "${RELEASE_USR_ROOT}/include/asio.hpp" "${DEPS_INCLUDE_DIR}/asio.hpp"
ln -s "${RELEASE_USR_ROOT}/include/tinyxml2.h" "${DEPS_INCLUDE_DIR}/tinyxml2.h"

COMMON_ARGS=(
  -G Ninja
  "-DCMAKE_MAKE_PROGRAM=${NINJA_BIN}"
  "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
  "-DROS2_OHOS_COMMAND_LINE_TOOLS_ROOT=${COMMAND_LINE_TOOLS_ROOT}"
  "-DOHOS_ARCH=${OHOS_ARCH}"
  "-DOHOS_STL=${OHOS_STL}"
  "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
)

"${CMAKE_BIN}" -S "${ROOT_DIR}/src/eProsima/foonathan_memory_vendor" -B "${FOONATHAN_BUILD_DIR}" \
  "${COMMON_ARGS[@]}" \
  "-DCMAKE_INSTALL_PREFIX=${STACK_INSTALL_DIR}" \
  -DFOONATHAN_MEMORY_FORCE_VENDORED_BUILD=ON \
  -DBUILD_TESTING=OFF
"${CMAKE_BIN}" --build "${FOONATHAN_BUILD_DIR}" --target install -- -j"$(nproc)"

"${CMAKE_BIN}" -S "${ROOT_DIR}/src/eProsima/Fast-CDR" -B "${FASTCDR_BUILD_DIR}" \
  "${COMMON_ARGS[@]}" \
  "-DCMAKE_INSTALL_PREFIX=${STACK_INSTALL_DIR}" \
  -DBUILD_TESTING=OFF \
  -DBUILD_DOCUMENTATION=OFF
"${CMAKE_BIN}" --build "${FASTCDR_BUILD_DIR}" --target install -- -j"$(nproc)"

"${CMAKE_BIN}" -S "${ROOT_DIR}/src/eProsima/Fast-DDS" -B "${FASTDDS_BUILD_DIR}" \
  "${COMMON_ARGS[@]}" \
  "-DCMAKE_INSTALL_PREFIX=${STACK_INSTALL_DIR}" \
  "-Dfastcdr_DIR=${STACK_INSTALL_DIR}/lib/cmake/fastcdr" \
  "-Dfoonathan_memory_DIR=${STACK_INSTALL_DIR}/lib/foonathan_memory/cmake" \
  "-DAsio_INCLUDE_DIR=${DEPS_INCLUDE_DIR}" \
  "-DTINYXML2_INCLUDE_DIR=${DEPS_INCLUDE_DIR}" \
  "-DTINYXML2_LIBRARY=${RELEASE_USR_ROOT}/lib/libtinyxml2.a" \
  -DBUILD_TESTING=OFF \
  -DEPROSIMA_BUILD_TESTS=OFF \
  -DCOMPILE_EXAMPLES=OFF \
  -DCOMPILE_TOOLS=OFF \
  -DFASTDDS_EXAMPLE_TESTS=OFF \
  -DPERFORMANCE_TESTS=OFF \
  -DSYSTEM_TESTS=OFF \
  -DPROFILING_TESTS=OFF \
  -DFASTDDS_STATISTICS=OFF \
  -DSECURITY=OFF \
  -DNO_TLS=ON \
  -DSQLITE3_SUPPORT=OFF \
  -DSHM_TRANSPORT_DEFAULT=OFF
"${CMAKE_BIN}" --build "${FASTDDS_BUILD_DIR}" --target install -- -j"$(nproc)"

"${CMAKE_BIN}" -S "${ROOT_DIR}/src/eProsima/Fast-DDS/examples/cpp/dds/BasicConfigurationExample" -B "${SMOKE_BUILD_DIR}" \
  "${COMMON_ARGS[@]}" \
  "-DCMAKE_INSTALL_PREFIX=${SMOKE_INSTALL_DIR}" \
  "-Dfastcdr_DIR=${STACK_INSTALL_DIR}/lib/cmake/fastcdr" \
  "-Dfastrtps_DIR=${STACK_INSTALL_DIR}/share/fastrtps/cmake" \
  "-Dfoonathan_memory_DIR=${STACK_INSTALL_DIR}/lib/foonathan_memory/cmake"
"${CMAKE_BIN}" --build "${SMOKE_BUILD_DIR}" --target install -- -j"$(nproc)"

echo "FastDDS standalone stack built:"
echo "  stack prefix: ${STACK_INSTALL_DIR}"
echo "  smoke binary: ${SMOKE_INSTALL_DIR}/examples/cpp/dds/BasicConfigurationExample/BasicConfigurationExample"
