#!/usr/bin/env bash
# Cross-build the PyQt5 sip runtime (PyQt5.sip) for the OHOS board.
# Output: install_ohos/Lib/site-packages/PyQt5/sip.cpython-312-aarch64-linux-ohos.so
set -euo pipefail

WS_UNIX="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="$WS_UNIX/target_deps_src"
# shellcheck source=../lib/locked_sources.sh
source "$SRC_DIR/lib/locked_sources.sh"

NATIVE="${OHOS_NATIVE_SDK:-}"
[ -n "$NATIVE" ] || { echo "ERROR: set OHOS_NATIVE_SDK" >&2; exit 2; }
SDK_FINGERPRINT="$(sdk_fingerprint "$NATIVE")"
CC="$NATIVE/llvm/bin/clang.exe"
SYSROOT="$NATIVE/sysroot"
WS="$(cygpath -m "$WS_UNIX")"
PY_INC="$WS/python_target/usr/include/python3.12"
SIP_SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/pyqt5_sip-12.19.0"
OUT_DIR="$WS/install_ohos/Lib/site-packages/PyQt5"
OBJ_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build_sip_obj"
OUT_FILE="$OUT_DIR/sip.cpython-312-aarch64-linux-ohos.so"

"$SRC_DIR/bootstrap_qt_host_tools.sh"
[ -f "$PY_INC/Python.h" ] || { echo "ERROR: target Python.h is missing" >&2; exit 2; }
RECIPE_FINGERPRINT="$(recipe_fingerprint \
  "$(lock_field pyqt5_sip-12.19.0.tar.gz 3)" "$(sha256_file "$PY_INC/Python.h")" \
  "$SDK_FINGERPRINT" "$(sha256_file "$0")")"
MARKER="$OBJ_DIR/.install-done"
if marker_matches "$MARKER" "$RECIPE_FINGERPRINT" && [ -f "$OUT_FILE" ]; then
  echo "PyQt5.sip already installed, nothing to do"
  exit 0
fi

mkdir -p "$OUT_DIR" "$OBJ_DIR"
objs=()
for c in "$SIP_SRC"/*.c; do
  obj="$OBJ_DIR/$(basename "${c%.c}").o"
  "$CC" -target aarch64-linux-ohos --sysroot="$SYSROOT" -D__MUSL__ \
    -fPIC -O2 -I"$PY_INC" -I"$SIP_SRC" -c "$c" -o "$obj"
  objs+=("$obj")
done

"$CC" -target aarch64-linux-ohos --sysroot="$SYSROOT" -shared \
  "${objs[@]}" -o "$OUT_FILE"

write_marker "$MARKER" "$RECIPE_FINGERPRINT"
echo "built: $OUT_FILE"
file "$OUT_FILE" || true
