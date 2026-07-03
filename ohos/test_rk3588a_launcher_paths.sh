#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LAUNCHER="${1:-${ROOT_DIR}/install/ohos-colcon-rk3588a/bin/ros2}"

if [[ ! -f "${LAUNCHER}" ]]; then
  echo "launcher not found: ${LAUNCHER}" >&2
  exit 1
fi

required_paths=(
  "/system/lib64/platformsdk"
  "/system/lib64/chipset-pub-sdk"
  "/system/lib64"
)

for path in "${required_paths[@]}"; do
  if ! grep -qF "${path}" "${LAUNCHER}"; then
    echo "missing runtime library path in ${LAUNCHER}: ${path}" >&2
    exit 1
  fi
done

echo "launcher runtime library paths ok: ${LAUNCHER}"
