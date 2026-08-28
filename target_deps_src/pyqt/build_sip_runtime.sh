#!/usr/bin/env bash
# Cross-build the PyQt5 sip runtime (PyQt5.sip) for the OHOS board.
# Output: install_ohos/Lib/site-packages/PyQt5/sip.cpython-312-aarch64-linux-ohos.so
set -euo pipefail

SDK='C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony'
NATIVE="$SDK/native"
CC="$NATIVE/llvm/bin/clang.exe"
SYSROOT="$NATIVE/sysroot"
WS="C:/Users/17715/Documents/codes/M-DDS/ros2"
PY_INC="$WS/python_target/usr/include/python3.12"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/pyqt5_sip-12.19.0"
OUT_DIR="$WS/install_ohos/Lib/site-packages/PyQt5"

mkdir -p "$OUT_DIR" build_sip_obj
objs=()
for c in "$SRC_DIR"/*.c; do
  obj="build_sip_obj/$(basename "${c%.c}").o"
  "$CC" -target aarch64-linux-ohos --sysroot="$SYSROOT" -D__MUSL__ \
    -fPIC -O2 -I"$PY_INC" -I"$SRC_DIR" -c "$c" -o "$obj"
  objs+=("$obj")
done

"$CC" -target aarch64-linux-ohos --sysroot="$SYSROOT" -shared \
  "${objs[@]}" -o "$OUT_DIR/sip.cpython-312-aarch64-linux-ohos.so"

echo "built: $OUT_DIR/sip.cpython-312-aarch64-linux-ohos.so"
file "$OUT_DIR/sip.cpython-312-aarch64-linux-ohos.so" || true
