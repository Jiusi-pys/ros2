#!/usr/bin/env bash
# Create a hash-bound, deterministic runtime release artifact from one board.
set -euo pipefail
cd "$(dirname "$0")/.."

usage() {
  echo "usage: ./scripts/create_python_runtime_artifact.sh [board_serial] [output.tar.gz]" >&2
}

[ "$#" -le 2 ] || { usage; exit 2; }
BOARD="${1:-3e01ff55454d202020104033bf453b00}"
OUTPUT="${2:-$(pwd)/python_target/runtime-artifacts/python312-rk3588a-3.12.7.tar.gz}"
[[ "$BOARD" =~ ^[A-Za-z0-9._:-]+$ ]] || { echo "unsafe board serial: $BOARD" >&2; exit 2; }

PY_REMOTE_PREFIX="${PYTHON_REMOTE_PREFIX:-/data/python312-rk3588a}"
[[ "$PY_REMOTE_PREFIX" =~ ^/data/[A-Za-z0-9._+-]+$ ]] || {
  echo "refusing unsafe source runtime prefix: $PY_REMOTE_PREFIX" >&2
  exit 2
}
REMOTE_USR="$PY_REMOTE_PREFIX/usr"

PYTHON="${HOST_PYTHON:-$(pwd)/.pixi/envs/default/python.exe}"
if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3 || true)"
fi
[ -n "$PYTHON" ] && [ -x "$PYTHON" ] || { echo "host Python 3 is required" >&2; exit 1; }
LOCK="${PYTHON_TARGET_LOCK:-$(pwd)/scripts/python/ohos_python.lock.json}"
TOOL="$(pwd)/scripts/python_runtime_artifact.py"
HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
[ -x "$HDC" ] || { echo "HDC is not executable: $HDC" >&2; exit 1; }

case "$OUTPUT" in
  [A-Za-z]:*) OUTPUT="$(cygpath -u "$OUTPUT")" ;;
  /*) ;;
  *) OUTPUT="$(pwd)/$OUTPUT" ;;
esac
MANIFEST="$OUTPUT.manifest.json"
[ ! -e "$OUTPUT" ] && [ ! -e "$MANIFEST" ] || {
  echo "create-only artifact already exists: $OUTPUT or $MANIFEST" >&2
  exit 1
}
mkdir -p "$(dirname "$OUTPUT")"

RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)-$$"
REMOTE_TOOL="/data/local/tmp/python-runtime-artifact-$RUN_ID.py"
REMOTE_LOCK="/data/local/tmp/python-runtime-lock-$RUN_ID.json"
REMOTE_ARCHIVE="/data/local/tmp/python-runtime-$RUN_ID.tar.gz"
LOCAL_PART="$OUTPUT.part.$RUN_ID"
COMPLETE=0

cleanup() {
  MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$BOARD" shell \
    "rm -f '$REMOTE_TOOL' '$REMOTE_LOCK' '$REMOTE_ARCHIVE'" >/dev/null 2>&1 || true
  if [ "$COMPLETE" -ne 1 ]; then
    rm -f "$LOCAL_PART" "$OUTPUT" "$MANIFEST"
  fi
}
trap cleanup EXIT

LOCAL_TOOL_WIN="$(cygpath -w "$TOOL")"
MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$BOARD" file send \
  "$LOCAL_TOOL_WIN" "$REMOTE_TOOL" >/dev/null
MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$BOARD" file send \
  "$(cygpath -w "$LOCK")" "$REMOTE_LOCK" >/dev/null

PY_BIN="$REMOTE_USR/bin/python3.12"
PACK_COMMAND="PYTHONDONTWRITEBYTECODE=1 LD_LIBRARY_PATH='$REMOTE_USR/lib' LD_PRELOAD='$REMOTE_USR/lib/libpython3.12.so.1.0' '$PY_BIN' '$REMOTE_TOOL' --lock '$REMOTE_LOCK' pack --runtime-usr '$REMOTE_USR' --output '$REMOTE_ARCHIVE' && echo PYTHON_RUNTIME_PACK_OK"
PACK_OUTPUT="$(MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$BOARD" shell "$PACK_COMMAND" 2>&1 | tr -d '\r')"
printf '%s\n' "$PACK_OUTPUT"
grep -Fq PYTHON_RUNTIME_PACK_OK <<<"$PACK_OUTPUT" || {
  echo "board did not complete deterministic runtime packing" >&2
  exit 1
}

REMOTE_SHA_LINE="$(MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$BOARD" shell \
  "sha256sum '$REMOTE_ARCHIVE'" | tr -d '\r\n')"
REMOTE_SHA="${REMOTE_SHA_LINE%%[[:space:]]*}"
[[ "$REMOTE_SHA" =~ ^[0-9a-f]{64}$ ]] || { echo "invalid remote artifact hash: $REMOTE_SHA" >&2; exit 1; }
LOCAL_PART_WIN="$(cygpath -w "$LOCAL_PART")"
MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$BOARD" file recv "$REMOTE_ARCHIVE" "$LOCAL_PART_WIN" >/dev/null
LOCAL_SHA="$("$PYTHON" -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$LOCAL_PART" | tr -d '\r')"
[ "$LOCAL_SHA" = "$REMOTE_SHA" ] || {
  echo "runtime artifact changed in transfer: board=$REMOTE_SHA host=$LOCAL_SHA" >&2
  exit 1
}

mv "$LOCAL_PART" "$OUTPUT"
"$PYTHON" "$TOOL" --lock "$LOCK" seal --archive "$OUTPUT" \
  --origin "board:$BOARD:$REMOTE_USR" >/dev/null
"$PYTHON" "$TOOL" --lock "$LOCK" verify --archive "$OUTPUT" --manifest "$MANIFEST"
COMPLETE=1
echo "python_runtime_artifact=$OUTPUT"
echo "python_runtime_artifact_manifest=$MANIFEST"
