#!/usr/bin/env bash
# Cross-build OGRE 1.12.10 (GLES2 render system, pbuffer EGL) for
# aarch64-linux-ohos and install it into install_ohos/.
#
# Also cross-builds the two dependencies that are missing from both the OHOS
# sysroot and install_ohos: freetype (OgreOverlay fonts) and zziplib
# (OGRE_CONFIG_ENABLE_ZIP). zlib comes from the OHOS sysroot (libz.so).
#
# Idempotent: sources are only downloaded/extracted/patched once, CMake build
# dirs are reused. Delete the respective build-ohos/ dir to force a rebuild.
#
# Usage: ./target_deps_src/build_ogre_ohos.sh
set -euo pipefail
cd "$(dirname "$0")/.."

WORKSPACE_ROOT="$(pwd -W 2>/dev/null || pwd)"
TOOLCHAIN_FILE="${WORKSPACE_ROOT}/cmake/ohos-aarch64.toolchain.cmake"
PREFIX="${WORKSPACE_ROOT}/install_ohos"
SRC_DIR="${WORKSPACE_ROOT}/target_deps_src"
PATCH_FILE="${SRC_DIR}/ogre-1.12.10-ohos.patch"

# pixi host tools (cmake, ninja) - bare `python` is a WindowsApps stub, and
# pkg-config binaries are blocked by WDAC (cmake/FindPkgConfig.cmake shadows it).
# NB: PATH needs the POSIX form; a Windows-style C:/ entry is listed but
# silently never searched by Git Bash command lookup.
export PATH="$(pwd)/.pixi/envs/default/Library/bin:$PATH"

# The sysroot keeps its libs in usr/lib/aarch64-linux-ohos; this makes
# find_library search lib/<triple> under the toolchain's find roots.
LIBARCH=aarch64-linux-ohos

fetch() {  # fetch <url> <out>
  if [ ! -f "$2" ]; then
    echo "== downloading $1"
    curl -fSL --retry 3 -o "$2" "$1"
  fi
}

extract() {  # extract <tarball> <dstdir> <expected-dir>
  if [ -d "$3" ]; then return 0; fi
  tar -xf "$(cygpath "$1")" -C "$(cygpath "$2")"
}

# ---------------------------------------------------------------- freetype --
fetch "https://download.savannah.gnu.org/releases/freetype/freetype-2.13.2.tar.xz" \
      "$SRC_DIR/freetype-2.13.2.tar.xz"
extract "$SRC_DIR/freetype-2.13.2.tar.xz" "$SRC_DIR" "$SRC_DIR/freetype-2.13.2"

if [ ! -f "$SRC_DIR/freetype-2.13.2/build-ohos/.install-done" ]; then
  echo "== building freetype"
  cmake -G Ninja -S "$SRC_DIR/freetype-2.13.2" -B "$SRC_DIR/freetype-2.13.2/build-ohos" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_MODULE_PATH="$WORKSPACE_ROOT/cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_LIBRARY_ARCHITECTURE=$LIBARCH \
    -DBUILD_SHARED_LIBS=ON \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_BROTLI=ON
  cmake --build "$SRC_DIR/freetype-2.13.2/build-ohos"
  cmake --install "$SRC_DIR/freetype-2.13.2/build-ohos"
  touch "$SRC_DIR/freetype-2.13.2/build-ohos/.install-done"
fi

# ----------------------------------------------------------------- zziplib --
fetch "https://github.com/gdraheim/zziplib/archive/refs/tags/v0.13.72.tar.gz" \
      "$SRC_DIR/zziplib-0.13.72.tar.gz"
extract "$SRC_DIR/zziplib-0.13.72.tar.gz" "$SRC_DIR" "$SRC_DIR/zziplib-0.13.72"
[ -d "$SRC_DIR/zziplib" ] || mv "$SRC_DIR/zziplib-0.13.72" "$SRC_DIR/zziplib"

