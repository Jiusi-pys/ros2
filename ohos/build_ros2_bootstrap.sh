#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-/home/kaihong/M-DDS_4.1/command-line-tools}"
OPENHARMONY_ROOT="${ROS2_OHOS_OPENHARMONY_ROOT:-/home/kaihong/M-DDS_4.1/OpenHarmony}"
RELEASE_SITE_PACKAGES_ROOT="${ROS2_OHOS_RELEASE_SITE_PACKAGES:-${OPENHARMONY_ROOT}/out/arm64/khs_3588s_sbc/packages/phone/data/local/release/usr/lib/python3.12/site-packages}"

CMAKE_BIN="${ROS2_OHOS_CMAKE:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/cmake}"
NINJA_BIN="${ROS2_OHOS_NINJA:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/ninja}"
PYTHON_BIN="${ROS2_OHOS_PYTHON_HOST:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/llvm/python3/bin/python3}"

PREFIX="${ROS2_OHOS_ROS2_PREFIX:-${ROOT_DIR}/install/ohos-ros2}"
BUILD_ROOT="${ROS2_OHOS_ROS2_BUILD_ROOT:-${ROOT_DIR}/build/ohos-ros2}"
PYDEPS_ROOT="${ROS2_OHOS_ROS2_PYDEPS_ROOT:-${BUILD_ROOT}/pydeps}"
OHOS_ARCH="${ROS2_OHOS_ARCH:-arm64-v8a}"
OHOS_STL="${ROS2_OHOS_STL:-c++_static}"
BUILD_TYPE="${ROS2_OHOS_BUILD_TYPE:-Release}"
TOOLCHAIN_FILE="${ROOT_DIR}/ohos/cmake/kaihongos.toolchain.cmake"

if [[ ! -x "${CMAKE_BIN}" ]]; then
  echo "cmake not found at ${CMAKE_BIN}" >&2
  exit 1
fi
if [[ ! -x "${NINJA_BIN}" ]]; then
  echo "ninja not found at ${NINJA_BIN}" >&2
  exit 1
fi
if [[ ! -x "${PYTHON_BIN}" ]]; then
  echo "python not found at ${PYTHON_BIN}" >&2
  exit 1
fi
if [[ ! -d "${RELEASE_SITE_PACKAGES_ROOT}" ]]; then
  echo "release site-packages not found at ${RELEASE_SITE_PACKAGES_ROOT}" >&2
  exit 1
fi

PURELIB="$("${PYTHON_BIN}" - <<'PY' "${PREFIX}"
import sys
import sysconfig
prefix = sys.argv[1]
print(sysconfig.get_path("purelib", vars={"base": prefix, "platbase": prefix}))
PY
)"

mkdir -p "${PYDEPS_ROOT}"
rm -rf "${PYDEPS_ROOT}/catkin_pkg" "${PYDEPS_ROOT}/packaging" "${PYDEPS_ROOT}/pyparsing" "${PYDEPS_ROOT}/yaml" "${PYDEPS_ROOT}/lark" "${PYDEPS_ROOT}/em.py"
ln -s "${RELEASE_SITE_PACKAGES_ROOT}/catkin_pkg" "${PYDEPS_ROOT}/catkin_pkg"
ln -s "${RELEASE_SITE_PACKAGES_ROOT}/packaging" "${PYDEPS_ROOT}/packaging"
if [[ -e "${RELEASE_SITE_PACKAGES_ROOT}/pyparsing" ]]; then
  ln -s "${RELEASE_SITE_PACKAGES_ROOT}/pyparsing" "${PYDEPS_ROOT}/pyparsing"
fi
if [[ -e "${RELEASE_SITE_PACKAGES_ROOT}/yaml" ]]; then
  ln -s "${RELEASE_SITE_PACKAGES_ROOT}/yaml" "${PYDEPS_ROOT}/yaml"
fi
if [[ -e "${RELEASE_SITE_PACKAGES_ROOT}/lark" ]]; then
  ln -s "${RELEASE_SITE_PACKAGES_ROOT}/lark" "${PYDEPS_ROOT}/lark"
fi
ln -s "${RELEASE_SITE_PACKAGES_ROOT}/em.py" "${PYDEPS_ROOT}/em.py"

BASE_PYTHONPATH="${PURELIB}:${PYDEPS_ROOT}"

