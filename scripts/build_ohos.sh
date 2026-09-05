#!/usr/bin/env bash
# Cross-build the ROS 2 stack (C++ core + Python bindings + CLI + demos) for
# OpenHarmony (aarch64). Run from the ros2/ workspace root inside Git Bash:
#   ./scripts/build_ohos.sh [extra colcon args...]
set -euo pipefail
cd "$(dirname "$0")/.."

WORKSPACE_ROOT="$(pwd -W 2>/dev/null || pwd)"
TOOLCHAIN_FILE="${WORKSPACE_ROOT}/cmake/ohos-aarch64.toolchain.cmake"
# A verification run may pin its colcon event log under a sealed evidence
# bundle.  Keep the historic log_ohos default for normal developer builds.
COLCON_LOG_BASE="${OHOS_LOG_BASE:-log_ohos}"
OHOS_DEFAULT_RMW="${OHOS_DEFAULT_RMW:-rmw_fastrtps_cpp}"
OHOS_CYCLONE_SHM="${OHOS_CYCLONE_SHM:-OFF}"
OHOS_DDS_SECURITY="${OHOS_DDS_SECURITY:-OFF}"
OHOS_BUILD_MDDS="${OHOS_BUILD_MDDS:-OFF}"
OHOS_REQUIRE_CLEAN="${OHOS_REQUIRE_CLEAN:-0}"
PYTHON_LOCK="${OHOS_PYTHON_LOCK:-${WORKSPACE_ROOT}/scripts/python/ohos_python.lock.json}"
PYTHON_RUNTIME_ARCHIVE="${OHOS_PYTHON_RUNTIME_ARCHIVE:-${WORKSPACE_ROOT}/python312_ohos_runtime.tar.gz}"
PYTHON_RUNTIME_MANIFEST="${OHOS_PYTHON_RUNTIME_MANIFEST:-${PYTHON_RUNTIME_ARCHIVE}.manifest.json}"
PYTHON_STAGE_MARKER="${OHOS_PYTHON_STAGE_MARKER:-${WORKSPACE_ROOT}/python_target/sitepkgs/.ros2-ohos-python-stage.json}"
OHOS_NATIVE_SDK="${OHOS_NATIVE_SDK:-}"

case "$OHOS_DEFAULT_RMW" in
  rmw_fastrtps_cpp|rmw_cyclonedds_cpp) ;;
  *) echo "error: OHOS_DEFAULT_RMW must be rmw_fastrtps_cpp or rmw_cyclonedds_cpp" >&2; exit 2 ;;
esac
case "$OHOS_CYCLONE_SHM" in ON|OFF) ;; *) echo "error: OHOS_CYCLONE_SHM must be ON or OFF" >&2; exit 2 ;; esac
case "$OHOS_DDS_SECURITY" in
  OFF) ;;
  *) echo "error: DDS Security/TLS is outside this release profile; OHOS_DDS_SECURITY must be OFF" >&2; exit 2 ;;
esac
case "$OHOS_BUILD_MDDS" in ON|OFF) ;; *) echo "error: OHOS_BUILD_MDDS must be ON or OFF" >&2; exit 2 ;; esac
case "$OHOS_REQUIRE_CLEAN" in 0|1) ;; *) echo "error: OHOS_REQUIRE_CLEAN must be 0 or 1" >&2; exit 2 ;; esac