if [ ! -f "$SRC_DIR/zziplib/build-ohos/.install-done" ]; then
  echo "== building zziplib"
  # NB: cmake --install prints two harmless "cd: /C:/..." errors from
  # zziplib's symlink post-install scripts; the real files install fine.
  cmake -G Ninja -S "$SRC_DIR/zziplib" -B "$SRC_DIR/zziplib/build-ohos" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_MODULE_PATH="$WORKSPACE_ROOT/cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_LIBRARY_ARCHITECTURE=$LIBARCH \
    -DBUILD_SHARED_LIBS=ON -DBUILD_STATIC_LIBS=OFF \
    -DZZIPMMAPPED=OFF -DZZIPFSEEKO=OFF -DZZIPWRAP=OFF \
    -DZZIPSDL=OFF -DZZIPBINS=OFF -DZZIPTEST=OFF -DZZIPDOCS=OFF
  cmake --build "$SRC_DIR/zziplib/build-ohos"
  cmake --install "$SRC_DIR/zziplib/build-ohos"
  touch "$SRC_DIR/zziplib/build-ohos/.install-done"
fi

# -------------------------------------------------------------------- ogre --
if [ ! -d "$SRC_DIR/ogre-1.12.10/.git" ]; then
  echo "== cloning ogre v1.12.10"
  rm -rf "$SRC_DIR/ogre-1.12.10"
  git clone --depth 1 --branch v1.12.10 https://github.com/OGRECave/ogre.git \
    "$SRC_DIR/ogre-1.12.10"
fi

# OHOS patch: pbuffer-based EGL support layer (RenderSystems/GLSupport/
# {include,src}/EGL/OHOS/), OHOS branch in GLSupport's CMake, X11 made
# non-required, OGRE_EXTRA_MODULE_PATH hook for the FindPkgConfig shadow.
# Apply with GNU patch: `git apply` silently skips hunks in some repos here.
if ! patch -d "$SRC_DIR/ogre-1.12.10" -p1 -R --dry-run -f -s < "$PATCH_FILE" >/dev/null 2>&1; then
  echo "== applying $PATCH_FILE"
  patch -d "$(cygpath "$SRC_DIR/ogre-1.12.10")" -p1 -N -f < "$PATCH_FILE"
fi

echo "== configuring ogre"
cmake -G Ninja -S "$SRC_DIR/ogre-1.12.10" -B "$SRC_DIR/ogre-1.12.10/build-ohos" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DOGRE_EXTRA_MODULE_PATH="$WORKSPACE_ROOT/cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_LIBRARY_ARCHITECTURE=$LIBARCH \
  -DOGRE_BUILD_RENDERSYSTEM_GL:BOOL=OFF \
  -DOGRE_BUILD_RENDERSYSTEM_GLES2:BOOL=ON \
  -DOGRE_BUILD_RENDERSYSTEM_D3D9:BOOL=OFF \
  -DOGRE_BUILD_RENDERSYSTEM_D3D11:BOOL=OFF \
  -DOGRE_GLSUPPORT_USE_EGL:BOOL=ON \
  -DOGRE_BUILD_DEPENDENCIES:BOOL=OFF \
  -DOGRE_BUILD_TESTS:BOOL=OFF \
  -DOGRE_BUILD_SAMPLES:BOOL=FALSE \
  -DOGRE_BUILD_TOOLS:BOOL=FALSE \
  -DOGRE_BUILD_COMPONENT_PYTHON:BOOL=OFF \
  -DOGRE_BUILD_COMPONENT_JAVA:BOOL=OFF \
  -DOGRE_BUILD_COMPONENT_CSHARP:BOOL=OFF \
  -DOGRE_BUILD_COMPONENT_BITES:BOOL=OFF \
  -DOGRE_BUILD_PLUGIN_DOT_SCENE:BOOL=OFF \
  -DOGRE_CONFIG_THREADS:STRING=0 \
  -DOGRE_RESOURCEMANAGER_STRICT:STRING=2 \
  -DOGRE_BUILD_LIBS_AS_FRAMEWORKS:BOOL=OFF \
  -DOGRE_CONFIG_ENABLE_ZIP:BOOL=ON \
  -DOGRE_STATIC:BOOL=OFF

cmake --build "$SRC_DIR/ogre-1.12.10/build-ohos"
cmake --install "$SRC_DIR/ogre-1.12.10/build-ohos"

# plugins.cfg bakes the host build path into PluginFolder; point it at the
# on-device location (deploy_ohos.sh unpacks install_ohos at /data/local/tmp/ros2).
sed -i 's|^PluginFolder=.*|PluginFolder=/data/local/tmp/ros2/lib/OGRE|' \
  "$PREFIX/share/OGRE/plugins.cfg"

echo "== done. OGRE installed into $PREFIX (libs: lib/ + lib/OGRE/, headers: include/OGRE/)"
