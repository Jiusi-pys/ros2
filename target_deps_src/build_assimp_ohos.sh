#!/usr/bin/env bash
# Cross-build assimp 5.3.1 for aarch64-linux-ohos and install it into
# install_ohos/ (consumed by rviz_assimp_vendor via its OHOS extras branch).
#
# Idempotent: the source is only downloaded/extracted once and the CMake
# build dir is reused. Delete build-ohos/ to force a rebuild.
#
# Usage: ./target_deps_src/build_assimp_ohos.sh
set -euo pipefail
cd "$(dirname "$0")/.."

WORKSPACE_ROOT="$(pwd -W 2>/dev/null || pwd)"
TOOLCHAIN_FILE="${WORKSPACE_ROOT}/cmake/ohos-aarch64.toolchain.cmake"
PREFIX="${WORKSPACE_ROOT}/install_ohos"
SRC_DIR="${WORKSPACE_ROOT}/target_deps_src"
# shellcheck source=lib/locked_sources.sh
source "$SRC_DIR/lib/locked_sources.sh"

OHOS_SDK="${OHOS_NATIVE_SDK:-}"
[ -n "$OHOS_SDK" ] || {
  echo "ERROR: set OHOS_NATIVE_SDK to the OpenHarmony native SDK directory" >&2
  exit 2
}
OHOS_SDK_FINGERPRINT="$(sdk_fingerprint "$OHOS_SDK")"

# pixi host tools (cmake, ninja) - bare `python` is a WindowsApps stub, and
# pkg-config binaries are blocked by WDAC (cmake/FindPkgConfig.cmake shadows it).
# NB: PATH needs the POSIX form; a Windows-style C:/ entry is listed but
# silently never searched by Git Bash command lookup.
export PATH="/usr/bin:$(pwd)/.pixi/envs/default/Library/bin:$(pwd)/.pixi/envs/default/Library/usr/bin:$PATH"

# The sysroot keeps its libs in usr/lib/aarch64-linux-ohos; this makes
# find_library search lib/<triple> under the toolchain's find roots.
LIBARCH=aarch64-linux-ohos

fetch_locked assimp-5.3.1.tar.gz "$SRC_DIR/assimp-5.3.1.tar.gz"
if [ ! -d "$SRC_DIR/assimp-5.3.1" ]; then
  tar -xf "$(cygpath "$SRC_DIR/assimp-5.3.1.tar.gz")" -C "$(cygpath "$SRC_DIR")"
fi

RECIPE_FINGERPRINT="$(recipe_fingerprint \
  "$(lock_field assimp-5.3.1.tar.gz 3)" \
  "$(sha256_file "$TOOLCHAIN_FILE")" \
  "$(sha256_file "$0")" \
  "$OHOS_SDK_FINGERPRINT")"
MARKER="$SRC_DIR/assimp-5.3.1/build-ohos/.install-done"
if marker_matches "$MARKER" "$RECIPE_FINGERPRINT" && \
   [ -f "$PREFIX/lib/libassimp.so" ]; then
  echo "== assimp already installed, nothing to do"
  exit 0
fi

echo "== building assimp"
cmake -G Ninja -S "$SRC_DIR/assimp-5.3.1" -B "$SRC_DIR/assimp-5.3.1/build-ohos" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DCMAKE_MODULE_PATH="$WORKSPACE_ROOT/cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_LIBRARY_ARCHITECTURE=$LIBARCH \
  -DBUILD_SHARED_LIBS=ON \
  -DASSIMP_BUILD_ASSIMP_TOOLS:BOOL=OFF \
  -DASSIMP_BUILD_TESTS:BOOL=OFF \
  -DASSIMP_BUILD_SAMPLES:BOOL=OFF \
  -DASSIMP_INSTALL_PDB:BOOL=OFF \
  -DASSIMP_WARNINGS_AS_ERRORS:BOOL=OFF \
  "-DCMAKE_CXX_FLAGS=-std=c++14" \
  "-DCMAKE_C_FLAGS=-Wno-deprecated-non-prototype"
cmake --build "$SRC_DIR/assimp-5.3.1/build-ohos"
cmake --install "$SRC_DIR/assimp-5.3.1/build-ohos"
write_marker "$MARKER" "$RECIPE_FINGERPRINT"

echo "== done. assimp installed into $PREFIX"
