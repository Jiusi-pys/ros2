#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS_4.1"
if [[ ! -d "${DEFAULT_OHOS_ROOT}/command-line-tools" && -d "/home/kaihong/M-DDS/command-line-tools" ]]; then
  DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS"
fi

COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-${DEFAULT_OHOS_ROOT}/command-line-tools}"
CMAKE_BIN="${ROS2_OHOS_CMAKE:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/cmake}"
NINJA_BIN="${ROS2_OHOS_NINJA:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/ninja}"

BUILD_DIR="${ROS2_OHOS_PYTHON312_DYNLOAD_BUILD_DIR:-${ROOT_DIR}/build/ohos-python312-dynload}"
TARGET_PYTHON_RUNTIME_ROOT="${ROS2_OHOS_TARGET_PYTHON_RUNTIME_ROOT:-${ROOT_DIR}/build/ohos-python-runtime/usr}"
TARGET_PYTHON_VERSION="${ROS2_OHOS_TARGET_PYTHON_VERSION:-3.12}"
TARGET_PYTHON_EXTENSION_SUFFIX="${ROS2_OHOS_TARGET_PYTHON_EXTENSION_SUFFIX:-.cpython-312-aarch64-linux-ohos.so}"

if [[ -n "${ROS2_OHOS_CPYTHON312_SOURCE:-}" ]]; then
  CPYTHON_SOURCE_DIR="${ROS2_OHOS_CPYTHON312_SOURCE}"
else
  CPYTHON_SOURCE_DIR=""
  for candidate in \
    "${ROOT_DIR}/build/Python-3.12.7" \
    "/tmp/cpython312-stdlib/Python-3.12.7" \
    "/tmp/Python-3.12.7"; do
    if [[ -f "${candidate}/Modules/arraymodule.c" ]]; then
      CPYTHON_SOURCE_DIR="${candidate}"
      break
    fi
  done
fi

if [[ -z "${CPYTHON_SOURCE_DIR}" || ! -f "${CPYTHON_SOURCE_DIR}/Modules/arraymodule.c" ]]; then
  cat >&2 <<'EOF'
CPython 3.12.7 source tree not found.
Set ROS2_OHOS_CPYTHON312_SOURCE to a CPython 3.12.7 source directory, or
extract Python-3.12.7 under build/, /tmp/cpython312-stdlib/, or /tmp/.
EOF
  exit 1
fi

for required in "${CMAKE_BIN}" "${NINJA_BIN}"; do
  if [[ ! -x "${required}" ]]; then
    echo "required tool not found: ${required}" >&2
    exit 1
  fi
done
if [[ ! -f "${TARGET_PYTHON_RUNTIME_ROOT}/include/python${TARGET_PYTHON_VERSION}/Python.h" ]]; then
  echo "target Python headers not found under ${TARGET_PYTHON_RUNTIME_ROOT}" >&2
  exit 1
fi
if [[ ! -f "${TARGET_PYTHON_RUNTIME_ROOT}/lib/libpython${TARGET_PYTHON_VERSION}.so" ]]; then
  echo "target libpython not found under ${TARGET_PYTHON_RUNTIME_ROOT}" >&2
  exit 1
fi

"${CMAKE_BIN}" -S "${ROOT_DIR}/ohos/cmake/python312_dynload" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="${NINJA_BIN}" \
  -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/ohos/cmake/kaihongos.toolchain.cmake" \
  -DROS2_OHOS_COMMAND_LINE_TOOLS_ROOT="${COMMAND_LINE_TOOLS_ROOT}" \
  -DOHOS_ARCH="${ROS2_OHOS_ARCH:-arm64-v8a}" \
  -DOHOS_STL="${ROS2_OHOS_STL:-c++_static}" \
  -DCMAKE_BUILD_TYPE="${ROS2_OHOS_BUILD_TYPE:-Release}" \
  -DCPYTHON_SOURCE_DIR="${CPYTHON_SOURCE_DIR}" \
  -DTARGET_PYTHON_RUNTIME_ROOT="${TARGET_PYTHON_RUNTIME_ROOT}" \
  -DTARGET_PYTHON_VERSION="${TARGET_PYTHON_VERSION}" \
  -DTARGET_PYTHON_EXTENSION_SUFFIX="${TARGET_PYTHON_EXTENSION_SUFFIX}"

"${CMAKE_BIN}" --build "${BUILD_DIR}" --target install -- -j"$(nproc)"

ln -sf "libpython${TARGET_PYTHON_VERSION}.so" \
  "${TARGET_PYTHON_RUNTIME_ROOT}/lib/libpython${TARGET_PYTHON_VERSION}.so.1.0"

echo "Python ${TARGET_PYTHON_VERSION} lib-dynload modules installed:"
echo "  source: ${CPYTHON_SOURCE_DIR}"
echo "  runtime: ${TARGET_PYTHON_RUNTIME_ROOT}"
echo "  dynload: ${TARGET_PYTHON_RUNTIME_ROOT}/lib/python${TARGET_PYTHON_VERSION}/lib-dynload"
