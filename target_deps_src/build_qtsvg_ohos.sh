#!/usr/bin/env bash
# Cross-build qtsvg 5.15.8 for aarch64-linux-ohos (provides the SVG image
# plugin libplugins_imageformats_qsvg.so + libQt5Svg.so (oh-clang mkspec);
# without it rviz's .svg cursors/icons fall
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
# shellcheck source=lib/locked_sources.sh
source "$SRC_DIR/lib/locked_sources.sh"

export PATH="/usr/bin:$(pwd)/.pixi/envs/default/Library/bin:$(pwd)/.pixi/envs/default/Library/usr/bin:$PATH"

# The oh-clang mkspec reads OHOS_SDK_PATH from the environment. Accept the SDK
# root directly or derive it from the native SDK used by the CMake toolchain.
OHOS_NATIVE="${OHOS_NATIVE_SDK:-}"
OHOS_SDK_ROOT="${OHOS_SDK_PATH:-}"
if [ -z "$OHOS_SDK_ROOT" ] && [ -n "$OHOS_NATIVE" ]; then
  OHOS_SDK_ROOT="$(dirname "$OHOS_NATIVE")"
fi
[ -n "$OHOS_SDK_ROOT" ] || {
  echo "ERROR: set OHOS_SDK_PATH or OHOS_NATIVE_SDK" >&2
  exit 2
}
[ -n "$OHOS_NATIVE" ] || OHOS_NATIVE="$OHOS_SDK_ROOT/native"
OHOS_SDK_FINGERPRINT="$(sdk_fingerprint "$OHOS_NATIVE")"
export OHOS_SDK_PATH="$(cygpath -w "$OHOS_SDK_ROOT" 2>/dev/null || printf '%s' "$OHOS_SDK_ROOT")"

fetch_locked qtsvg-everywhere-src-5.15.8.tar.xz \
  "$SRC_DIR/qtsvg-everywhere-src-5.15.8.tar.xz"
if [ ! -d "$QTSVG_SRC" ]; then
  tar -xf "$(cygpath "$SRC_DIR/qtsvg-everywhere-src-5.15.8.tar.xz")" -C "$(cygpath "$SRC_DIR")"
fi

mkdir -p "$QTSVG_SRC/build-ohos"
cd "$QTSVG_SRC/build-ohos"

RECIPE_FINGERPRINT="$(recipe_fingerprint \
  "$(lock_field qtsvg-everywhere-src-5.15.8.tar.xz 3)" \
  "$(sha256_file "$SRC_DIR/qtbase-5.15.8-ohos.patch")" \
  "$(sha256_file "$WORKSPACE_ROOT/target_deps_src/qt-host-tools/cross-qmake.bat")" \
  "$OHOS_SDK_FINGERPRINT" "$(sha256_file "$0")")"
MARKER="$QTSVG_SRC/build-ohos/.install-done"
if marker_matches "$MARKER" "$RECIPE_FINGERPRINT" && \
   [ -f "$WORKSPACE_ROOT/install_ohos/lib/libQt5Svg.so" ] && \
   [ -f "$WORKSPACE_ROOT/install_ohos/plugins/imageformats/libplugins_imageformats_qsvg.so" ]; then
  echo "== qtsvg already installed, nothing to do"
  exit 0
fi

echo "== qmake qtsvg (oh-clang via cross-qmake)"
cmd //c "$(cygpath -w "$SRC_DIR/qt-host-tools/cross-qmake.bat")" "$(cygpath -w "$QTSVG_SRC")"

echo "== building qtsvg"
make -j"$(nproc)"
make install
write_marker "$MARKER" "$RECIPE_FINGERPRINT"

echo "== done. qtsvg installed into $WORKSPACE_ROOT/install_ohos (lib/libQt5Svg.so, plugins/imageformats/libplugins_imageformats_qsvg.so)"
