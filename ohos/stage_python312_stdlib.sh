#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_PYTHON_VERSION="${ROS2_OHOS_TARGET_PYTHON_VERSION:-3.12}"
TARGET_PYTHON_RUNTIME_ROOT="${ROS2_OHOS_TARGET_PYTHON_RUNTIME_ROOT:-${ROOT_DIR}/build/ohos-python-runtime/usr}"
TARGET_STDLIB="${TARGET_PYTHON_RUNTIME_ROOT}/lib/python${TARGET_PYTHON_VERSION}"

if [[ -n "${ROS2_OHOS_CPYTHON312_SOURCE:-}" ]]; then
  CPYTHON_SOURCE_DIR="${ROS2_OHOS_CPYTHON312_SOURCE}"
else
  CPYTHON_SOURCE_DIR=""
  for candidate in \
    "${ROOT_DIR}/build/Python-3.12.7" \
    "/tmp/cpython312-stdlib/Python-3.12.7" \
    "/tmp/Python-3.12.7"; do
    if [[ -f "${candidate}/Lib/argparse.py" ]]; then
      CPYTHON_SOURCE_DIR="${candidate}"
      break
    fi
  done
fi

if [[ -z "${CPYTHON_SOURCE_DIR}" || ! -f "${CPYTHON_SOURCE_DIR}/Lib/argparse.py" ]]; then
  cat >&2 <<'EOF'
CPython 3.12.7 source tree not found.
Set ROS2_OHOS_CPYTHON312_SOURCE to a CPython 3.12.7 source directory, or
extract Python-3.12.7 under build/, /tmp/cpython312-stdlib/, or /tmp/.
EOF
  exit 1
fi

mkdir -p "${TARGET_STDLIB}"
rsync -a \
  --exclude '/test/' \
  --exclude '/tkinter/' \
  --exclude '/idlelib/' \
  --exclude '/turtledemo/' \
  --exclude '/ensurepip/' \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  --exclude '*.pyo' \
  "${CPYTHON_SOURCE_DIR}/Lib/" \
  "${TARGET_STDLIB}/"

echo "Python ${TARGET_PYTHON_VERSION} pure stdlib staged:"
echo "  source: ${CPYTHON_SOURCE_DIR}/Lib"
echo "  target: ${TARGET_STDLIB}"
