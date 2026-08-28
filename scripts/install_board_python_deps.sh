#!/usr/bin/env bash
# Push the third-party Python packages staged in python_target/sitepkgs/
# (numpy/pyyaml/psutil cross builds + pure-Python wheels) into the
# python312-rk3588a runtime's site-packages on a board.
#
# Usage: ./scripts/install_board_python_deps.sh [board_serial ...]
set -euo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARDS=("$@")
if [ ${#BOARDS[@]} -eq 0 ]; then
  BOARDS=(3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00)
fi

export MSYS2_ARG_CONV_EXCL='*'
SRC_WIN="$(cygpath -w "$(pwd)/python_target/sitepkgs")"
SP=/data/python312-rk3588a/usr/lib/python3.12/site-packages

for board in "${BOARDS[@]}"; do
  echo "== $board"
  # wipe only the entries we manage, then re-push
  "$HDC" -t "$board" shell "cd $SP && ls" > /tmp/board_sp_$board.txt 2>/dev/null || true
  for entry in python_target/sitepkgs/*; do
    name="$(basename "$entry")"
    "$HDC" -t "$board" shell "rm -rf $SP/$name"
    "$HDC" -t "$board" file send "${SRC_WIN}\\\\${name}" "$SP/$name" > /dev/null
  done
  # verify
  "$HDC" -t "$board" shell "LD_LIBRARY_PATH=/data/python312-rk3588a/usr/lib LD_PRELOAD=/data/python312-rk3588a/usr/lib/libpython3.12.so.1.0 /data/python312-rk3588a/usr/bin/python3.12 -c 'import numpy, yaml, psutil, lark, catkin_pkg, argcomplete, packaging, setuptools, pip, em, lxml.etree, cryptography.fernet, cffi, pycparser, pytest, pytest_timeout, pytest_repeat, pytest_rerunfailures, pytest_mock, colcon_core, colcon_cmake, colcon_ros, colcon_test_result, colcon_python_setup_py; print(\"DEPS_OK\")'" \
    | grep -q DEPS_OK && echo "   deps verified" || { echo "   DEPS VERIFICATION FAILED" >&2; exit 1; }
done
