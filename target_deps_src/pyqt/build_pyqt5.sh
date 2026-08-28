#!/usr/bin/env bash
# Cross-build PyQt5 for the OHOS board (aarch64-linux-ohos).
#
# Pipeline:
#   1. sip-build --no-make on the PyQt5 sdist using cross-qmake.bat (host qmake
#      + -qtconf pointing at the qtbase cross build) -> C++ + qmake Makefiles
#   2. sed the generated Makefiles: host python include -> target python
#      sysroot include, and link the target libpython3.12 (the oh-clang mkspec
#      links with --no-undefined, so Py* symbols must resolve at link time)
#   3. make with the pixi GNU make; the oh-clang mkspec drives the NDK clang
#   4. install: copy the produced modules into
#      install_ohos/Lib/site-packages/PyQt5 with the target extension suffix
#
# Prerequisites: qtbase installed into install_ohos (Phase 6a), sip 6.8.6 and
# pyqt-builder pip-installed into the pixi host python, PyQt5 sdist extracted.
set -euo pipefail

WS_UNIX="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WS='C:/Users/17715/Documents/codes/M-DDS/ros2'
PYQT_SRC="$WS_UNIX/target_deps_src/pyqt/PyQt5-5.15.11"
BUILD_DIR="$PYQT_SRC/build-ohos"
SIPBUILD='C:\Users\17715\Documents\codes\M-DDS\ros2\.pixi\envs\default\Scripts\sip-build.exe'
CROSS_QMAKE='C:\Users\17715\Documents\codes\M-DDS\ros2\target_deps_src\qt-host-tools\cross-qmake.bat'
HOST_PY_INC='C:/Users/17715/Documents/codes/M-DDS/ros2/.pixi/envs/default/Include'
TARGET_PY_INC='C:/Users/17715/Documents/codes/M-DDS/ros2/python_target/usr/include/python3.12'
TARGET_PY_LIB='C:/Users/17715/Documents/codes/M-DDS/ros2/python_target/usr/lib/libpython3.12.so'
SITE_PKGS="$WS_UNIX/install_ohos/Lib/site-packages"
EXT_SUFFIX='cpython-312-aarch64-linux-ohos.so'
MODULES="QtCore QtNetwork QtGui QtWidgets QtPrintSupport QtTest QtXml"

if [ "${1:-}" != "--make-only" ]; then
  rm -rf "$BUILD_DIR"
  (cd "$PYQT_SRC" && "$SIPBUILD" --no-make --confirm-license \
    --qmake "$CROSS_QMAKE" \
    --build-dir build-ohos \
    --enable QtCore --enable QtGui --enable QtWidgets --enable QtPrintSupport \
    --enable QtNetwork --enable QtXml --enable QtTest \
    --no-designer-plugin --no-qml-plugin --no-tools \
    --disabled-feature PyQt_SSL --disabled-feature PyQt_Desktop_OpenGL)

  # Fix the generated Makefiles for the cross target.
  for d in $MODULES; do
    mk="$BUILD_DIR/$d/Makefile"
    sed -i "s|$HOST_PY_INC|$TARGET_PY_INC|g" "$mk"
    sed -i "s|^LIBS          = \(.*\)|LIBS          = \1 $TARGET_PY_LIB|" "$mk"
  done
fi

export OSTYPE=msys
export PATH="$WS_UNIX/.pixi/envs/default/Library/bin:$PATH"
export OHOS_SDK_PATH='C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony'
(cd "$BUILD_DIR" && make -j"$(nproc)")

# Install: module .so + the generated python package files.
mkdir -p "$SITE_PKGS/PyQt5"
cp "$BUILD_DIR/__init__.py" "$SITE_PKGS/PyQt5/__init__.py"
# Ship the .sip sources as PyQt5/bindings (same layout as the upstream wheels;
# qt_gui_cpp's sip4 binding generation needs them).
rm -rf "$SITE_PKGS/PyQt5/bindings"
cp -r "$PYQT_SRC/sip" "$SITE_PKGS/PyQt5/bindings"
# The uic subpackage (loadUi support) is pure python; the sdist ships it
# under pyuic/.
cp -r "$PYQT_SRC/pyuic/uic" "$SITE_PKGS/PyQt5/uic"
for d in $MODULES; do
  cp "$BUILD_DIR/$d/lib$d.so" "$SITE_PKGS/PyQt5/$d.$EXT_SUFFIX"
done
echo "PyQt5 installed into $SITE_PKGS/PyQt5"
ls "$SITE_PKGS/PyQt5"