if [ "$OHOS_REQUIRE_CLEAN" = 1 ]; then
  unset CC CXX CPP LD AR AS NM RANLIB STRIP OBJCOPY OBJDUMP \
    CFLAGS CXXFLAGS CPPFLAGS LDFLAGS CPATH CPLUS_INCLUDE_PATH LIBRARY_PATH \
    CMAKE_PREFIX_PATH CMAKE_TOOLCHAIN_FILE CMAKE_GENERATOR CMAKE_BUILD_TYPE \
    PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR \
    AMENT_PREFIX_PATH COLCON_PREFIX_PATH DESTDIR MAKE MAKEFLAGS MFLAGS \
    PYTHONHOME PYTHONPATH PYTHONUSERBASE QMAKESPEC QMAKEFEATURES QTDIR \
    QT_PLUGIN_PATH QT_QPA_PLATFORM CONFIG_SHELL CONFIG_SITE BASH_ENV ENV CDPATH
  export PYTHONNOUSERSITE=1
  if [ "$#" -ne 0 ]; then
    echo "error: clean release builds reject caller-supplied colcon/CMake arguments" >&2
    exit 2
  fi
  for output in build_ohos "$COLCON_LOG_BASE"; do
    if [ -e "$output" ] || [ -L "$output" ]; then
      echo "error: clean-build gate refuses pre-existing output path: $output" >&2
      exit 2
    fi
  done
  if [ ! -d install_ohos ] || [ -L install_ohos ]; then
    echo "error: clean-build gate requires the freshly built target-dependency prefix install_ohos" >&2
    exit 2
  fi
  for stale in \
    install_ohos/Lib/librclcpp.so \
    install_ohos/Lib/librmw_implementation.so \
    install_ohos/Lib/site-packages/rclpy/__init__.py \
    install_ohos/share/ament_index/resource_index/packages/rclcpp; do
    if [ -e "$stale" ] || [ -L "$stale" ]; then
      echo "error: clean-build gate found a pre-existing ROS artifact: $stale" >&2
      exit 2
    fi
  done
  for dependency in \
    install_ohos/.ohos-target-deps.recipe.sha256 \
    install_ohos/lib/libtinyxml2.so \
    install_ohos/lib/libQt5Core.so \
    install_ohos/lib/libQt5Svg.so \
    install_ohos/lib/libOgreMain.so; do
    if [ ! -f "$dependency" ] || [ -L "$dependency" ]; then
      echo "error: clean-build target dependency is missing: $dependency" >&2
      exit 2
    fi
  done
  for receipt_input in \
    "$PYTHON_LOCK" "$PYTHON_RUNTIME_ARCHIVE" "$PYTHON_RUNTIME_MANIFEST" "$PYTHON_STAGE_MARKER"; do
    if [ ! -f "$receipt_input" ] || [ -L "$receipt_input" ]; then
      echo "error: clean-build Python receipt input is missing: $receipt_input" >&2
      exit 2
    fi
  done
  if [ -z "$OHOS_NATIVE_SDK" ] || [ ! -d "$OHOS_NATIVE_SDK" ]; then
    echo "error: clean-build gate requires OHOS_NATIVE_SDK" >&2
    exit 2
  fi
fi

export PATH="$HOME/.pixi/bin:$PATH"

# Verify the exact completed dependency inventory before adding any ROS files.
if [ "$OHOS_REQUIRE_CLEAN" = 1 ]; then
  pixi run python target_deps_src/lib/clean_deps_receipt.py verify \
    --workspace "$WORKSPACE_ROOT" --prefix install_ohos \
    --manifest install_ohos/.ohos-target-deps.clean-prefix.json \
    --receipt install_ohos/.ohos-target-deps.clean-receipt.json
fi

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
# generators and setup.py installs; locked target headers/libs staged into
# python_target/usr from the verified CPython runtime artifact are used to
# compile the CPython extensions (rclpy, rosidl_generator_py output, ...).
# NOTE: normalize to forward slashes - a backslash path embedded into a cmake
# string (e.g. add_launch_test's PYTHON_EXECUTABLE) breaks re-parsing with
# "Invalid character escape '\U'".
HOST_PYTHON="$(pixi run python -c 'import sys; print(sys.executable)' | tr -d '\r' | sed 's|\\|/|g')"
PY_TARGET="${WORKSPACE_ROOT}/python_target/usr"
TARGET_NUMPY_INCLUDE="${WORKSPACE_ROOT}/python_target/sitepkgs/numpy/core/include"
for f in "${PY_TARGET}/include/python3.12/Python.h" "${PY_TARGET}/lib/libpython3.12.so"; do
  if [ ! -f "$f" ]; then
    echo "error: $f missing - stage the locked CPython runtime/interface before building" >&2
    exit 1
  fi
done
for f in "${TARGET_NUMPY_INCLUDE}/numpy/numpyconfig.h" "${TARGET_NUMPY_INCLUDE}/numpy/_numpyconfig.h"; do
  if [ ! -f "$f" ]; then
    echo "error: target NumPy header missing: $f; stage the locked aarch64 wheel first" >&2
    exit 1
  fi
done

