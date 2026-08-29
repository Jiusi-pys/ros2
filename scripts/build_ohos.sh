#!/usr/bin/env bash
# Cross-build the ROS 2 stack (C++ core + Python bindings + CLI + demos) for
# OpenHarmony (aarch64). Run from the ros2/ workspace root inside Git Bash:
#   ./scripts/build_ohos.sh [extra colcon args...]
set -euo pipefail
cd "$(dirname "$0")/.."

WORKSPACE_ROOT="$(pwd -W 2>/dev/null || pwd)"
TOOLCHAIN_FILE="${WORKSPACE_ROOT}/cmake/ohos-aarch64.toolchain.cmake"

export PATH="$HOME/.pixi/bin:$PATH"

# rosidl_generator_rs (and other generators) expect ROS_DISTRO in the
# environment; colcon's hook chain does not reliably forward env vars into the
# cmake subprocess on Windows, so export it explicitly.
export ROS_DISTRO=jazzy

# Build-time host tools: pure-Python ament packages (ament_package, rosidl_*
# generators, ...) are installed into the target prefix but are executed by
# the *host* Python during the build of dependent packages. colcon's hook
# chain does not reliably forward PYTHONPATH into the cmake subprocess on
# Windows, so export it explicitly.
SITE_PACKAGES="${WORKSPACE_ROOT}/install_ohos/Lib/site-packages"
mkdir -p "$SITE_PACKAGES"
export PYTHONPATH="${SITE_PACKAGES}${PYTHONPATH:+;${PYTHONPATH}}"
# ament lint CMake macros find_program() the lint CLI entry points at
# configure time (BUILD_TESTING=ON); they live in Scripts/ on a Windows host.
# NOTE: use the unix-style pwd (not $WORKSPACE_ROOT, which is C:/... style):
# MSYS PATH conversion mangles drive-letter entries in a colon-separated PATH.
export PATH="$(pwd)/install_ohos/Scripts${PATH:+:$PATH}"

# Cross Python configuration: the host (pixi) interpreter runs the interface
# generators and setup.py installs; target headers/libs (pulled from the
# board into python_target/ by scripts/pull_python_target.sh) are used to
# compile the CPython extensions (rclpy, rosidl_generator_py output, ...).
# NOTE: normalize to forward slashes - a backslash path embedded into a cmake
# string (e.g. add_launch_test's PYTHON_EXECUTABLE) breaks re-parsing with
# "Invalid character escape '\U'".
HOST_PYTHON="$(pixi run python -c 'import sys; print(sys.executable)' | tr -d '\r' | sed 's|\\|/|g')"
HOST_NUMPY_INCLUDE="$(pixi run python -c 'import numpy; print(numpy.get_include())' | tr -d '\r' | sed 's|\\|/|g')"
PY_TARGET="${WORKSPACE_ROOT}/python_target/usr"
for f in "${PY_TARGET}/include/python3.12/Python.h" "${PY_TARGET}/lib/libpython3.12.so"; do
  if [ ! -f "$f" ]; then
    echo "error: $f missing - run scripts/pull_python_target.sh first" >&2
    exit 1
  fi
done

# Everything in the workspace is built except the packages below.
# NOTE: colcon requires the environment hooks of EVERY declared dependency
# (including test deps and group members) of every built package. Packages
# whose only problem was being a test dep of a kept package are therefore
# NOT skipped but simply built; the package.xml of packages that declare
# deps on the packages below have been patched instead.
PACKAGES_SKIP=(
  # alternative DDS vendors (Connext); Fast-DDS (fastrtps) is ported
  rmw_connextdds rmw_connextdds_common rmw_connextddsmicro rti_connext_dds_cmake_module
  rosidl_generator_dds_idl
  # rmw_mdds is in scope (mdds/dsoftbus RMW implementation)
  # iceoryx is ported (Phase 4): CycloneDDS picks up iceoryx_binding_c
  # automatically (ENABLE_SHM=AUTO) once the iceoryx packages are installed.
  # GUI packages (Qt / rqt / turtlesim / rviz) are ported (Phase 6); rviz uses
  # the prebuilt GLES2 OGRE from target_deps_src/build_ogre_ohos.sh.
)

# PYTHON_MODULE_EXTENSION: pybind11 queries the HOST interpreter for
# EXT_SUFFIX (yielding a win_amd64 .pyd name); override with the target value.
# --base-paths src: colcon's default scan root is the workspace root, which
# would pick up target_deps_src/* as plain cmake packages (a static, non-PIC
# tinyxml2 gets installed and poisons rosbag2_storage/urdfdom).
exec pixi run colcon --log-base log_ohos build --merge-install \
  --build-base build_ohos --install-base install_ohos \
  --base-paths src \
  --packages-skip "${PACKAGES_SKIP[@]}" \
  --event-handlers console_direct+ \
  --cmake-args \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DCMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE=PRE_TEST \
    -DBUILD_EXAMPLES=OFF \
    -DTHIRDPARTY=ON \
    -DENABLE_SSL=NO \
    -DBUILD_IDLC=OFF \
    -DBUILD_DDSPERF=OFF \
    -DFORCE_BUILD_VENDOR_PKG=ON \
    -DCMAKE_MODULE_PATH="${WORKSPACE_ROOT}/cmake" \
    -DCMAKE_LIBRARY_ARCHITECTURE=aarch64-linux-ohos \
    -DRMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=OFF \
    -DPython3_EXECUTABLE="${HOST_PYTHON}" \
    -DPython3_INCLUDE_DIR="${PY_TARGET}/include/python3.12" \
    -DPython3_LIBRARY="${PY_TARGET}/lib/libpython3.12.so" \
    -DPython3_SOABI="cpython-312-aarch64-linux-ohos" \
    -DPython_EXECUTABLE="${HOST_PYTHON}" \
    -DPython_INCLUDE_DIR="${PY_TARGET}/include/python3.12" \
    -DPython_LIBRARY="${PY_TARGET}/lib/libpython3.12.so" \
    -DPython3_NumPy_INCLUDE_DIR="${HOST_NUMPY_INCLUDE}" \
    -DPYTHON_MODULE_EXTENSION=".cpython-312-aarch64-linux-ohos.so" \
    -DOHOS_SIP4_EXECUTABLE="${WORKSPACE_ROOT}/target_deps_src/qt-host-tools/sip4/Library/bin/sip.exe" \
    -DOHOS_SIP4_INCLUDE_DIR="${WORKSPACE_ROOT}/target_deps_src/qt-host-tools/sip4/include" \
    -DOHOS_PYQT5_SIP_DIR="${WORKSPACE_ROOT}/target_deps_src/pyqt/PyQt5-5.15.11/sip" \
    --no-warn-unused-cli \
  "$@"
