#!/usr/bin/env bash
# Build python_target/sitepkgs from the exact locked target artifacts.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "$#" -ne 0 ]; then
  echo "usage: ./scripts/stage_python_wheels.sh" >&2
  echo "individual wheel arguments are forbidden; update the checked-in lock instead" >&2
  exit 2
fi

PYTHON="${HOST_PYTHON:-$(pwd)/.pixi/envs/default/python.exe}"
if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3 || true)"
fi
[ -n "$PYTHON" ] && [ -x "$PYTHON" ] || { echo "host Python 3 is required" >&2; exit 1; }

LOCK="${PYTHON_TARGET_LOCK:-$(pwd)/scripts/python/ohos_python.lock.json}"
MANAGER="$(pwd)/scripts/python_target.py"
SITE_PKGS="${PYTHON_SITEPKGS_DIR:-$(pwd)/python_target/sitepkgs}"
SITE_PARENT="$(dirname "$SITE_PKGS")"
mkdir -p "$SITE_PARENT"

STAGING="$(mktemp -d "$SITE_PARENT/.sitepkgs.stage.XXXXXX")"
BACKUP="$SITE_PARENT/.sitepkgs.previous.$$"
cleanup() {
  rm -rf "$STAGING"
  if [ -d "$BACKUP" ] && [ ! -e "$SITE_PKGS" ]; then
    mv "$BACKUP" "$SITE_PKGS"
  fi
}
trap cleanup EXIT

"$PYTHON" "$MANAGER" --lock "$LOCK" unpack-stage --output "$STAGING"
PYTHON_TARGET_LOCK="$LOCK" \
PYTHON_TARGET_ROOT="${PYTHON_TARGET_ROOT:-$(pwd)/python_target/usr}" \
  ./scripts/build_psutil_ohos.sh --output "$STAGING"
"$PYTHON" "$MANAGER" --lock "$LOCK" finalize-stage --site "$STAGING"

if [ -e "$BACKUP" ]; then
  echo "refusing existing backup path: $BACKUP" >&2
  exit 1
fi
if [ -e "$SITE_PKGS" ]; then
  mv "$SITE_PKGS" "$BACKUP"
fi
mv "$STAGING" "$SITE_PKGS"
STAGING="$SITE_PARENT/.stage-installed.$$"
rm -rf "$BACKUP"

"$PYTHON" "$MANAGER" --lock "$LOCK" verify-stage --site "$SITE_PKGS"
echo "target_numpy_include=$SITE_PKGS/numpy/core/include"
echo "python target packages staged at $SITE_PKGS"