run_python() {
  PYTHONPATH="${BASE_PYTHONPATH}${PYTHONPATH:+:${PYTHONPATH}}" "${PYTHON_BIN}" "$@"
}

run_cmake() {
  PYTHONPATH="${BASE_PYTHONPATH}${PYTHONPATH:+:${PYTHONPATH}}" "${CMAKE_BIN}" "$@"
}

install_python_package() {
  local source_dir="$1"
  echo "[python] ${source_dir}"
  (
    cd "${source_dir}"
    run_python setup.py install \
      --prefix "${PREFIX}" \
      --single-version-externally-managed \
      --record /tmp/ros2-bootstrap-record.txt >/dev/null
  )
}

build_cmake_package() {
  local source_dir="$1"
  local build_dir="$2"
  local -a package_dir_args=()

  for dir in "${PREFIX}"/share/*/cmake "${PREFIX}"/lib/cmake/* "${PREFIX}"/lib/*/cmake; do
    if [[ -d "${dir}" ]]; then
      local package_name
      package_name="$(basename "$(dirname "${dir}")")"
      if [[ "${package_name}" == "cmake" ]]; then
        package_name="$(basename "${dir}")"
      fi
      package_dir_args+=("-D${package_name}_DIR=${dir}")
    fi
  done

  echo "[cmake] ${source_dir}"
  run_cmake -S "${source_dir}" -B "${build_dir}" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="${NINJA_BIN}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DROS2_OHOS_COMMAND_LINE_TOOLS_ROOT="${COMMAND_LINE_TOOLS_ROOT}" \
    -DOHOS_ARCH="${OHOS_ARCH}" \
    -DOHOS_STL="${OHOS_STL}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DCMAKE_PREFIX_PATH="${PREFIX}" \
    -DPython3_EXECUTABLE="${PYTHON_BIN}" \
    -DBUILD_TESTING=OFF \
    "${package_dir_args[@]}"
  run_cmake --build "${build_dir}" --target install -- -j"$(nproc)"
}

mkdir -p "${BUILD_ROOT}"

install_python_package "${ROOT_DIR}/src/ament/ament_package"
install_python_package "${ROOT_DIR}/src/ament/ament_index/ament_index_python"
install_python_package "${ROOT_DIR}/src/ros2/ament_cmake_ros/domain_coordinator"

build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_core" "${BUILD_ROOT}/ament_cmake_core"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_python" "${BUILD_ROOT}/ament_cmake_python"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_test" "${BUILD_ROOT}/ament_cmake_test"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_export_definitions" "${BUILD_ROOT}/ament_cmake_export_definitions"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_export_include_directories" "${BUILD_ROOT}/ament_cmake_export_include_directories"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_export_libraries" "${BUILD_ROOT}/ament_cmake_export_libraries"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_libraries" "${BUILD_ROOT}/ament_cmake_libraries"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_export_link_flags" "${BUILD_ROOT}/ament_cmake_export_link_flags"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_export_targets" "${BUILD_ROOT}/ament_cmake_export_targets"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_export_interfaces" "${BUILD_ROOT}/ament_cmake_export_interfaces"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_export_dependencies" "${BUILD_ROOT}/ament_cmake_export_dependencies"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_target_dependencies" "${BUILD_ROOT}/ament_cmake_target_dependencies"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_gen_version_h" "${BUILD_ROOT}/ament_cmake_gen_version_h"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_include_directories" "${BUILD_ROOT}/ament_cmake_include_directories"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_version" "${BUILD_ROOT}/ament_cmake_version"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake" "${BUILD_ROOT}/ament_cmake"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_gtest" "${BUILD_ROOT}/ament_cmake_gtest"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_gmock" "${BUILD_ROOT}/ament_cmake_gmock"
build_cmake_package "${ROOT_DIR}/src/ament/ament_cmake/ament_cmake_pytest" "${BUILD_ROOT}/ament_cmake_pytest"
build_cmake_package "${ROOT_DIR}/src/ros2/ament_cmake_ros/ament_cmake_ros" "${BUILD_ROOT}/ament_cmake_ros"

echo "ROS 2 ament bootstrap complete:"
echo "  prefix: ${PREFIX}"
echo "  purelib: ${PURELIB}"