# The accepted archive is exactly the post-build tree.  Stage the target C++
# runtime before the BEGIN snapshot; deployment is deliberately read-only with
# respect to install_ohos.
if [ "$OHOS_REQUIRE_CLEAN" = 1 ]; then
  OHOS_LIBCXX="${OHOS_NATIVE_SDK}/llvm/lib/aarch64-linux-ohos/libc++_shared.so"
  if [ ! -f "$OHOS_LIBCXX" ] || [ -L "$OHOS_LIBCXX" ]; then
    echo "error: OHOS libc++ runtime is missing: $OHOS_LIBCXX" >&2
    exit 2
  fi
  mkdir -p install_ohos/Lib
  cp -f "$OHOS_LIBCXX" install_ohos/Lib/libc++_shared.so
fi

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
  # The generic release profile excludes MDDS packages unless explicitly
  # requested with OHOS_BUILD_MDDS=ON.
  # iceoryx is built, but CycloneDDS SHM is deterministic and OFF by default;
  # the opt-in profile must set OHOS_CYCLONE_SHM=ON and pass its own runtime gate.
  # GUI packages (Qt / rqt / turtlesim / rviz) are ported (Phase 6); rviz uses
  # the prebuilt GLES2 OGRE from target_deps_src/build_ogre_ohos.sh.
)
if [ "$OHOS_BUILD_MDDS" = OFF ]; then
  PACKAGES_SKIP+=(mdds mdds_gateway rmw_mdds)
  MDDS_WITH_DSOFTBUS=OFF
else
  MDDS_WITH_DSOFTBUS=ON
fi

printf 'OHOS_BUILD_INPUT default_rmw=%s cyclone_shm=%s dds_security=%s build_mdds=%s target_numpy=%s log_base=%s\n' \
  "$OHOS_DEFAULT_RMW" "$OHOS_CYCLONE_SHM" "$OHOS_DDS_SECURITY" "$OHOS_BUILD_MDDS" \
  "$TARGET_NUMPY_INCLUDE" "$COLCON_LOG_BASE"

BUILD_RECEIPT_BEGIN=""
EXPECTED_PACKAGES=""
if [ "$OHOS_REQUIRE_CLEAN" = 1 ]; then
  # Keep Windows Python and Git Bash on the same filesystem path. Mixing the
  # pixi and Git Bash /tmp mounts can make a freshly created record inaccessible.
  RECEIPT_GIT_DIR="$(git rev-parse --path-format=absolute --git-dir)"
  RECEIPT_GIT_DIR="$(cd "$RECEIPT_GIT_DIR" && (pwd -W 2>/dev/null || pwd -P))"
  RECEIPT_TMP_BASE="$RECEIPT_GIT_DIR/ohos-build-receipt.$$.${RANDOM}${RANDOM}"
  mkdir -- "$RECEIPT_TMP_BASE"
  BUILD_RECEIPT_BEGIN="$RECEIPT_TMP_BASE/begin.json"
  EXPECTED_PACKAGES="$RECEIPT_TMP_BASE/packages.txt"
  LISTED_PACKAGES="$RECEIPT_TMP_BASE/listed-packages.txt"
  cleanup_receipt_inputs() {
    rm -f -- "$BUILD_RECEIPT_BEGIN" "$EXPECTED_PACKAGES" "$LISTED_PACKAGES"
    rmdir -- "$RECEIPT_TMP_BASE"
  }
  trap cleanup_receipt_inputs EXIT
  trap 'cleanup_receipt_inputs; exit 130' INT
  trap 'cleanup_receipt_inputs; exit 143' TERM HUP

  declare -A SKIPPED_PACKAGE=()
  for package in "${PACKAGES_SKIP[@]}"; do SKIPPED_PACKAGE["$package"]=1; done
  # Do not hide a failed colcon discovery behind process substitution.
  pixi run colcon list --base-paths src --names-only > "$LISTED_PACKAGES"
  while IFS= read -r package; do
    package="${package%$'\r'}"
    [[ "$package" =~ ^[A-Za-z0-9_.+-]+$ ]] || {
      echo "error: colcon listed an unsafe package name: $package" >&2
      exit 2
    }
    if [ -z "${SKIPPED_PACKAGE[$package]+present}" ]; then
      printf '%s\n' "$package" >> "$EXPECTED_PACKAGES"
    fi
  done < "$LISTED_PACKAGES"
  LC_ALL=C sort -u -o "$EXPECTED_PACKAGES" "$EXPECTED_PACKAGES"
  [ -s "$EXPECTED_PACKAGES" ] || { echo "error: clean build package plan is empty" >&2; exit 2; }

  pixi run python scripts/ohos_build_receipt.py begin \
    --workspace "$WORKSPACE_ROOT" \
    --lock "$WORKSPACE_ROOT/ros2.ohos.lock.repos" \
    --sdk-root "$OHOS_NATIVE_SDK" \
    --install-root "$WORKSPACE_ROOT/install_ohos" \
    --rmw "$OHOS_DEFAULT_RMW" \
    --cyclonedds-shm "$OHOS_CYCLONE_SHM" \
    --dds-security "$OHOS_DDS_SECURITY" \
    --build-mdds "$OHOS_BUILD_MDDS" \
    --python-lock "$PYTHON_LOCK" \
    --python-runtime-archive "$PYTHON_RUNTIME_ARCHIVE" \
    --python-runtime-manifest "$PYTHON_RUNTIME_MANIFEST" \
    --python-stage-marker "$PYTHON_STAGE_MARKER" \
    --python-target-root "$PY_TARGET" \
    --output "$BUILD_RECEIPT_BEGIN"
