#!/usr/bin/env bash
# Stage the hash-bound CPython 3.12 build interface used by the ROS cross build.
set -euo pipefail
cd "$(dirname "$0")/.."

usage() {
  cat <<'EOF'
Usage:
  ./scripts/pull_python_target.sh [board_serial]
  ./scripts/pull_python_target.sh --runtime-usr PATH
  ./scripts/pull_python_target.sh --verify-only

Board mode reads the already deployed /data Python runtime. Local mode consumes
the `usr` directory produced by the pinned Jiusi-pys/python harness. In both
cases all critical files, the complete header tree, CPython version, SOABI and
AArch64 ELF identity must match scripts/python/ohos_python.lock.json before the
current python_target/usr directory is replaced.
EOF
}

MODE=board
SOURCE_USR=""
BOARD=""
case "${1:-}" in
  --runtime-usr)
    [ "$#" -eq 2 ] || { usage >&2; exit 2; }
    MODE=local
    SOURCE_USR="$2"
    ;;
  --verify-only)
    [ "$#" -eq 1 ] || { usage >&2; exit 2; }
    MODE=verify
    ;;
  --help|-h)
    usage
    exit 0
    ;;
  --*)
    echo "unknown option: $1" >&2
    usage >&2
    exit 2
    ;;
  *)
    [ "$#" -le 1 ] || { usage >&2; exit 2; }
    BOARD="${1:-3e01ff55454d202020104033bf453b00}"
    ;;
esac

PYTHON="${HOST_PYTHON:-$(pwd)/.pixi/envs/default/python.exe}"
if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3 || true)"
fi
[ -n "$PYTHON" ] && [ -x "$PYTHON" ] || { echo "host Python 3 is required" >&2; exit 1; }

LOCK="${PYTHON_TARGET_LOCK:-$(pwd)/scripts/python/ohos_python.lock.json}"
MANAGER="$(pwd)/scripts/python_target.py"
DEST="${PYTHON_TARGET_ROOT:-$(pwd)/python_target/usr}"

if [ "$MODE" = verify ]; then
  "$PYTHON" "$MANAGER" --lock "$LOCK" verify-runtime --root "$DEST"
  exit 0
fi

PARENT="$(dirname "$DEST")"
mkdir -p "$PARENT"
STAGING="$(mktemp -d "$PARENT/.python-usr.stage.XXXXXX")"
BACKUP="$PARENT/.python-usr.previous.$$"
cleanup() {
  rm -rf "$STAGING"
  if [ -d "$BACKUP" ] && [ ! -e "$DEST" ]; then
    mv "$BACKUP" "$DEST"
  fi
}
trap cleanup EXIT
mkdir -p \
  "$STAGING/include" \
  "$STAGING/lib/config-3.12-aarch64-linux-ohos"

if [ "$MODE" = local ]; then
  [ -d "$SOURCE_USR" ] || { echo "runtime usr directory does not exist: $SOURCE_USR" >&2; exit 1; }
  cp -R "$SOURCE_USR/include/python3.12" "$STAGING/include/python3.12"
  cp -f "$SOURCE_USR/lib/libpython3.12.so.1.0" "$STAGING/lib/libpython3.12.so.1.0"
  # DrvFS exposes Linux symlinks through \\wsl.localhost as reparse points that
  # MSYS cp cannot always follow. Prefer the locked libffi 3.6.0 payload and
  # materialize the SONAME file used by the Windows-hosted cross build.
  if [ -f "$SOURCE_USR/lib/libffi.so.8.3.1" ]; then
    cp -f "$SOURCE_USR/lib/libffi.so.8.3.1" "$STAGING/lib/libffi.so.8"
  else
    cp -f "$SOURCE_USR/lib/libffi.so.8" "$STAGING/lib/libffi.so.8"
  fi
  if [ -f "$SOURCE_USR/lib/_sysconfigdata__linux_aarch64-linux-ohos.py" ]; then
    cp -f "$SOURCE_USR/lib/_sysconfigdata__linux_aarch64-linux-ohos.py" "$STAGING/lib/"
  else
    cp -f "$SOURCE_USR/lib/python3.12/_sysconfigdata__linux_aarch64-linux-ohos.py" "$STAGING/lib/"
  fi
  if [ -f "$SOURCE_USR/lib/config-3.12-aarch64-linux-ohos/Makefile" ]; then
    cp -f "$SOURCE_USR/lib/config-3.12-aarch64-linux-ohos/Makefile" \
      "$STAGING/lib/config-3.12-aarch64-linux-ohos/Makefile"
  else
    cp -f "$SOURCE_USR/lib/python3.12/config-3.12-aarch64-linux-ohos/Makefile" \
      "$STAGING/lib/config-3.12-aarch64-linux-ohos/Makefile"
  fi
  ORIGIN="local-runtime:$SOURCE_USR"
else
  [[ "$BOARD" =~ ^[A-Za-z0-9._:-]+$ ]] || { echo "unsafe board serial: $BOARD" >&2; exit 2; }
  HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
  [ -x "$HDC" ] || { echo "HDC is not executable: $HDC" >&2; exit 1; }
  REMOTE="${PYTHON_REMOTE_PREFIX:-/data/python312-rk3588a}/usr"
  case "$REMOTE" in
    /data/*/usr) ;;
    *) echo "refusing unsafe remote Python usr prefix: $REMOTE" >&2; exit 2 ;;
  esac
  export MSYS2_ARG_CONV_EXCL='*'
  WSTAGING="$(cygpath -w "$STAGING")"
  "$HDC" -t "$BOARD" file recv "$REMOTE/include/python3.12" "$WSTAGING\\include\\python3.12"
  "$HDC" -t "$BOARD" file recv "$REMOTE/lib/libpython3.12.so.1.0" \
    "$WSTAGING\\lib\\libpython3.12.so.1.0"
  "$HDC" -t "$BOARD" file recv "$REMOTE/lib/libffi.so.8" "$WSTAGING\\lib\\libffi.so.8"
  "$HDC" -t "$BOARD" file recv \
    "$REMOTE/lib/python3.12/_sysconfigdata__linux_aarch64-linux-ohos.py" \
    "$WSTAGING\\lib\\_sysconfigdata__linux_aarch64-linux-ohos.py"
  "$HDC" -t "$BOARD" file recv \
    "$REMOTE/lib/python3.12/config-3.12-aarch64-linux-ohos/Makefile" \
    "$WSTAGING\\lib\\config-3.12-aarch64-linux-ohos\\Makefile"
  ORIGIN="board:$BOARD:$REMOTE"
fi

cp -f "$STAGING/lib/libpython3.12.so.1.0" "$STAGING/lib/libpython3.12.so"
"$PYTHON" "$MANAGER" --lock "$LOCK" verify-runtime --root "$STAGING" \
  --write-provenance --origin "$ORIGIN" >/dev/null

if [ -e "$BACKUP" ]; then
  echo "refusing existing backup path: $BACKUP" >&2
  exit 1
fi
if [ -e "$DEST" ]; then
  mv "$DEST" "$BACKUP"
fi
mv "$STAGING" "$DEST"
STAGING="$PARENT/.stage-installed.$$"
rm -rf "$BACKUP"

"$PYTHON" "$MANAGER" --lock "$LOCK" verify-runtime --root "$DEST"
echo "python_target staged and hash-bound at $DEST"
