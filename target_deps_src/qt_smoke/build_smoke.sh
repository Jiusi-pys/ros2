#!/usr/bin/env bash
# Cross-compile the minimal Qt5 smoke test for the OHOS board.
# Run from the workspace root after qtbase `make install` into install_ohos/.
set -euo pipefail

WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="$WORKSPACE_ROOT/target_deps_src"
NATIVE="${OHOS_NATIVE_SDK:-}"
[ -n "$NATIVE" ] || { echo "ERROR: set OHOS_NATIVE_SDK" >&2; exit 2; }
CXX="$NATIVE/llvm/bin/clang++.exe"
SYSROOT="$NATIVE/sysroot"
PREFIX="$WORKSPACE_ROOT/install_ohos"
SMOKE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../lib/locked_sources.sh
source "$SRC_DIR/lib/locked_sources.sh"

SDK_FINGERPRINT="$(sdk_fingerprint "$NATIVE")"
QT_MARKER="$SRC_DIR/qtbase-everywhere-src-5.15.8/build-ohos/.install-done"
[ -f "$QT_MARKER" ] || {
  echo "ERROR: Qtbase recipe marker missing; run build_qtbase_ohos.sh" >&2
  exit 2
}
RECIPE_FINGERPRINT="$(recipe_fingerprint \
  "$(sha256_file "$QT_MARKER")" "$(sha256_file "$SMOKE_DIR/qt_smoke.cpp")" \
  "$(sha256_file "$0")" "$SDK_FINGERPRINT")"
MARKER="$SMOKE_DIR/.qt-smoke.recipe.sha256"
if marker_matches "$MARKER" "$RECIPE_FINGERPRINT" && [ -f "$SMOKE_DIR/qt_smoke" ]; then
  printf 'QT_SMOKE_RECIPE fingerprint=%s state=already-built\n' "$RECIPE_FINGERPRINT"
  exit 0
fi

"$CXX" -target aarch64-linux-ohos --sysroot="$SYSROOT" -D__MUSL__ \
  -fPIC -O2 \
  -I"$PREFIX/include" \
  -I"$PREFIX/include/QtCore" \
  -I"$PREFIX/include/QtGui" \
  -I"$PREFIX/include/QtWidgets" \
  "$SMOKE_DIR/qt_smoke.cpp" -o "$SMOKE_DIR/qt_smoke" \
  -L"$PREFIX/lib" -lQt5Widgets -lQt5Gui -lQt5Core \
  -Wl,--export-dynamic -Wl,-rpath,/data/local/tmp/ros2/lib

write_marker "$MARKER" "$RECIPE_FINGERPRINT"
printf 'QT_SMOKE_RECIPE fingerprint=%s state=built\n' "$RECIPE_FINGERPRINT"
file "$SMOKE_DIR/qt_smoke" || true
