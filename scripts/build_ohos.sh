#!/usr/bin/env bash
# Cross-build the ROS 2 stack (C++ core + Python bindings + CLI + demos) for
# OpenHarmony (aarch64). Run from the ros2/ workspace root inside Git Bash:
#   ./scripts/build_ohos.sh [extra colcon args...]
set -euo pipefail
cd "$(dirname "$0")/.."

WORKSPACE_ROOT="$(pwd -W 2>/dev/null || pwd)"
TOOLCHAIN_FILE="${WORKSPACE_ROOT}/cmake/ohos-aarch64.toolchain.cmake"

export PATH="$HOME/.pixi/bin:$PATH"

# Build-time host tools: pure-Python ament packages (ament_package, rosidl_*
# generators, ...) are installed into the target prefix but are executed by
# the *host* Python during the build of dependent packages. colcon's hook
# chain does not reliably forward PYTHONPATH into the cmake subprocess on
# Windows, so export it explicitly.
SITE_PACKAGES="${WORKSPACE_ROOT}/install_ohos/Lib/site-packages"
mkdir -p "$SITE_PACKAGES"
export PYTHONPATH="${SITE_PACKAGES}${PYTHONPATH:+;${PYTHONPATH}}"

# Cross Python configuration: the host (pixi) interpreter runs the interface
# generators and setup.py installs; target headers/libs (pulled from the
# board into python_target/ by scripts/pull_python_target.sh) are used to
# compile the CPython extensions (rclpy, rosidl_generator_py output, ...).
HOST_PYTHON="$(pixi run python -c 'import sys; print(sys.executable)' | tr -d '\r' | sed 's|\\\\|/|g')"
HOST_NUMPY_INCLUDE="$(pixi run python -c 'import numpy; print(numpy.get_include())' | tr -d '\r' | sed 's|\\\\|/|g')"
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
  # other rmw DDS implementations are out of scope for this port
  rmw_mdds
  # shared-memory transport (CycloneDDS built with ENABLE_ICEORYX=OFF)
  iceoryx_binding_c iceoryx_hoofs iceoryx_posh iceoryx_introspection
  # Rust generator (no Rust toolchain for the target)
  rosidl_generator_rs
  # test-only mocking lib: aarch64 trampoline asm does not assemble with the
  # OHOS toolchain; test_depend entries in kept packages were removed instead
  mimick_vendor
  # GUI packages (Qt / rviz / rqt) - no display stack on the board
  python_qt_binding qt_dotgraph qt_gui qt_gui_app qt_gui_core qt_gui_cpp qt_gui_py_common
  rqt rqt_action rqt_bag rqt_bag_plugins rqt_console rqt_graph rqt_gui rqt_gui_cpp
  rqt_gui_py rqt_msg rqt_plot rqt_publisher rqt_py_common rqt_py_console
  rqt_reconfigure rqt_service_caller rqt_shell rqt_srv rqt_topic
  rviz_assimp_vendor rviz_common rviz_default_plugins rviz_ogre_vendor rviz_rendering
  rviz_rendering_tests rviz_visual_testing_framework rviz2
  tango_icons_vendor turtlesim
  # Gazebo vendor packages
  gz_cmake_vendor gz_math_vendor gz_utils_vendor
  # LTTng tracing (not available on OHOS; TRACETOOLS_DISABLED=ON)
  lttngpy ros2trace tracetools_launch tracetools_read tracetools_test tracetools_trace
  test_ros2trace
  # security tooling: needs the `cryptography` wheel (Rust build) on the target
  sros2 sros2_cmake
  # meta package exec_depends on sros2 (skipped above)
  ros2cli_common_extensions
  # needs a target Bullet build; tf2_bullet is an optional conversion helper
  tf2_bullet
  # OpenCV-dependent demos (no OpenCV for the target; cv_bridge is not in the
  # ros2.repos workspace either)
  image_tools intra_process_demo
  # host-side lint tooling (not useful on the board). ament_clang_format /
  # ament_cmake_clang_format stay: rosbag2_storage_mcap declares test_depends
  # on them and colcon needs their environment hooks.
  uncrustify_vendor ament_uncrustify
  ament_clang_tidy ament_cmake_clang_tidy
  ament_cmake_mypy ament_cmake_pclint ament_cmake_pycodestyle ament_cmake_pyflakes
  ament_pclint ament_pyflakes
  # test-only packages (BUILD_TESTING=OFF). rosbag2_test_common /
  # rosbag2_test_msgdefs stay: other rosbag2 packages declare test_depends on
  # them and colcon needs their environment hooks. Same for rosbag2_tests
  # (test_depend of the rosbag2 meta package).
  test_cli test_cli_remapping test_communication test_launch_ros test_launch_testing
  test_osrf_testing_tools_cpp test_quality_of_service test_rclcpp test_rmw_implementation
  test_security test_tf2 test_tracetools test_tracetools_launch
  rosidl_generator_tests rosidl_typesupport_introspection_tests rosidl_typesupport_tests
  launch_testing_examples
  rosbag2_performance_benchmarking rosbag2_performance_benchmarking_msgs
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
    -DBUILD_TESTING=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DTHIRDPARTY=ON \
    -DENABLE_SSL=NO \
    -DENABLE_ICEORYX=OFF \
    -DBUILD_IDLC=OFF \
    -DBUILD_DDSPERF=OFF \
    -DTRACETOOLS_DISABLED=ON \
    -DRMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=OFF \
    -DPython3_EXECUTABLE="${HOST_PYTHON}" \
    -DPython3_INCLUDE_DIR="${PY_TARGET}/include/python3.12" \
    -DPython3_LIBRARY="${PY_TARGET}/lib/libpython3.12.so" \
    -DPython3_SOABI="cpython-312-aarch64-linux-ohos" \
    -DPython3_NumPy_INCLUDE_DIR="${HOST_NUMPY_INCLUDE}" \
    -DPYTHON_MODULE_EXTENSION=".cpython-312-aarch64-linux-ohos.so" \
    --no-warn-unused-cli \
  "$@"
