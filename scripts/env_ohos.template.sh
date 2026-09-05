# Generated deployment environment for KaihongOS/RK3588A.
# deploy_ohos.sh prepends MDDS_DEPLOY_EXPECTED_MARKER to this template.

export ROS2_HOME="${ROS2_HOME:-/data/local/tmp/ros2}"
if [ -z "${MDDS_DEPLOY_EXPECTED_MARKER:-}" ] || \
   [ ! -f "$ROS2_HOME/.mdds_deploy_complete" ] || \
   [ -L "$ROS2_HOME/.mdds_deploy_complete" ] || \
   [ "$(cat "$ROS2_HOME/.mdds_deploy_complete" 2>/dev/null)" != "$MDDS_DEPLOY_EXPECTED_MARKER" ]; then
  echo "ERROR: incomplete or mixed ROS 2 deployment at $ROS2_HOME" >&2
  return 70 2>/dev/null || exit 70
fi

export MDDS_TOKEN_EXEC="$ROS2_HOME/bin/mdds_token_exec"
if [ ! -x "$MDDS_TOKEN_EXEC" ] || [ -L "$MDDS_TOKEN_EXEC" ]; then
  echo "ERROR: required per-process MDDS token launcher is missing: $MDDS_TOKEN_EXEC" >&2
  return 70 2>/dev/null || exit 70
fi

export LD_LIBRARY_PATH="$ROS2_HOME/lib:$ROS2_HOME/Lib:${LD_LIBRARY_PATH:-}"
export AMENT_PREFIX_PATH="$ROS2_HOME"
export RCUTILS_COLORIZED_OUTPUT=0
export RCUTILS_CONSOLE_OUTPUT_FORMAT="[{severity}] [{name}]: {message}"
export HOME="$ROS2_HOME"
export ROS_LOG_DIR="$ROS2_HOME/log"
# Put this deployment first.  Board scripts use the explicit wrappers below;
# they never fall through to a stale /usr/local/bin/ros2 overlay.
export PATH="$ROS2_HOME/bin:$PATH"
export ROS2_CLI_WRAPPER="$ROS2_HOME/env.sh:ros2"

export LTTNG_SESSION_CONFIG_XSD_PATH="$ROS2_HOME/share/xml/lttng"
export LTTNG_CONSUMERD64_BIN="$ROS2_HOME/lib/lttng/libexec/lttng-consumerd"
export LTTNG_CONSUMERD64_LIBDIR="$ROS2_HOME/lib"
if [ -x "$ROS2_HOME/bin/lttng-sessiond" ] && ! "$ROS2_HOME/bin/lttng" list >/dev/null 2>&1; then
  setsid "$ROS2_HOME/bin/lttng-sessiond" --daemonize </dev/null >/dev/null 2>&1 || true
fi

export FASTDDS_BUILTIN_TRANSPORTS=UDPv4
if ! grep -q " /dev/shm " /proc/mounts 2>/dev/null; then
  mkdir -p /dev/shm 2>/dev/null
  mount -t tmpfs tmpfs /dev/shm 2>/dev/null || true
fi

export QT_PLUGIN_PATH="$ROS2_HOME/plugins"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

# Only the launched child receives the native AccessToken.  libmdds itself
# never mutates the identity of a composable ROS process.
mdds_exec() {
  "$MDDS_TOKEN_EXEC" -- "$@"
}

export ROS2_TALKER_RAW="$ROS2_HOME/Lib/demo_nodes_cpp/talker"
export ROS2_LISTENER_RAW="$ROS2_HOME/Lib/demo_nodes_cpp/listener"
export ROS2_TALKER="$ROS2_TALKER_RAW"
export ROS2_LISTENER="$ROS2_LISTENER_RAW"

export ROS2_PY_TALKER_RAW="$ROS2_HOME/Lib/demo_nodes_py/talker-script.py"
export ROS2_PY_LISTENER_RAW="$ROS2_HOME/Lib/demo_nodes_py/listener-script.py"
export ROS2_PY_TALKER="python3.12 $ROS2_PY_TALKER_RAW"
export ROS2_PY_LISTENER="python3.12 $ROS2_PY_LISTENER_RAW"

PY312=/data/python312-rk3588a/usr
if [ -x "$PY312/bin/python3.12" ]; then
  PATH="$PY312/bin:$PATH"
  LD_LIBRARY_PATH="$PY312/lib:$LD_LIBRARY_PATH"
  export PATH LD_LIBRARY_PATH
  export LD_PRELOAD="$PY312/lib/libpython3.12.so.1.0${LD_PRELOAD:+:$LD_PRELOAD}"
  export PYTHONPATH="$ROS2_HOME/Lib/site-packages:$PY312/lib/python3.12/site-packages${PYTHONPATH:+:$PYTHONPATH}"
fi

# Remove inherited definitions before installing the deployment-owned wrapper.
unalias ros2 2>/dev/null || true
unset -f ros2 2>/dev/null || true
ros2() {
  if [ "${ROS2_CLI_WRAPPER:-}" != "$ROS2_HOME/env.sh:ros2" ]; then
    echo "ERROR: ROS 2 CLI overlay provenance is not this deployment" >&2
    return 70
  fi
  "$MDDS_TOKEN_EXEC" -- python3.12 -c \
    'import sys; from ros2cli.cli import main; sys.exit(main())' "$@"
}

rqt() {
  LD_PRELOAD="$ROS2_HOME/Lib/site-packages/qt_gui_cpp/libqt_gui_cpp_sip.so${LD_PRELOAD:+:$LD_PRELOAD}" \
    "$MDDS_TOKEN_EXEC" -- python3.12 -c '
import sys, os
from rqt_gui.main import main
try:
    rc = main()
except SystemExit as e:
    rc = e.code if isinstance(e.code, int) else 0
sys.stdout.flush()
sys.stderr.flush()
os._exit(int(rc or 0))
' "$@"
}
