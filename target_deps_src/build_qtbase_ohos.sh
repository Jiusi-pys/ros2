#!/usr/bin/env bash
# Reproducible Qtbase 5.15.8 cross build for aarch64-linux-ohos.
set -euo pipefail
cd "$(dirname "$0")/.."

WORKSPACE_ROOT="$(pwd -W 2>/dev/null || pwd)"
SRC_DIR="$WORKSPACE_ROOT/target_deps_src"
PREFIX="$WORKSPACE_ROOT/install_ohos"
QT_SRC="$SRC_DIR/qtbase-everywhere-src-5.15.8"
QT_BUILD="$QT_SRC/build-ohos"
PATCH_FILE="$SRC_DIR/qtbase-5.15.8-ohos.patch"
HOST_BIN="$SRC_DIR/qt-host-tools/qt5_applications/Qt/bin"
# shellcheck source=lib/locked_sources.sh
source "$SRC_DIR/lib/locked_sources.sh"

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
export OSTYPE=msys
export PATH="/usr/bin:$(cygpath "$WORKSPACE_ROOT/.pixi/envs/default/Library/bin"):$(cygpath "$WORKSPACE_ROOT/.pixi/envs/default/Library/usr/bin"):$PATH"

"$SRC_DIR/bootstrap_qt_host_tools.sh"
fetch_locked qtbase-5.15.8.tar.xz "$SRC_DIR/qtbase-5.15.8.tar.xz"
if [ ! -d "$QT_SRC" ]; then
  tar -xf "$(cygpath "$SRC_DIR/qtbase-5.15.8.tar.xz")" -C "$(cygpath "$SRC_DIR")"
fi
apply_patch_locked "$QT_SRC" "$PATCH_FILE"

RECIPE_FINGERPRINT="$(recipe_fingerprint \
  "$(lock_field qtbase-5.15.8.tar.xz 3)" "$(sha256_file "$PATCH_FILE")" \
  "$(sha256_file "$SRC_DIR/qt-host-tools/.bootstrap-done")" \
  "$OHOS_SDK_FINGERPRINT" "$(sha256_file "$0")")"
MARKER="$QT_BUILD/.install-done"
if marker_matches "$MARKER" "$RECIPE_FINGERPRINT" && \
   [ -f "$PREFIX/lib/libQt5Core.so" ] && \
   [ -f "$PREFIX/plugins/platforms/libplugins_platforms_qoffscreen.so" ]; then
  echo "== Qtbase already installed, nothing to do"
  exit 0
fi

mkdir -p "$QT_BUILD" "$PREFIX/bin"
cd "$QT_BUILD"
if [ ! -f Makefile ]; then
  echo "== configuring Qtbase 5.15.8"
  ../configure \
    -opensource -confirm-license -release -shared \
    -prefix "$PREFIX" \
    -xplatform oh-clang -platform oh-clang \
    -external-hostbindir "$HOST_BIN" \
    -opengl es2 -egl -eglfs -no-gbm -no-kms \
    -no-dbus -no-glib -no-icu -no-openssl -no-cups \
    -qt-pcre -qt-zlib -qt-libpng -qt-libjpeg -qt-freetype -qt-harfbuzz \
    -no-fontconfig -nomake tests -nomake examples -no-pch
fi

echo "== building Qtbase"
make -j"$(nproc)"

# With external host tools qmake is deliberately not cross-built. Seed the
# generated target so Qt's install rule has an explicit, deterministic input.
cp "$HOST_BIN/qmake.exe" "$QT_BUILD/qmake/qmake"

# qmake on Windows cannot qinstall the deeply relative eglfs_emu path. Replace
# only that generated path with its absolute build-tree equivalent.
EGLFS_MAKEFILE="$QT_BUILD/src/plugins/platforms/eglfs/deviceintegration/eglfs_emu/Makefile"
[ -f "$EGLFS_MAKEFILE" ] || { echo "ERROR: eglfs_emu Makefile missing" >&2; exit 1; }
sed -i "s|\.\./\.\./\.\./\.\./\.\./\.\./plugins/egldeviceintegrations/|$QT_BUILD/plugins/egldeviceintegrations/|g" \
  "$EGLFS_MAKEFILE"

echo "== installing Qtbase"
make install

# Downstream CMake and qmake consumers need host tools beside the target Qt
# config files. They are never executed on the board and remain visibly .exe.
for tool in qmake.exe moc.exe uic.exe rcc.exe; do
  cp "$HOST_BIN/$tool" "$PREFIX/bin/$tool"
done

for required in \
  "$PREFIX/lib/libQt5Core.so" \
  "$PREFIX/lib/libQt5Gui.so" \
  "$PREFIX/lib/libQt5Widgets.so" \
  "$PREFIX/plugins/platforms/libplugins_platforms_qoffscreen.so" \
  "$PREFIX/plugins/egldeviceintegrations/libplugins_egldeviceintegrations_qeglfs-emu-integration.so" \
  "$PREFIX/bin/qmake.exe" "$PREFIX/bin/moc.exe" "$PREFIX/bin/uic.exe" "$PREFIX/bin/rcc.exe"; do
  [ -f "$required" ] || { echo "ERROR: Qt install is incomplete: $required" >&2; exit 1; }
done

write_marker "$MARKER" "$RECIPE_FINGERPRINT"
printf 'QTBASE_RECIPE fingerprint=%s\n' "$RECIPE_FINGERPRINT"
echo "== Qtbase installed into $PREFIX"
