#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/build_ohos.sh"
# language: "shell"
# summary: "Shell file: set -euo pipefail."
# symbols: []
# generated_by: "codebase-frontmatter-summary"
# codex-file-meta: end

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OHOS_DIR="${ROOT_DIR}/ohos"
EXTRA_CMAKE_ARGS=("$@")

DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS_4.1"
if [[ ! -d "${DEFAULT_OHOS_ROOT}/command-line-tools" && -d "/home/kaihong/M-DDS/command-line-tools" ]]; then
  DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS"
fi

COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-${DEFAULT_OHOS_ROOT}/command-line-tools}"
OPENHARMONY_ROOT="${ROS2_OHOS_OPENHARMONY_ROOT:-${DEFAULT_OHOS_ROOT}/OpenHarmony}"
OPENHARMONY_PREBUILTS_ROOT="${ROS2_OHOS_PREBUILTS_ROOT:-${OPENHARMONY_ROOT}/prebuilts}"

CMAKE_BIN="${ROS2_OHOS_CMAKE:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/cmake}"
NINJA_BIN="${ROS2_OHOS_NINJA:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/ninja}"
PYTHON_BIN="${ROS2_OHOS_PYTHON:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/llvm/python3/bin/python3}"
STRIP_BIN="${ROS2_OHOS_STRIP:-${OPENHARMONY_PREBUILTS_ROOT}/clang/ohos/linux-x86_64/llvm/bin/llvm-strip}"

BUILD_DIR="${ROS2_OHOS_BUILD_DIR:-${ROOT_DIR}/build/ohos-arm64}"
INSTALL_DIR="${ROS2_OHOS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-arm64}"
ROS2_PREFIX="${ROS2_OHOS_RMW_PREFIX:-${ROOT_DIR}/install/ohos-ros2}"
ROS2_PYDEPS_ROOT="${ROS2_OHOS_ROS2_PYDEPS_ROOT:-${ROOT_DIR}/build/ohos-ros2/pydeps}"
BUILD_TYPE="${ROS2_OHOS_BUILD_TYPE:-Release}"
OHOS_ARCH="${ROS2_OHOS_ARCH:-arm64-v8a}"
OHOS_STL="${ROS2_OHOS_STL:-c++_static}"
ENABLE_RMW_SMOKE="${ROS2_OHOS_ENABLE_RMW_SMOKE:-ON}"

if [[ ! -x "${CMAKE_BIN}" ]]; then
  echo "cmake not found at ${CMAKE_BIN}" >&2
  exit 1
fi
if [[ ! -x "${NINJA_BIN}" ]]; then
  echo "ninja not found at ${NINJA_BIN}" >&2
  exit 1
fi
if [[ ! -x "${PYTHON_BIN}" ]]; then
  echo "python3 not found at ${PYTHON_BIN}" >&2
  exit 1
fi

ROS2_PURELIB="$("${PYTHON_BIN}" - <<'PY' "${ROS2_PREFIX}"
import sys
import sysconfig
prefix = sys.argv[1]
print(sysconfig.get_path("purelib", vars={"base": prefix, "platbase": prefix}))
PY
)"
ROS2_PYTHONPATH="${ROS2_PURELIB}:${ROS2_PYDEPS_ROOT}"
ROS2_AMENT_PREFIX_PATH="${ROS2_PREFIX}"

AMENT_PREFIX_PATH="${ROS2_AMENT_PREFIX_PATH}${AMENT_PREFIX_PATH:+:${AMENT_PREFIX_PATH}}" \
PYTHONPATH="${ROS2_PYTHONPATH}${PYTHONPATH:+:${PYTHONPATH}}" \
"${CMAKE_BIN}" -S "${OHOS_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="${NINJA_BIN}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
  -DCMAKE_TOOLCHAIN_FILE="${OHOS_DIR}/cmake/kaihongos.toolchain.cmake" \
  -DROS2_OHOS_COMMAND_LINE_TOOLS_ROOT="${COMMAND_LINE_TOOLS_ROOT}" \
  -DOHOS_ARCH="${OHOS_ARCH}" \
  -DOHOS_STL="${OHOS_STL}" \
  -DROS2_OHOS_ENABLE_RMW_SMOKE="${ENABLE_RMW_SMOKE}" \
  -DROS2_OHOS_RMW_PREFIX="${ROS2_PREFIX}" \
  -DPython3_EXECUTABLE="${PYTHON_BIN}" \
  "${EXTRA_CMAKE_ARGS[@]}"

AMENT_PREFIX_PATH="${ROS2_AMENT_PREFIX_PATH}${AMENT_PREFIX_PATH:+:${AMENT_PREFIX_PATH}}" \
PYTHONPATH="${ROS2_PYTHONPATH}${PYTHONPATH:+:${PYTHONPATH}}" \
"${CMAKE_BIN}" --build "${BUILD_DIR}" --target install -- -j"$(nproc)"

if [[ -x "${STRIP_BIN}" ]]; then
  "${STRIP_BIN}" \
    "${INSTALL_DIR}/bin/ros2_ohos_smoke" \
    "${INSTALL_DIR}/lib/libros2_ohos_dummy.so" \
    "${INSTALL_DIR}/lib/libros2_rcpputils.so" \
    "${INSTALL_DIR}/lib/libros2_rcutils.so" || true
  if [[ -f "${INSTALL_DIR}/bin/ros2_ohos_pubsub_smoke" ]]; then
    "${STRIP_BIN}" "${INSTALL_DIR}/bin/ros2_ohos_pubsub_smoke" || true
  fi
fi

echo "Build complete:"
echo "  install dir: ${INSTALL_DIR}"
