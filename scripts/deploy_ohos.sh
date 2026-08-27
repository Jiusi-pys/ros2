#!/usr/bin/env bash
# Deploy the OHOS cross-built install tree to both RK3588 boards.
# Run from the ros2/ workspace root inside Git Bash:
#   ./scripts/deploy_ohos.sh [board ...]
# Default boards: the two connected RK3588 devices.
set -euo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-/c/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
# RMW to activate on the boards: RMW=rmw_fastrtps_cpp ./scripts/deploy_ohos.sh
RMW="${RMW:-rmw_cyclonedds_cpp}"
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
export RMW_IMPLEMENTATION=$RMW
export RCUTILS_COLORIZED_OUTPUT=0
export RCUTILS_CONSOLE_OUTPUT_FORMAT="[{severity}] [{name}]: {message}"
export HOME=\$ROS2_HOME
export ROS_LOG_DIR=\$ROS2_HOME/log
# OHOS has no /dev/shm: restrict Fast-DDS to UDP (avoids SHM transport errors)
export FASTDDS_BUILTIN_TRANSPORTS=UDPv4
# demo executables (Windows-style layout from the cross build)
export ROS2_TALKER=\$ROS2_HOME/Lib/demo_nodes_cpp/talker
export ROS2_LISTENER=\$ROS2_HOME/Lib/demo_nodes_cpp/listener
EOF

for board in ${BOARDS[@]}; do
  echo "== deploy to $board =="
  "$HDC" -t "$board" shell "mkdir -p $DEVICE_DIR && rm -rf $DEVICE_DIR/*"
  hdc_send "$board" "$ARCHIVE" "$DEVICE_DIR/ros2_ohos_install.tar.gz"
  remote_size=$("$HDC" -t "$board" shell "stat -c %s $DEVICE_DIR/ros2_ohos_install.tar.gz" | tr -d '\r\n ')
  if [ "$remote_size" != "$local_size" ]; then
    echo "   TRANSFER FAILED: local=$local_size remote=$remote_size" >&2
    exit 1
  fi
  hdc_send "$board" "$ENV_FILE" "$DEVICE_DIR/env.sh"
  "$HDC" -t "$board" shell \
    "cd $DEVICE_DIR && tar -xzf ros2_ohos_install.tar.gz && rm ros2_ohos_install.tar.gz && find Lib -type f -exec chmod +x {} +"
  # Windows host FS is case-insensitive: the tar only has Lib/, but plugin
  # paths in the ament index reference lib/. Create a compat symlink.
  "$HDC" -t "$board" shell "cd $DEVICE_DIR && ln -sfn Lib lib"
  # verify (hdc shell always exits 0, so check explicitly)
  "$HDC" -t "$board" shell "test -x $DEVICE_DIR/Lib/demo_nodes_cpp/talker && echo TALKER_OK || echo TALKER_MISSING"
done
echo "== deploy done =="
