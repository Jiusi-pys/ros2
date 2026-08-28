#!/usr/bin/env bash
# Deploy the OHOS cross-built install tree to both RK3588 boards.
# Run from the ros2/ workspace root inside Git Bash:
#   ./scripts/deploy_ohos.sh [board ...]
# Default boards: the two connected RK3588 devices.
set -euo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-/c/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
# RMW to preselect on the boards (optional):
#   RMW=rmw_fastrtps_cpp ./scripts/deploy_ohos.sh
# When RMW is empty, env.sh does not set RMW_IMPLEMENTATION at all - both
# rmw_cyclonedds_cpp and rmw_fastrtps_cpp are available and the RMW can be
# switched freely at runtime via the RMW_IMPLEMENTATION environment variable.
RMW="${RMW:-}"
BOARDS=("$@")
if [ ${#BOARDS[@]} -eq 0 ]; then
  BOARDS=(3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00)
fi
DEVICE_DIR=/data/local/tmp/ros2
ARCHIVE="$(pwd)/ros2_ohos_install.tar.gz"

if [ ! -d install_ohos ]; then
  echo "install_ohos/ not found - run scripts/build_ohos.sh first" >&2
  exit 1
fi

# hdc is a native Windows binary: give it backslash paths, MSYS /tmp paths get
# mangled. MSYS2_ARG_CONV_EXCL stops Git Bash from rewriting the *remote*
# /data/... path into a Windows path.
hdc_send() { # hdc_send <board> <local> <remote>
  MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$1" file send "$(cygpath -w "$2")" "$3"
}

echo "== flattening vendor libs into lib/ =="
for vendordir in install_ohos/opt/*_vendor/lib; do
  [ -d "$vendordir" ] || continue
  cp -a "$vendordir"/lib*.so* install_ohos/lib/ 2>/dev/null || true
done

# The OHOS NDK links C++ binaries against the shared libc++; ship the runtime.
OHOS_NATIVE="${OHOS_NATIVE_SDK:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/native}"
cp -f "$OHOS_NATIVE/llvm/lib/aarch64-linux-ohos/libc++_shared.so" install_ohos/lib/ 2>/dev/null || true

# PyKDL (python_orocos_kdl_vendor) installs as a bare PyKDL.so into
# Lib/python3.12/dist-packages, which the board interpreter cannot find: it
# needs the OHOS SOABI suffix and the Lib/site-packages dir on PYTHONPATH.
if [ -f install_ohos/Lib/python3.12/dist-packages/PyKDL.so ]; then
  mv install_ohos/Lib/python3.12/dist-packages/PyKDL.so \
     install_ohos/Lib/site-packages/PyKDL.cpython-312-aarch64-linux-ohos.so
fi

# Strip CRLF from ament-index resource files. They are written by the Windows
# host build with \r\n, and pluginlib's get_resource() does not strip the \r,
# which corrupts the plugin.xml path and fails with "has no Root Element".
find install_ohos/share/ament_index -type f -exec sed -i 's/\r$//' {} +

echo "== packing install_ohos =="
tar -C install_ohos -czf "$ARCHIVE" .
local_size=$(stat -c %s "$ARCHIVE")
echo "   archive: $local_size bytes"

# env.sh is written locally and pushed: `hdc shell` does not forward host stdin.
ENV_FILE="$(pwd)/scripts/env.sh"
cat > "$ENV_FILE" <<EOF
export ROS2_HOME=/data/local/tmp/ros2
export LD_LIBRARY_PATH=\$ROS2_HOME/lib:\$ROS2_HOME/Lib:\$LD_LIBRARY_PATH
export AMENT_PREFIX_PATH=\$ROS2_HOME
export RCUTILS_COLORIZED_OUTPUT=0
export RCUTILS_CONSOLE_OUTPUT_FORMAT="[{severity}] [{name}]: {message}"
export HOME=\$ROS2_HOME
export ROS_LOG_DIR=\$ROS2_HOME/log
# lttng tools / iox-roudi live in bin/ (Windows-style layout from the cross build)
export PATH=\$ROS2_HOME/bin:\$PATH
# lttng-tools bakes the build-host share/ path into the binaries; point the
# session config XSD lookup at the on-board copy instead.
export LTTNG_SESSION_CONFIG_XSD_PATH=\$ROS2_HOME/share/xml/lttng
# The consumerd path is baked in too; override it, then start a session
# daemon if none is running (the lttng CLI's auto-spawn uses the baked
# build-host path and cannot start one by itself).
export LTTNG_CONSUMERD64_BIN=\$ROS2_HOME/lib/lttng/libexec/lttng-consumerd
export LTTNG_CONSUMERD64_LIBDIR=\$ROS2_HOME/lib
if [ -x "\$ROS2_HOME/bin/lttng-sessiond" ] && ! \$ROS2_HOME/bin/lttng list >/dev/null 2>&1; then
  setsid \$ROS2_HOME/bin/lttng-sessiond --daemonize </dev/null >/dev/null 2>&1 || true
fi
# OHOS has no /dev/shm: restrict Fast-DDS to UDP (avoids SHM transport errors)
export FASTDDS_BUILTIN_TRANSPORTS=UDPv4
# iceoryx (zero-copy via CycloneDDS SHM) needs POSIX shm: mount a tmpfs over
# /dev/shm, then start the RouDi daemon with: iox-roudi &
if ! grep -q " /dev/shm " /proc/mounts 2>/dev/null; then
  mkdir -p /dev/shm 2>/dev/null
  mount -t tmpfs tmpfs /dev/shm 2>/dev/null || true
fi
# Qt5 (cross-built qtbase lives in the same prefix): the prefix baked into
# QtCore is the Windows build path, so plugin lookup must be redirected.
# Default to the offscreen QPA - the board has no display server; override
# QT_QPA_PLATFORM (e.g. vnc) when needed.
export QT_PLUGIN_PATH=\$ROS2_HOME/plugins
export QT_QPA_PLATFORM=\${QT_QPA_PLATFORM:-offscreen}
# demo executables (Windows-style layout from the cross build)
export ROS2_TALKER=\$ROS2_HOME/Lib/demo_nodes_cpp/talker
export ROS2_LISTENER=\$ROS2_HOME/Lib/demo_nodes_cpp/listener
# python demos: colcon on a Windows host writes setuptools *-script.py entry
# scripts into lib/<pkg>/ (on the board that is Lib/<pkg>/); the .exe
# launchers next to them are unusable, so invoke python3.12 explicitly.
export ROS2_PY_TALKER="python3.12 \$ROS2_HOME/Lib/demo_nodes_py/talker-script.py"
export ROS2_PY_LISTENER="python3.12 \$ROS2_HOME/Lib/demo_nodes_py/listener-script.py"

# --- Python stack (rclpy / ros2cli / demo_nodes_py) --------------------------
# CPython 3.12 runtime from https://github.com/Jiusi-pys/python
PY312=/data/python312-rk3588a/usr
if [ -x "\$PY312/bin/python3.12" ]; then
  PATH="\$PY312/bin:\$PATH"
  LD_LIBRARY_PATH="\$PY312/lib:\$LD_LIBRARY_PATH"
  # The python312 launcher dlopen()s libpython with RTLD_LOCAL, so extension
  # modules that do not link libpython (musllinux numpy/pyyaml wheels, ROS 2
  # typesupport .so) cannot resolve Py* symbols. Preload it globally.
  export LD_PRELOAD="\$PY312/lib/libpython3.12.so.1.0\${LD_PRELOAD:+:\$LD_PRELOAD}"
  # ROS 2 python packages installed by colcon on a Windows host land in
  # Lib/site-packages; third-party deps live in the python312 site-packages.
  export PYTHONPATH="\$ROS2_HOME/Lib/site-packages:\$PY312/lib/python3.12/site-packages\${PYTHONPATH:+:\$PYTHONPATH}"
fi
# ros2 entry-point wrapper (the .exe launchers colcon generates on a Windows
# host cannot run on the board)
ros2() {
  python3.12 -c 'import sys; from ros2cli.cli import main; sys.exit(main())' "\$@"
}
# rqt needs libqt_gui_cpp_sip.so preloaded: class_loader's cross-DSO
# dynamic_cast on the weak AbstractMetaObject<Base> typeinfo only works if one
# copy is in the global link scope (libc++abi compares typeinfo by pointer,
# unlike libstdc++'s name-string fallback); CPython dlopen()s extension
# modules with RTLD_LOCAL, and the python3.12 executable is not linked with
# --export-dynamic.
rqt() {
  # os._exit instead of a normal interpreter exit: musl's dlclose of the Qt
  # stack during interpreter teardown segfaults (139) AFTER main() has
  # completed; skipping teardown keeps the real exit code and output.
  LD_PRELOAD="\$ROS2_HOME/Lib/site-packages/qt_gui_cpp/libqt_gui_cpp_sip.so\${LD_PRELOAD:+:\$LD_PRELOAD}" \
    python3.12 -c '
import sys, os
from rqt_gui.main import main
try:
    rc = main()
except SystemExit as e:
    rc = e.code if isinstance(e.code, int) else 0
sys.stdout.flush()
sys.stderr.flush()
os._exit(int(rc or 0))
' "\$@"
}
EOF
# Only pin the RMW when explicitly requested; otherwise both RMWs are
# available and switchable at runtime via RMW_IMPLEMENTATION.
if [ -n "$RMW" ]; then
  echo "export RMW_IMPLEMENTATION=$RMW" >> "$ENV_FILE"
fi

for board in ${BOARDS[@]}; do
  echo "== deploy to $board =="
  # Note: rm -rf $DEVICE_DIR/* does not match dotfiles; stale QSettings caches
  # under .config (rqt's discovered-plugin path cache) must go too.
  "$HDC" -t "$board" shell "mkdir -p $DEVICE_DIR && rm -rf $DEVICE_DIR/* $DEVICE_DIR/.config $DEVICE_DIR/.cache"
  hdc_send "$board" "$ARCHIVE" "$DEVICE_DIR/ros2_ohos_install.tar.gz"
  remote_size=$("$HDC" -t "$board" shell "stat -c %s $DEVICE_DIR/ros2_ohos_install.tar.gz" | tr -d '\r\n ')
  if [ "$remote_size" != "$local_size" ]; then
    echo "   TRANSFER FAILED: local=$local_size remote=$remote_size" >&2
    exit 1
  fi
  hdc_send "$board" "$ENV_FILE" "$DEVICE_DIR/env.sh"
  "$HDC" -t "$board" shell \
    "cd $DEVICE_DIR && tar -xzf ros2_ohos_install.tar.gz && rm ros2_ohos_install.tar.gz && find Lib -type f -exec chmod +x {} + && chmod +x bin/* lib/lttng/libexec/* 2>/dev/null; true"
  # Windows host FS is case-insensitive: the tar only has Lib/, but plugin
  # paths in the ament index reference lib/. Create a compat symlink.
  "$HDC" -t "$board" shell "cd $DEVICE_DIR && ln -sfn Lib lib"
  # verify (hdc shell always exits 0, so check explicitly)
  "$HDC" -t "$board" shell "test -x $DEVICE_DIR/Lib/demo_nodes_cpp/talker && echo TALKER_OK || echo TALKER_MISSING"
done
echo "== deploy done =="
