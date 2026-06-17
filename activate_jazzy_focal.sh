# Activate the source-built ROS 2 Jazzy (Ubuntu 20.04 Focal) for the CURRENT shell.
#
#   source activate_jazzy_focal.sh
#
# Brings up the conda runtime (python3.12 + numpy/psutil) AND the workspace
# overlay, so `ros2 run`, `ros2 node list`, `ros2 topic echo`, etc. all work.
#
# Why a conda env at runtime: Focal ships python3.8, but Jazzy needs python3.12
# (rclpy, ros2cli). The "jazzy-build" env provides it. See build_jazzy_focal.sh
# and CLAUDE-side notes for the full toolchain story.

_JAZZY_WS="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
_JAZZY_ENV="${JAZZY_BUILD_ENV:-jazzy-build}"

# Activate the conda env in-place (works in an interactive bash that has run
# `eval "$(micromamba shell hook --shell bash)"`; otherwise falls back).
if command -v micromamba >/dev/null 2>&1; then
  eval "$(micromamba shell hook --shell bash 2>/dev/null)" || true
  micromamba activate "${_JAZZY_ENV}" 2>/dev/null || {
    echo "[activate_jazzy_focal] could not 'micromamba activate ${_JAZZY_ENV}'." >&2
    echo "  Run inside:  micromamba run -n ${_JAZZY_ENV} bash" >&2
  }
fi

# Overlay the workspace install.
if [ -f "${_JAZZY_WS}/install/setup.bash" ]; then
  source "${_JAZZY_WS}/install/setup.bash"
  echo "[activate_jazzy_focal] ROS 2 ${ROS_DISTRO} ready (source build, env=${_JAZZY_ENV})"
  echo "  try:  ros2 run demo_nodes_cpp talker"
else
  echo "[activate_jazzy_focal] ${_JAZZY_WS}/install/setup.bash not found — build first:" >&2
  echo "  ./build_jazzy_focal.sh demo_nodes_cpp demo_nodes_py rclpy ros2cli ros2run ros2node ros2topic" >&2
fi
