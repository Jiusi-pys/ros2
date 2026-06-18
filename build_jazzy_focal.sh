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
  # Extra repos (see extra_repos.repos) — appended only when checked out.
  [ -d "${WS}/src/ros-perception/vision_msgs" ] && PKGS+=(vision_msgs)
  [ -d "${WS}/src/ros/diagnostics" ] && PKGS+=(diagnostic_updater diagnostic_aggregator
        diagnostic_common_diagnostics diagnostic_remote_logging self_test diagnostics)
  [ -d "${WS}/src/ros-perception/image_pipeline" ] && PKGS+=(image_proc depth_image_proc
        stereo_image_proc image_publisher image_rotate image_view camera_calibration
        tracetools_image_pipeline image_pipeline)
  if [ -d "${WS}/src/ros-navigation/navigation2" ]; then
    PKGS+=(nav2_bt_navigator nav2_planner nav2_controller nav2_behaviors nav2_waypoint_follower
        nav2_navfn_planner nav2_smac_planner nav2_theta_star_planner
        nav2_mppi_controller nav2_regulated_pure_pursuit_controller nav2_rotation_shim_controller
        nav2_graceful_controller nav2_dwb_controller nav2_collision_monitor nav2_velocity_smoother
        nav2_smoother nav2_constrained_smoother nav2_amcl nav2_map_server nav2_lifecycle_manager
        nav2_route opennav_docking opennav_docking_bt opennav_docking_core nav2_simple_commander)
  fi
  [ -d "${WS}/src/SteveMacenski/slam_toolbox" ] && PKGS+=(slam_toolbox)
  if [ -d "${WS}/src/ros-controls/ros2_control" ]; then
    PKGS+=(controller_manager hardware_interface controller_interface transmission_interface joint_limits)
  fi
  if [ -d "${WS}/src/ros-controls/ros2_controllers" ]; then
    # all controllers except the rqt GUI package
    while IFS= read -r _p; do PKGS+=("$_p"); done < <(
      find "${WS}/src/ros-controls/ros2_controllers" -name package.xml -exec dirname {} \; |
        xargs -n1 basename | grep -vE 'rqt' | sort)
  fi
  if [ -d "${WS}/src/moveit/moveit2" ]; then
    # moveit2 HEADLESS core (no setup_assistant/Qt, rviz, perception, chomp/stomp/pilz, py)
    PKGS+=(moveit_core moveit_ros_occupancy_map_monitor moveit_ros_planning moveit_ros_move_group
        moveit_ros_warehouse moveit_ros_planning_interface moveit_kinematics moveit_planners_ompl
        moveit_simple_controller_manager moveit_ros_control_interface moveit_servo
        moveit_configs_utils moveit_plugins)
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
# Other conda CONFIG packages consumed by nav2 (Ceres -> nav2_constrained_smoother,
# xsimd -> nav2_mppi_controller). Harmless when the package doesn't use them.
CERES_DIR_HINT="$(ls -d "${ENV_PREFIX}/lib/cmake/Ceres" 2>/dev/null || true)"
XSIMD_DIR_HINT="$(ls -d "${ENV_PREFIX}/share/cmake/xsimd" 2>/dev/null || true)"
# Eigen 3.4.0 from conda: the version Jazzy targets. apt's 3.3.7 has GCC-12
# bugs (BDCSVD operator== non-bool, Eigen/Core preprocessor) that break
# kinematics_interface_kdl / moveit; conda 5.0 is too new AND its config sets
# no EIGEN3_INCLUDE_DIRS. conda 3.4.0 is GCC-12-clean and sets the vars.
EIGEN3_DIR_HINT="$(ls -d "${ENV_PREFIX}/share/eigen3/cmake" 2>/dev/null || true)"

export CC=gcc-12
export CXX=g++-12

# Conda-provided native libs (OpenCV, OMPL, Boost) are built against a newer
# libstdc++ (CXXABI_1.3.15) than system gcc-12 ships. Prefer the conda
# libstdc++ at link AND runtime so those symbols resolve. libstdc++ is
# backward-compatible, so this is safe for system-gcc-12-compiled objects too.
CONDA_LINK_FLAGS="-L${ENV_PREFIX}/lib -Wl,-rpath,${ENV_PREFIX}/lib"

# Make conda CONFIG packages (OpenCV, Boost, OMPL, Ceres, xsimd, xtensor,
# GeographicLib, METIS, nanoflann ...) discoverable to find_package, including
# transitive finds (e.g. Ceres -> METIS) that per-package -D<Pkg>_DIR hints
# cannot satisfy. colcon prepends the workspace install dirs, so workspace
# packages still win. Safe now that fastrtps (the only openssl consumer that
# could mis-pick conda's openssl 3.x headers) is already built.
export CMAKE_PREFIX_PATH="${ENV_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
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
      -DCeres_DIR="${CERES_DIR_HINT}" \
      -Dxsimd_DIR="${XSIMD_DIR_HINT}" \
      -DEigen3_DIR="${EIGEN3_DIR_HINT}" \
      "-DCMAKE_EXE_LINKER_FLAGS=${CONDA_LINK_FLAGS}" \
      "-DCMAKE_SHARED_LINKER_FLAGS=${CONDA_LINK_FLAGS}" \
    --parallel-workers 4 \
    --continue-on-error \
    --event-handlers console_cohesion+ console_package_list+
