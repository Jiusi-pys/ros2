#!/usr/bin/env bash
# Source-build ROS 2 Jazzy (trimmed set) on Ubuntu 20.04 Focal.
#
# Toolchain strategy (Focal has no Jazzy binaries; deadsnakes dropped focal):
#   - Python 3.12  : conda env "jazzy-build" (micromamba), no prebuilt ROS to avoid shadowing
#   - C/C++        : system gcc-12 / g++-12 (ppa:ubuntu-toolchain-r/test)
#   - CMake >=3.22 : Kitware (installs 4.x) -> CMAKE_POLICY_VERSION_MINIMUM=3.5 for legacy modules
#   - C++ libs     : apt (libssl/tinyxml2/eigen/asio ...)
#
# Usage:
#   ./build_jazzy_focal.sh                 # build default trimmed closure
#   ./build_jazzy_focal.sh <pkg> [pkg...]  # build --packages-up-to <pkg...>
set -euo pipefail

WS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$WS"

ENV_NAME="${JAZZY_BUILD_ENV:-jazzy-build}"
ENV_PREFIX="${MAMBA_ROOT_PREFIX:-$HOME/micromamba}/envs/${ENV_NAME}"
PYTHON_EXE="${ENV_PREFIX}/bin/python"
NUMPY_INC="$("$PYTHON_EXE" -c 'import numpy; print(numpy.get_include())' 2>/dev/null)"

PKGS=("$@")
if [ ${#PKGS[@]} -eq 0 ]; then
  # Core runtime + rclpy + demo nodes + ros2 CLI verbs, plus the COMPLETE
  # tf2 (geometry2) and rosbag2 repos. geometry2 metapackage pulls the whole
  # tf2 stack; examples/test_tf2 and the rosbag2 examples/benchmarking are
  # added explicitly. All verified on Focal (9/9 capability tests pass).
  PKGS=(demo_nodes_cpp demo_nodes_py rclpy
        ros2run ros2node ros2topic ros2pkg ros2service ros2param
        ros2interface ros2action ros2component ros2doctor
        ros2lifecycle ros2multicast ros2launch
        # tf2 / geometry2 (full repo)
        geometry2 tf2 tf2_ros tf2_py tf2_msgs tf2_geometry_msgs
        tf2_sensor_msgs tf2_eigen tf2_eigen_kdl tf2_kdl tf2_bullet
        tf2_tools tf2_ros_py examples_tf2_py test_tf2
        # rosbag2 (full repo)
        rosbag2 ros2bag rosbag2_transport rosbag2_cpp rosbag2_py
        rosbag2_storage rosbag2_storage_default_plugins
        rosbag2_storage_sqlite3 rosbag2_storage_mcap
        rosbag2_compression rosbag2_compression_zstd
        rosbag2_examples_cpp rosbag2_examples_py
        rosbag2_performance_benchmarking
        sensor_msgs nav_msgs
        # image pipeline (image_common is in core src)
        image_transport camera_info_manager camera_calibration_parsers)
  # cv_bridge / image_geometry live in vision_opencv (extra repo, see
  # vision_opencv.repos). Add them only when that repo has been cloned.
  if [ -d "${WS}/src/ros-perception/vision_opencv/cv_bridge" ]; then
    PKGS+=(cv_bridge image_geometry opencv_tests)
  fi
fi

# Expose ONLY asio headers (not the whole conda include dir, which would leak
# conda's openssl 3.x headers and break fastrtps linkage against system libssl).
ASIO_INC_DIR="${WS}/.deps/asio-include"
if [ -f "${ENV_PREFIX}/include/asio.hpp" ]; then
  mkdir -p "${ASIO_INC_DIR}"
  ln -sfn "${ENV_PREFIX}/include/asio.hpp" "${ASIO_INC_DIR}/asio.hpp"
  ln -sfn "${ENV_PREFIX}/include/asio"     "${ASIO_INC_DIR}/asio"
fi

# Pre-seed system SQLite3 / lz4 paths so rosbag2's sqlite3_vendor / liblz4_vendor
# custom Find modules (which can otherwise miss multiarch libdirs under the conda
# build env) resolve them. find_library/find_path treat a pre-cached value as a
# no-op, so FPHSA passes.
_libdir="/usr/lib/$(uname -m)-linux-gnu"
SQLITE3_LIB="$( [ -e "${_libdir}/libsqlite3.so" ] && echo "${_libdir}/libsqlite3.so" || echo /usr/lib/libsqlite3.so )"
SQLITE3_INC="/usr/include"
LZ4_LIB="$( [ -e "${_libdir}/liblz4.so" ] && echo "${_libdir}/liblz4.so" || echo /usr/lib/liblz4.so )"
LZ4_INC="/usr/include"

# OpenCV / Boost come from the conda env (no system OpenCV on Focal; Boost.Python
# must match python3.12). Point CONFIG-mode find_package at the env's cmake dirs.
# Only cv_bridge / image_geometry consume these; harmless for other packages.
OPENCV_DIR_HINT="$(ls -d "${ENV_PREFIX}/lib/cmake/opencv4" 2>/dev/null || true)"
BOOST_DIR_HINT="$(ls -d "${ENV_PREFIX}"/lib/cmake/Boost-* 2>/dev/null | head -1 || true)"

export CC=gcc-12
export CXX=g++-12
# CMake 4.x removed compatibility with cmake_minimum_required(<3.5); many ROS
# vendored modules still declare it. This floor keeps them configurable.
export CMAKE_POLICY_VERSION_MINIMUM=3.5

echo "== workspace      : $WS"
echo "== build env      : $ENV_NAME ($PYTHON_EXE)"
echo "== compiler       : $($CC --version | head -1)"
echo "== cmake          : $(cmake --version | head -1)"
echo "== packages-up-to : ${PKGS[*]}"

exec micromamba run -n "$ENV_NAME" \
  colcon build --merge-install \
    --packages-up-to "${PKGS[@]}" \
    --cmake-args \
      -DCMAKE_BUILD_TYPE=Release \
      -DPython3_EXECUTABLE="$PYTHON_EXE" \
      -DBUILD_TESTING=OFF \
      -DAsio_INCLUDE_DIR="${ASIO_INC_DIR}" \
      -DCMAKE_PROJECT_INCLUDE_BEFORE="${WS}/cmake/ensure_python_targets_host.cmake" \
      -DHOST_NUMPY_INCLUDE_DIR="${NUMPY_INC}" \
      -DSQLite3_LIBRARY="${SQLITE3_LIB}" \
      -DSQLite3_INCLUDE_DIR="${SQLITE3_INC}" \
      -Dlz4_LIBRARY="${LZ4_LIB}" \
      -Dlz4_INCLUDE_DIR="${LZ4_INC}" \
      -DOpenCV_DIR="${OPENCV_DIR_HINT}" \
      -DBoost_DIR="${BOOST_DIR_HINT}" \
    --parallel-workers 4 \
    --continue-on-error \
    --event-handlers console_cohesion+ console_package_list+
