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

# slam_toolbox (Focal headless: drop rviz/Qt5 plugin, G2O/CSparse/CHOLMOD, Boost 1.90 system)
if [ -d "$WS/src/SteveMacenski/slam_toolbox" ]; then
  apply_one "$WS/src/SteveMacenski/slam_toolbox" \
    "$WS/patches/focal-host/0004-slam_toolbox-focal-headless-eigen-boost.patch"
fi

# moveit2 (Focal: Boost 1.90 system, octomap 1.10, drop rviz exec/test deps)
if [ -d "$WS/src/moveit/moveit2" ]; then
  apply_one "$WS/src/moveit/moveit2" \
    "$WS/patches/focal-host/0005-moveit2-focal-boost-octomap-rviz.patch"
fi