fi

# PYTHON_MODULE_EXTENSION: pybind11 queries the HOST interpreter for
# EXT_SUFFIX (yielding a win_amd64 .pyd name); override with the target value.
# --base-paths src: colcon's default scan root is the workspace root, which
# would pick up target_deps_src/* as plain cmake packages (a static, non-PIC
# tinyxml2 gets installed and poisons rosbag2_storage/urdfdom).
set +e
pixi run colcon --log-base "$COLCON_LOG_BASE" build --merge-install \
  --build-base build_ohos --install-base install_ohos \
  --base-paths src \
  --packages-skip "${PACKAGES_SKIP[@]}" \
  --event-handlers console_direct+ \
  --cmake-args \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DMDDS_WITH_DSOFTBUS="$MDDS_WITH_DSOFTBUS" \
    -DBUILD_TESTING=ON \
    -DCMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE=PRE_TEST \
    -DBUILD_EXAMPLES=OFF \
    -DTHIRDPARTY=ON \
    -DTHIRDPARTY_fastcdr=OFF \
    -DTHIRDPARTY_Asio=FORCE \
    -DTHIRDPARTY_TinyXML2=FORCE \
    -DTHIRDPARTY_UPDATE=OFF \
    -DCMAKE_SKIP_RPATH=ON \
    -DENABLE_SSL=NO \
    -DSECURITY=OFF \
    -DNO_TLS=ON \
    -DSHM_TRANSPORT_DEFAULT=OFF \
    -DENABLE_SHM="$OHOS_CYCLONE_SHM" \
    -DBUILD_IDLC=OFF \
    -DBUILD_DDSPERF=OFF \
    -DFORCE_BUILD_VENDOR_PKG=ON \
    -DCMAKE_MODULE_PATH="${WORKSPACE_ROOT}/cmake" \
    -DCMAKE_LIBRARY_ARCHITECTURE=aarch64-linux-ohos \
    -DRMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=OFF \
    -DRMW_IMPLEMENTATION="$OHOS_DEFAULT_RMW" \
    -DPython3_EXECUTABLE="${HOST_PYTHON}" \
    -DPython3_INCLUDE_DIR="${PY_TARGET}/include/python3.12" \
    -DPython3_LIBRARY="${PY_TARGET}/lib/libpython3.12.so" \
    -DPython3_SOABI="cpython-312-aarch64-linux-ohos" \
    -DPython_EXECUTABLE="${HOST_PYTHON}" \
    -DPython_INCLUDE_DIR="${PY_TARGET}/include/python3.12" \
    -DPython_LIBRARY="${PY_TARGET}/lib/libpython3.12.so" \
    -DPython3_NumPy_INCLUDE_DIR="${TARGET_NUMPY_INCLUDE}" \
    -DPYTHON_MODULE_EXTENSION=".cpython-312-aarch64-linux-ohos.so" \
    -DOHOS_SIP4_EXECUTABLE="${WORKSPACE_ROOT}/target_deps_src/qt-host-tools/sip4/Library/bin/sip.exe" \
    -DOHOS_SIP4_INCLUDE_DIR="${WORKSPACE_ROOT}/target_deps_src/qt-host-tools/sip4/include" \
    -DOHOS_PYQT5_SIP_DIR="${WORKSPACE_ROOT}/target_deps_src/pyqt/PyQt5-5.15.11/sip" \
    --no-warn-unused-cli \
  "$@"
