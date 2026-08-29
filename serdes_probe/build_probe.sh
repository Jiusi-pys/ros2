#!/usr/bin/env bash
# Cross-compile serdes_probe.cpp for OpenHarmony (aarch64) against install_ohos.
# Run from ros2/ (workspace root):  bash serdes_probe/build_probe.sh
set -euo pipefail

SDK="${OHOS_NATIVE_SDK:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/native}"
CXX="$SDK/llvm/bin/clang++.exe"
HERE="$(cd "$(dirname "$0")" && pwd)"
INSTALL="$(cd "$HERE/../install_ohos" && pwd -W)"

# Only the packages the probe includes; adding every include/<pkg> dir pulls in
# shadow headers (e.g. zzip/stdint.h) that break libc++.
PKGS=(
  rcutils
  rmw
  rosidl_runtime_c
  rosidl_runtime_cpp
  rosidl_typesupport_interface
  rosidl_typesupport_c
  rosidl_typesupport_cpp
  std_msgs
  builtin_interfaces
  rcl_interfaces
)
INC=()
for p in "${PKGS[@]}"; do
  INC+=("-I$INSTALL/include/$p")
done

LIBS=(
  -lrmw_mdds
  -lrcutils
  -lrosidl_runtime_c
  -lrosidl_typesupport_c
  -lrosidl_typesupport_cpp
  -lrosidl_typesupport_introspection_c
  -lrcl_interfaces__rosidl_generator_c
  -lrcl_interfaces__rosidl_typesupport_c
  -lrcl_interfaces__rosidl_typesupport_cpp
  -lbuiltin_interfaces__rosidl_generator_c
  -lbuiltin_interfaces__rosidl_typesupport_c
  -lbuiltin_interfaces__rosidl_typesupport_cpp
  -lstd_msgs__rosidl_generator_c
  -lstd_msgs__rosidl_typesupport_c
  -lstd_msgs__rosidl_typesupport_cpp
)

"$CXX" --target=aarch64-linux-ohos --sysroot="$SDK/sysroot" -D__MUSL__ \
  -std=c++17 -O2 -Wall \
  "$HERE/serdes_probe.cpp" \
  "${INC[@]}" \
  -L"$INSTALL/lib" "${LIBS[@]}" \
  -Wl,--allow-shlib-undefined \
  -o "$HERE/serdes_probe"

echo "built $HERE/serdes_probe"
