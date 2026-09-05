#!/usr/bin/env bash
# Fetch the exact public Python harness, wheels and source archives in the lock.
set -euo pipefail
cd "$(dirname "$0")/.."

PYTHON="${HOST_PYTHON:-$(pwd)/.pixi/envs/default/python.exe}"
if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3 || true)"
fi
[ -n "$PYTHON" ] && [ -x "$PYTHON" ] || { echo "host Python 3 is required" >&2; exit 1; }

LOCK="${PYTHON_TARGET_LOCK:-$(pwd)/scripts/python/ohos_python.lock.json}"
MANAGER="$(pwd)/scripts/python_target.py"
CLEAN=()
if [ "${1:-}" = "--clean" ]; then
  CLEAN=(--clean)
  shift
fi
[ "$#" -eq 0 ] || { echo "usage: $0 [--clean]" >&2; exit 2; }

"$PYTHON" "$MANAGER" --lock "$LOCK" checkout-harness
"$PYTHON" "$MANAGER" --lock "$LOCK" fetch-artifacts "${CLEAN[@]}"
echo "python_target_inputs=verified"
