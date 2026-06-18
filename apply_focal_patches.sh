#!/usr/bin/env bash
# Apply Focal-host source patches needed to source-build ROS 2 Jazzy on
# Ubuntu 20.04 (see build_jazzy_focal.sh). Idempotent: safe to re-run.
set -euo pipefail
WS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

apply_one() {
  local repo="$1" patch="$2"
  if git -C "$repo" apply --check "$patch" >/dev/null 2>&1; then
    git -C "$repo" apply "$patch"
    echo "applied: $patch"
  elif git -C "$repo" apply --reverse --check "$patch" >/dev/null 2>&1; then
    echo "already applied: $patch"
  else
    echo "SKIP (does not apply cleanly): $patch" >&2
  fi
}

apply_one "$WS/src/ros2/rmw_fastrtps" \
  "$WS/patches/focal-host/0001-rmw_fastrtps_cpp-gcc12-ternary-lambda.patch"

# nav2 (navigation2) — only present when the extra repos have been imported.
if [ -d "$WS/src/ros-navigation/navigation2" ]; then
  apply_one "$WS/src/ros-navigation/navigation2" \
    "$WS/patches/focal-host/0002-nav2_common-drop-werror-gcc12.patch"
  apply_one "$WS/src/ros-navigation/navigation2" \
    "$WS/patches/focal-host/0003-nav2_route-include-filesystem.patch"
fi
