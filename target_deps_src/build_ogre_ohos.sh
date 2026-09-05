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

extract() {  # extract <tarball> <dstdir> <expected-dir>
  if [ -d "$3" ]; then return 0; fi
  tar -xf "$(cygpath "$1")" -C "$(cygpath "$2")"
}

# ---------------------------------------------------------------- freetype --
fetch_locked freetype-2.13.2.tar.xz "$SRC_DIR/freetype-2.13.2.tar.xz"
extract "$SRC_DIR/freetype-2.13.2.tar.xz" "$SRC_DIR" "$SRC_DIR/freetype-2.13.2"

FREETYPE_RECIPE="$(recipe_fingerprint \
  "$(lock_field freetype-2.13.2.tar.xz 3)" \
  "$(sha256_file "$TOOLCHAIN_FILE")" "$OHOS_SDK_FINGERPRINT" \
  "$(sha256_file "$0")" freetype)"
FREETYPE_MARKER="$SRC_DIR/freetype-2.13.2/build-ohos/.install-done"
if ! marker_matches "$FREETYPE_MARKER" "$FREETYPE_RECIPE" || \
   [ ! -f "$PREFIX/lib/libfreetype.so" ]; then
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
  write_marker "$FREETYPE_MARKER" "$FREETYPE_RECIPE"
fi

# ----------------------------------------------------------------- zziplib --
fetch_locked zziplib-0.13.72.tar.gz "$SRC_DIR/zziplib-0.13.72.tar.gz"
extract "$SRC_DIR/zziplib-0.13.72.tar.gz" "$SRC_DIR" "$SRC_DIR/zziplib-0.13.72"
[ -d "$SRC_DIR/zziplib" ] || mv "$SRC_DIR/zziplib-0.13.72" "$SRC_DIR/zziplib"

ZZIP_RECIPE="$(recipe_fingerprint \
  "$(lock_field zziplib-0.13.72.tar.gz 3)" \
  "$(sha256_file "$TOOLCHAIN_FILE")" "$OHOS_SDK_FINGERPRINT" \
  "$(sha256_file "$0")" zziplib)"
ZZIP_MARKER="$SRC_DIR/zziplib/build-ohos/.install-done"
if ! marker_matches "$ZZIP_MARKER" "$ZZIP_RECIPE" || \
   [ ! -f "$PREFIX/lib/libzzip-0.so" ]; then
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
  write_marker "$ZZIP_MARKER" "$ZZIP_RECIPE"
fi

# -------------------------------------------------------------------- ogre --
ensure_locked_git_checkout ogre-1.12.10 "$SRC_DIR/ogre-1.12.10"

# OHOS patch: pbuffer-based EGL support layer (RenderSystems/GLSupport/
# {include,src}/EGL/OHOS/), OHOS branch in GLSupport's CMake, X11 made
# non-required, OGRE_EXTRA_MODULE_PATH hook for the FindPkgConfig shadow.
# Apply with GNU patch: `git apply` silently skips hunks in some repos here.
apply_patch_locked "$SRC_DIR/ogre-1.12.10" "$PATCH_FILE"

OGRE_RECIPE="$(recipe_fingerprint \
  "$(lock_field ogre-1.12.10 3)" "$(sha256_file "$PATCH_FILE")" \
  "$(sha256_file "$TOOLCHAIN_FILE")" "$OHOS_SDK_FINGERPRINT" \
  "$FREETYPE_RECIPE" "$ZZIP_RECIPE" "$(sha256_file "$0")")"
OGRE_MARKER="$SRC_DIR/ogre-1.12.10/build-ohos/.install-done"
if marker_matches "$OGRE_MARKER" "$OGRE_RECIPE" && \
   [ -f "$PREFIX/lib/OGRE/RenderSystem_GLES2.so" ]; then
  echo "== OGRE already installed, nothing to do"
  exit 0
fi

echo "== configuring ogre"
cmake -G Ninja -S "$SRC_DIR/ogre-1.12.10" -B "$SRC_DIR/ogre-1.12.10/build-ohos" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
  -DOGRE_EXTRA_MODULE_PATH="$WORKSPACE_ROOT/cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_SKIP_RPATH=ON \
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

# OGRE overrides CMAKE_INSTALL_RPATH with the Windows install prefix. The
# deployment profile supplies its exact library directory through LD_LIBRARY_PATH.
for library in "$PREFIX/lib/libOgreMain.so" "$PREFIX/lib/OGRE/RenderSystem_GLES2.so"; do
  dynamic_tags="$("$OHOS_SDK/llvm/bin/llvm-readelf.exe" -d "$library")"
  if printf '%s\n' "$dynamic_tags" | grep -Eq '\((RPATH|RUNPATH)\)'; then
    echo "ERROR: OGRE retained a build-host runtime search path: $library" >&2
    exit 1
  fi
done

# plugins.cfg bakes the host build path into PluginFolder; point it at the
# on-device location (deploy_ohos.sh unpacks install_ohos at /data/local/tmp/ros2).
sed -i 's|^PluginFolder=.*|PluginFolder=/data/local/tmp/ros2/lib/OGRE|' \
  "$PREFIX/share/OGRE/plugins.cfg"
write_marker "$OGRE_MARKER" "$OGRE_RECIPE"

echo "== done. OGRE installed into $PREFIX (libs: lib/ + lib/OGRE/, headers: include/OGRE/)"
