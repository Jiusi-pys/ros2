#!/usr/bin/env bash
# Cross-build third-party target dependencies that are normally provided by the
# host system on Windows (via pixi) but must exist as aarch64-linux-ohos builds
# for the board: tinyxml2 (pluginlib), console_bridge (urdfdom), Eigen headers
# (tf2_eigen & friends). Everything installs straight into install_ohos/ (the
# toolchain's CMAKE_FIND_ROOT_PATH already covers it, and deploy_ohos.sh ships
# it as-is).
#
# Usage: ./scripts/build_target_deps.sh
set -euo pipefail
cd "$(dirname "$0")/.."

WORKSPACE_ROOT="$(pwd -W 2>/dev/null || pwd)"
TOOLCHAIN_FILE="${WORKSPACE_ROOT}/cmake/ohos-aarch64.toolchain.cmake"
PREFIX="${WORKSPACE_ROOT}/install_ohos"
SRC_DIR="${WORKSPACE_ROOT}/target_deps_src"
mkdir -p "$SRC_DIR"

export PATH="$HOME/.pixi/bin:$PATH"

fetch() {  # fetch <url> <out.tar.gz>
  if [ ! -f "$2" ]; then
    echo "== downloading $1"
    curl -fSL --retry 3 -o "$2" "$1"
  fi
}

extract() {  # extract <tarball> <dstdir> <expected-dir>
  if [ -d "$3" ]; then return 0; fi
  # GNU tar parses a leading "C:" in a Windows path as a remote host; give it
  # POSIX paths instead.
  local posix_tarball posix_dir
  posix_tarball="$(cygpath "$1")"; posix_dir="$(cygpath "$2")"
  tar -xzf "$posix_tarball" -C "$posix_dir"
}

build_cmake() {  # build_cmake <srcdir> <name> [extra cmake args...]
  local src="$1" name="$2"; shift 2
  echo "== building $name"
  pixi run cmake -S "$src" -B "$src/build-ohos" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF "$@"
  pixi run cmake --build "$src/build-ohos"
  pixi run cmake --install "$src/build-ohos"
}

# --- tinyxml2 10.0.0 (matches pixi pin) -------------------------------------
fetch https://github.com/leethomason/tinyxml2/archive/refs/tags/10.0.0.tar.gz "$SRC_DIR/tinyxml2-10.0.0.tar.gz"
extract "$SRC_DIR/tinyxml2-10.0.0.tar.gz" "$SRC_DIR" "$SRC_DIR/tinyxml2-10.0.0"
build_cmake "$SRC_DIR/tinyxml2-10.0.0" tinyxml2 -Dtinyxml2_BUILD_TESTING=OFF
# The static archive is built without PIC and urdfdom would pick it over the
# shared library; keep only the .so and drop the static CMake target files.
rm -f "$PREFIX/lib/libtinyxml2.a" "$PREFIX/lib/cmake/tinyxml2/tinyxml2-static-targets"*.cmake

# --- console_bridge 1.0.1 (matches pixi pin) --------------------------------
fetch https://github.com/ros/console_bridge/archive/refs/tags/1.0.1.tar.gz "$SRC_DIR/console_bridge-1.0.1.tar.gz"
extract "$SRC_DIR/console_bridge-1.0.1.tar.gz" "$SRC_DIR" "$SRC_DIR/console_bridge-1.0.1"
build_cmake "$SRC_DIR/console_bridge-1.0.1" console_bridge

# --- Eigen 3.4.0 headers + CMake package files (header-only; identical to the pixi pin)
if [ ! -f "$PREFIX/include/eigen3/signature_of_eigen3_matrix_library" ]; then
  echo "== installing Eigen headers"
  mkdir -p "$PREFIX/include"
  cp -r "${WORKSPACE_ROOT}/.pixi/envs/default/Library/include/eigen3" "$PREFIX/include/"
fi
if [ ! -f "$PREFIX/share/eigen3/cmake/Eigen3Config.cmake" ]; then
  echo "== installing Eigen CMake package files"
  mkdir -p "$PREFIX/share/eigen3"
  cp -r "${WORKSPACE_ROOT}/.pixi/envs/default/Library/share/eigen3/cmake" "$PREFIX/share/eigen3/"
fi

echo "target deps installed into $PREFIX"
