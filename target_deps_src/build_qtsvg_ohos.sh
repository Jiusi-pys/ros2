#!/usr/bin/env bash
# Cross-build qtsvg 5.15.8 for aarch64-linux-ohos (provides the SVG image
# plugin libqsvg.so + libQt5Svg.so; without it rviz's .svg cursors/icons fall
# back to defaults with "Could not load pixmap" warnings).
#
# Idempotent: sources are only downloaded/extracted once, the build dir is
# reused. Delete qtsvg-everywhere-src-5.15.8/build-ohos to force a rebuild.
#
# NB: do NOT set MSYS2_ARG_CONV_EXCL for this build (MSYS path conversion of
# qmake/make flags breaks), and PATH entries must be POSIX-form.
set -euo pipefail
cd "$(dirname "$0")/.."

WORKSPACE_ROOT="$(pwd -W 2>/dev/null || pwd)"
SRC_DIR="${WORKSPACE_ROOT}/target_deps_src"
QTSVG_SRC="${SRC_DIR}/qtsvg-everywhere-src-5.15.8"

export PATH="$(pwd)/.pixi/envs/default/Library/bin:$PATH"

# The oh-clang mkspec reads OHOS_SDK_PATH from the environment; export it so
# the recursive qmake invocations spawned by make (which do not go through
# cross-qmake.bat) also see it. Windows path form, qmake.exe is a win binary.
export OHOS_SDK_PATH='C:\Users\17715\Downloads\commandline-tools-windows-x64-6.1.1.300\command-line-tools\sdk\default\openharmony'

if [ ! -f "$SRC_DIR/qtsvg-everywhere-src-5.15.8.tar.xz" ]; then
  echo "== downloading qtsvg 5.15.8"
  curl -fSL --retry 3 -o "$SRC_DIR/qtsvg-everywhere-src-5.15.8.tar.xz" \
    "https://download.qt.io/archive/qt/5.15/5.15.8/submodules/qtsvg-everywhere-opensource-src-5.15.8.tar.xz"
fi
if [ ! -d "$QTSVG_SRC" ]; then
  tar -xf "$(cygpath "$SRC_DIR/qtsvg-everywhere-src-5.15.8.tar.xz")" -C "$(cygpath "$SRC_DIR")"
fi

mkdir -p "$QTSVG_SRC/build-ohos"
cd "$QTSVG_SRC/build-ohos"

echo "== qmake qtsvg (oh-clang via cross-qmake)"
cmd //c "$(cygpath -w "$SRC_DIR/qt-host-tools/cross-qmake.bat")" "$(cygpath -w "$QTSVG_SRC")"

echo "== building qtsvg"
make -j"$(nproc)"
make install

echo "== done. qtsvg installed into $WORKSPACE_ROOT/install_ohos (lib/libQt5Svg.so, plugins/imageformats/libqsvg.so)"
