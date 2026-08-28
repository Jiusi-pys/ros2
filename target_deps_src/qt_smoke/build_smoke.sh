#!/usr/bin/env bash
# Cross-compile the minimal Qt5 smoke test for the OHOS board.
# Run from the workspace root after qtbase `make install` into install_ohos/.
set -euo pipefail

SDK='C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony'
NATIVE="$SDK/native"
CXX="$NATIVE/llvm/bin/clang++.exe"
SYSROOT="$NATIVE/sysroot"
PREFIX="C:/Users/17715/Documents/codes/M-DDS/ros2/install_ohos"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$CXX" -target aarch64-linux-ohos --sysroot="$SYSROOT" -D__MUSL__ \
  -fPIC -O2 \
  -I"$PREFIX/include" \
  -I"$PREFIX/include/QtCore" \
  -I"$PREFIX/include/QtGui" \
  -I"$PREFIX/include/QtWidgets" \
  "$SRC_DIR/qt_smoke.cpp" -o "$SRC_DIR/qt_smoke" \
  -L"$PREFIX/lib" -lQt5Widgets -lQt5Gui -lQt5Core \
  -Wl,--export-dynamic -Wl,-rpath,/data/local/tmp/ros2/lib

echo "built: $SRC_DIR/qt_smoke"
file "$SRC_DIR/qt_smoke" || true