COLCON_RC=$?
set -e
if [ "$COLCON_RC" -ne 0 ]; then
  printf 'OHOS_BUILD_TERMINAL result=FAIL rc=%s default_rmw=%s cyclone_shm=%s dds_security=%s build_mdds=%s\n' \
    "$COLCON_RC" "$OHOS_DEFAULT_RMW" "$OHOS_CYCLONE_SHM" "$OHOS_DDS_SECURITY" "$OHOS_BUILD_MDDS" >&2
  exit "$COLCON_RC"
fi

if [ -d install_ohos/share/ament_index ]; then
  find install_ohos/share/ament_index -type f -exec sed -i 's/\r$//' {} +
fi
pixi run python scripts/finalize_ohos_install.py install_ohos

# Package the exact runtime verification implementation inside the tree that
# the COMPLETE receipt hashes. Board acceptance uses only this installed copy.
mkdir -p install_ohos/share/ros2_ohos
cp scripts/verify_board_python.py scripts/python_runtime_artifact.py scripts/trace_mount_namespace.py install_ohos/share/ros2_ohos/
if [ "$OHOS_REQUIRE_CLEAN" = 1 ]; then
  mkdir -p install_ohos/Lib/ros2_ohos_tests
  while IFS= read -r test_name; do
    [[ "$test_name" =~ ^test_[a-z_]+$ ]] || exit 2
    test_binary="build_ohos/test_rmw_implementation/$test_name"
    [ -f "$test_binary" ] && [ ! -L "$test_binary" ] || {
      echo "error: clean build omitted required RMW regression binary: $test_name" >&2
      exit 2
    }
    cp "$test_binary" install_ohos/Lib/ros2_ohos_tests/
  done < scripts/ohos_rmw_tests.txt
  cp scripts/ohos_rmw_tests.txt install_ohos/share/ros2_ohos/
  pixi run python scripts/audit_ohos_elf.py --root install_ohos \
    --readelf "$OHOS_NATIVE_SDK/llvm/bin/llvm-readelf.exe"
  pixi run python src/eProsima/Fast-DDS/test/ohos/check_topic_description_rtti.py \
    --readelf "$OHOS_NATIVE_SDK/llvm/bin/llvm-readelf.exe" \
    --provider install_ohos/Lib/libfastrtps.so \
    --consumer install_ohos/Lib/librmw_fastrtps_shared_cpp.so
fi

BUILD_RECEIPT="NOT_REQUESTED"
if [ "$OHOS_REQUIRE_CLEAN" = 1 ]; then
  mapfile -t EVENT_LOGS < <(find "$COLCON_LOG_BASE" -mindepth 2 -maxdepth 2 -type f -name events.log | LC_ALL=C sort)
  if [ "${#EVENT_LOGS[@]}" -ne 1 ]; then
    echo "error: expected exactly one clean-build events.log, found ${#EVENT_LOGS[@]}" >&2
    exit 2
  fi
  BUILD_RECEIPT="$COLCON_LOG_BASE/ohos_build_receipt.json"
  pixi run python scripts/ohos_build_receipt.py finish \
    --workspace "$WORKSPACE_ROOT" \
    --lock "$WORKSPACE_ROOT/ros2.ohos.lock.repos" \
    --sdk-root "$OHOS_NATIVE_SDK" \
    --begin "$BUILD_RECEIPT_BEGIN" \
    --install-root "$WORKSPACE_ROOT/install_ohos" \
    --build-log "${EVENT_LOGS[0]}" \
    --expected-packages "$EXPECTED_PACKAGES" \
    --output "$BUILD_RECEIPT"
fi
printf 'OHOS_BUILD_TERMINAL result=PASS default_rmw=%s cyclone_shm=%s dds_security=%s build_mdds=%s receipt=%s\n' \
  "$OHOS_DEFAULT_RMW" "$OHOS_CYCLONE_SHM" "$OHOS_DDS_SECURITY" "$OHOS_BUILD_MDDS" "$BUILD_RECEIPT"
