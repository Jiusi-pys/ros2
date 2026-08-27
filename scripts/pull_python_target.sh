#!/usr/bin/env bash
# Pull the target-side CPython 3.12 development files (headers + libpython)
# from a board that already has the python312-rk3588a runtime deployed
# (https://github.com/Jiusi-pys/python, /data/python312-rk3588a).
# They land in python_target/usr/ and are consumed by scripts/build_ohos.sh
# as Python3_INCLUDE_DIR / Python3_LIBRARY for the cross build.
#
# Usage: ./scripts/pull_python_target.sh [board_serial]
set -euo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD="${1:-3e01ff55454d202020104033bf453b00}"
REMOTE=/data/python312-rk3588a/usr
DEST=python_target/usr
mkdir -p "$DEST/lib" "$DEST/include"

export MSYS2_ARG_CONV_EXCL='*'
WDEST="$(cygpath -w "$(pwd)/$DEST")"

"$HDC" -t "$BOARD" file recv "$REMOTE/include/python3.12" "$WDEST\\include\\python3.12"
"$HDC" -t "$BOARD" file recv "$REMOTE/lib/libpython3.12.so.1.0" "$WDEST\\lib\\libpython3.12.so.1.0"
"$HDC" -t "$BOARD" file recv "$REMOTE/lib/libffi.so.8" "$WDEST\\lib\\libffi.so.8"
"$HDC" -t "$BOARD" file recv "$REMOTE/lib/python3.12/_sysconfigdata__linux_aarch64-linux-ohos.py" \
  "$WDEST\\lib\\_sysconfigdata__linux_aarch64-linux-ohos.py" || true

# Windows has no symlink support here; copy as the dev link name.
cp -f "$DEST/lib/libpython3.12.so.1.0" "$DEST/lib/libpython3.12.so"
echo "python_target staged at $DEST"
