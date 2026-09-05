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
WS="$(cygpath -m "$WS_UNIX")"
SRC_DIR="$WS_UNIX/target_deps_src"
PYQT_SRC="$WS_UNIX/target_deps_src/pyqt/PyQt5-5.15.11"
BUILD_DIR="$PYQT_SRC/build-ohos"
SIPBUILD="$WS_UNIX/.pixi/envs/default/Scripts/sip-build.exe"
CROSS_QMAKE="$(cygpath -w "$WS_UNIX/target_deps_src/qt-host-tools/cross-qmake.bat")"
HOST_PY_INC="$WS/.pixi/envs/default/Include"
TARGET_PY_INC="$WS/python_target/usr/include/python3.12"
TARGET_PY_LIB="$WS/python_target/usr/lib/libpython3.12.so"
SITE_PKGS="$WS_UNIX/install_ohos/Lib/site-packages"
EXT_SUFFIX='cpython-312-aarch64-linux-ohos.so'
MODULES="QtCore QtNetwork QtGui QtWidgets QtPrintSupport QtTest QtXml"
# shellcheck source=../lib/locked_sources.sh
source "$SRC_DIR/lib/locked_sources.sh"

OHOS_NATIVE="${OHOS_NATIVE_SDK:-}"
OHOS_SDK_ROOT="${OHOS_SDK_PATH:-}"
if [ -z "$OHOS_SDK_ROOT" ] && [ -n "$OHOS_NATIVE" ]; then
  OHOS_SDK_ROOT="$(dirname "$OHOS_NATIVE")"
fi
[ -n "$OHOS_SDK_ROOT" ] || { echo "ERROR: set OHOS_SDK_PATH or OHOS_NATIVE_SDK" >&2; exit 2; }
[ -n "$OHOS_NATIVE" ] || OHOS_NATIVE="$OHOS_SDK_ROOT/native"
OHOS_SDK_FINGERPRINT="$(sdk_fingerprint "$OHOS_NATIVE")"
export OHOS_SDK_PATH="$(cygpath -w "$OHOS_SDK_ROOT" 2>/dev/null || printf '%s' "$OHOS_SDK_ROOT")"

"$SRC_DIR/bootstrap_qt_host_tools.sh"
[ -f "$WS_UNIX/target_deps_src/qtbase-everywhere-src-5.15.8/build-ohos/.install-done" ] || {
  echo "ERROR: Qtbase recipe marker missing; run build_qtbase_ohos.sh" >&2
  exit 2
}
[ -f "$WS_UNIX/python_target/usr/include/python3.12/Python.h" ] || {
  echo "ERROR: target CPython headers are missing" >&2
  exit 2
}
[ -f "$WS_UNIX/python_target/usr/lib/libpython3.12.so" ] || {
  echo "ERROR: target libpython3.12.so is missing" >&2
  exit 2
}

RECIPE_FINGERPRINT="$(recipe_fingerprint \
  "$(lock_field PyQt5-5.15.11.tar.gz 3)" \
  "$(sha256_file "$WS_UNIX/target_deps_src/qtbase-everywhere-src-5.15.8/build-ohos/.install-done")" \
  "$(sha256_file "$WS_UNIX/python_target/usr/include/python3.12/Python.h")" \
  "$(sha256_file "$WS_UNIX/python_target/usr/lib/libpython3.12.so")" \
  "$OHOS_SDK_FINGERPRINT" "$(sha256_file "$0")")"
MARKER="$BUILD_DIR/.install-done"
if marker_matches "$MARKER" "$RECIPE_FINGERPRINT" && \
   [ -f "$SITE_PKGS/PyQt5/QtCore.$EXT_SUFFIX" ]; then
  echo "PyQt5 already installed, nothing to do"
  exit 0
fi

if [ "${1:-}" != "--make-only" ]; then
  rm -rf "$BUILD_DIR"
  (cd "$PYQT_SRC" && "$SIPBUILD" --no-make --confirm-license \
    --qmake "$CROSS_QMAKE" \
    --qmake-setting 'CONFIG += no_qt_rpath' \
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
export PATH="/usr/bin:$WS_UNIX/.pixi/envs/default/Library/bin:$WS_UNIX/.pixi/envs/default/Library/usr/bin:$PATH"
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
  dynamic_tags="$("$OHOS_NATIVE/llvm/bin/llvm-readelf.exe" -d "$SITE_PKGS/PyQt5/$d.$EXT_SUFFIX")"
  if printf '%s\n' "$dynamic_tags" | grep -Eq '\((RPATH|RUNPATH)\)'; then
    echo "ERROR: PyQt retained a build-host runtime search path: $d" >&2
    exit 1
  fi
done
write_marker "$MARKER" "$RECIPE_FINGERPRINT"
echo "PyQt5 installed into $SITE_PKGS/PyQt5"
ls "$SITE_PKGS/PyQt5"
