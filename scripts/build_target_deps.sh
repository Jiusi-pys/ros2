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

# Every network input is declared in sources.lock and verified before use.
# shellcheck source=../target_deps_src/lib/locked_sources.sh
source "$SRC_DIR/lib/locked_sources.sh"

export PATH="$HOME/.pixi/bin:$PATH"

extract() {  # extract <tarball> <dstdir> <expected-dir>
  if [ -d "$3" ]; then return 0; fi
  # GNU tar parses a leading "C:" in a Windows path as a remote host; give it
  # POSIX paths instead.
  local posix_tarball posix_dir
  posix_tarball="$(cygpath "$1")"; posix_dir="$(cygpath "$2")"
  tar -xf "$posix_tarball" -C "$posix_dir"  # auto-detects gz/bz2/xz
}

# --- autotools cross-compile support (userspace-rcu, lttng-ust) -------------
OHOS_SDK="${OHOS_NATIVE_SDK:-}"
[ -n "$OHOS_SDK" ] || {
  echo "ERROR: set OHOS_NATIVE_SDK to the OpenHarmony native SDK directory" >&2
  exit 2
}
[ -d "$OHOS_SDK/sysroot" ] || {
  echo "ERROR: OHOS native SDK sysroot is missing: $OHOS_SDK/sysroot" >&2
  exit 2
}
OHOS_LLVM_BIN="$OHOS_SDK/llvm/bin"
OHOS_SYSROOT="$OHOS_SDK/sysroot"
OHOS_SDK_FINGERPRINT="$(sdk_fingerprint "$OHOS_SDK")"
printf 'SDK_VERIFIED fingerprint=%s\n' "$OHOS_SDK_FINGERPRINT"

TARGET_DEPS_FINGERPRINT="$(recipe_fingerprint \
  "$(sha256_file "$SOURCE_LOCK")" \
  "$(sha256_file "$SRC_DIR/lttng-ust-2.13.8-ohos.patch")" \
  "$(sha256_file "$TOOLCHAIN_FILE")" \
  "$(sha256_file "$0")" \
  "$OHOS_SDK_FINGERPRINT")"
TARGET_DEPS_MARKER="$PREFIX/.ohos-target-deps.recipe.sha256"
if marker_matches "$TARGET_DEPS_MARKER" "$TARGET_DEPS_FINGERPRINT" && \
   [ -f "$PREFIX/lib/libtinyxml2.so" ] && \
   [ -f "$PREFIX/lib/libconsole_bridge.so" ] && \
   [ -f "$PREFIX/lib/libBulletCollision.so" ] && \
   [ -f "$PREFIX/lib/libopencv_core.so" ] && \
   [ -f "$PREFIX/lib/liburcu.so" ] && \
   [ -f "$PREFIX/lib/liblttng-ust.so" ] && \
   [ -f "$PREFIX/lib/libpopt.so" ] && \
   [ -f "$PREFIX/lib/libxml2.so" ] && \
   [ -f "$PREFIX/bin/lttng" ]; then
  printf 'TARGET_DEPS_RECIPE fingerprint=%s state=already-installed\n' \
    "$TARGET_DEPS_FINGERPRINT"
  exit 0
fi

ohos_cc_wrappers() {
  # autotools needs CC/CXX to be commands; wrap the NDK clang with the target
  # triple/sysroot baked in (the NDK's own wrappers are unusable from CMake but
  # fine here - still, explicit wrappers keep flags identical to the toolchain
  # file: aarch64-linux-ohos + sysroot + -D__MUSL__).
  local dir="$SRC_DIR/ohos-autotools-bin"
  # These tracked wrappers resolve OHOS_NATIVE_SDK at invocation time.  Do not
  # rewrite them with a workstation-specific SDK path during a release build.
  [ -f "$dir/ohos-cc" ] && [ -f "$dir/ohos-cxx" ] || {
    echo "ERROR: tracked OHOS compiler wrappers are missing from $dir" >&2
    return 1
  }
  chmod +x "$dir/ohos-cc" "$dir/ohos-cxx"
  echo "$dir"
}

build_autotools() {  # build_autotools <srcdir> <name> [extra configure args...]
  local src="$1" name="$2"; shift 2
  echo "== building $name (autotools)"
  local wrappers; wrappers="$(ohos_cc_wrappers)"
  local host_sh
  host_sh="$(cygpath -m "$(cygpath -d /usr/bin/sh)")"
  case "$host_sh" in
    *[[:space:]]*)
      echo "ERROR: Git Bash short shell path still contains whitespace: $host_sh" >&2
      return 1
      ;;
  esac
  # NB: do NOT set MSYS2_ARG_CONV_EXCL here - autotools passes POSIX paths
  # (-I/c/..., ../../../src/foo.c) that MSYS must convert to Windows paths for
  # clang.exe; disabling conversion silently breaks every -I flag.
  #
  # Configure in the freshly extracted source tree.  An out-of-tree configure
  # canonicalizes srcdir to /c/... under Git Bash; the pixi GNU make is a
  # native Windows executable and then treats that prerequisite as C:\c\...
  # ("No rule to make target .../Makefile.am").  In-tree Autotools keeps
  # srcdir='.' while retaining MSYS argument conversion for the OHOS compiler.
  # Clean mode proves that this source tree did not exist before BEGIN, so this
  # does not reuse or modify a caller-owned checkout.
  (
    export PATH="/usr/bin:$(cygpath "$WORKSPACE_ROOT/.pixi/envs/default/Library/bin"):$(cygpath "$WORKSPACE_ROOT/.pixi/envs/default/Library/usr/bin"):$PATH"
    cd "$src"
    env \
      CONFIG_SHELL="$host_sh" \
      SHELL="$host_sh" \
      CC="$wrappers/ohos-cc" \
      CXX="$wrappers/ohos-cxx" \
      LD="$(cygpath "$OHOS_LLVM_BIN/ld.lld.exe")" \
      AR="$(cygpath "$OHOS_LLVM_BIN/llvm-ar.exe")" \
      RANLIB="$(cygpath "$OHOS_LLVM_BIN/llvm-ranlib.exe")" \
      NM="$(cygpath "$OHOS_LLVM_BIN/llvm-nm.exe")" \
      STRIP="$(cygpath "$OHOS_LLVM_BIN/llvm-strip.exe")" \
      OBJDUMP="$(cygpath "$OHOS_LLVM_BIN/llvm-objdump.exe")" \
      CPPFLAGS="-I$(cygpath "$PREFIX/include")" \
      LDFLAGS="-L$(cygpath "$PREFIX/lib")" \
      ./configure \
        --host=aarch64-linux-musl \
        --prefix="$PREFIX" \
        --disable-static \
        "$@"
    # GNU libtool classifies the OHOS linker as GNU/Linux and otherwise bakes
    # the Windows staging prefix into DT_RUNPATH during its install relink.
    # The board loads these peers from its deployed LD_LIBRARY_PATH; a host
    # C:/... directory is both unusable and non-reproducible.  Clear both
    # hard-code mechanisms in every generated libtool stanza before linking.
    [ -f libtool ] || { echo "ERROR: $name configure did not generate libtool" >&2; exit 1; }
    sed -i \
      -e 's|^runpath_var=.*|runpath_var=|' \
      -e 's|^hardcode_libdir_flag_spec=.*|hardcode_libdir_flag_spec=|' \
      -e 's|^hardcode_libdir_separator=.*|hardcode_libdir_separator=|' \
      -e 's|^hardcode_into_libs=.*|hardcode_into_libs=no|' \
      libtool
    if grep -E '^(runpath_var|hardcode_libdir_flag_spec|hardcode_libdir_separator)=.+' libtool >/dev/null || \
       grep -E '^hardcode_into_libs=(yes|unknown)' libtool >/dev/null; then
      echo "ERROR: failed to disable host RUNPATH in $name libtool" >&2
      exit 1
    fi
    make SHELL="$host_sh" -j"$(nproc)"
    make SHELL="$host_sh" install
  )
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
fetch_locked tinyxml2-10.0.0.tar.gz "$SRC_DIR/tinyxml2-10.0.0.tar.gz"
extract "$SRC_DIR/tinyxml2-10.0.0.tar.gz" "$SRC_DIR" "$SRC_DIR/tinyxml2-10.0.0"
build_cmake "$SRC_DIR/tinyxml2-10.0.0" tinyxml2 -Dtinyxml2_BUILD_TESTING=OFF
# The static archive is built without PIC and urdfdom would pick it over the
# shared library; keep only the .so and drop the static CMake target files.
rm -f "$PREFIX/lib/libtinyxml2.a" "$PREFIX/lib/cmake/tinyxml2/tinyxml2-static-targets"*.cmake

# --- console_bridge 1.0.1 (matches pixi pin) --------------------------------
fetch_locked console_bridge-1.0.1.tar.gz "$SRC_DIR/console_bridge-1.0.1.tar.gz"
extract "$SRC_DIR/console_bridge-1.0.1.tar.gz" "$SRC_DIR" "$SRC_DIR/console_bridge-1.0.1"
build_cmake "$SRC_DIR/console_bridge-1.0.1" console_bridge

# --- Bullet 3.25 (for tf2_bullet; only the math/collision/dynamics libs) ----
fetch_locked bullet3-3.25.tar.gz "$SRC_DIR/bullet3-3.25.tar.gz"
extract "$SRC_DIR/bullet3-3.25.tar.gz" "$SRC_DIR" "$SRC_DIR/bullet3-3.25"
build_cmake "$SRC_DIR/bullet3-3.25" bullet \
  -DINSTALL_LIBS=ON \
  -DBUILD_BULLET3=OFF -DBUILD_EXTRAS=OFF -DBUILD_UNIT_TESTS=OFF \
  -DBUILD_CPU_DEMOS=OFF -DBUILD_OPENGL3_DEMOS=OFF -DBUILD_BULLET2_DEMOS=OFF \
  -DBUILD_CLSOCKET=OFF -DBUILD_ENET=OFF -DBUILD_PYBULLET=OFF \
  -DUSE_GRAPHICAL_BENCHMARK=OFF

# --- OpenCV 4.9.0 (matches pixi pin; minimal module set for the demos) ------
# image_tools / intra_process_demo only need core,imgproc,imgcodecs,highgui,
# videoio. All GUI/capture backends are off: cam2image -m 0 renders in
# software and showimage must not need a display server.
fetch_locked opencv-4.9.0.tar.gz "$SRC_DIR/opencv-4.9.0.tar.gz"
extract "$SRC_DIR/opencv-4.9.0.tar.gz" "$SRC_DIR" "$SRC_DIR/opencv-4.9.0"
build_cmake "$SRC_DIR/opencv-4.9.0" opencv \
  -DBUILD_LIST=core,imgproc,imgcodecs,highgui,videoio \
  -DBUILD_opencv_apps=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOCS=OFF \
  -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_JAVA=OFF \
  -DBUILD_opencv_python3=OFF -DBUILD_opencv_python_bindings_generator=OFF \
  -DBUILD_PROTOBUF=OFF -DWITH_PROTOBUF=OFF -DWITH_CUDA=OFF -DWITH_OPENCL=OFF \
  -DWITH_GTK=OFF -DWITH_QT=OFF -DWITH_VTK=OFF -DWITH_FFMPEG=OFF \
  -DWITH_V4L=OFF -DWITH_GSTREAMER=OFF -DWITH_1394=OFF -DWITH_OPENJPEG=OFF \
  -DWITH_JASPER=OFF -DWITH_WEBP=OFF -DWITH_OPENEXR=OFF -DWITH_IPP=OFF \
  -DWITH_TBB=OFF -DWITH_OPENMP=OFF -DWITH_PTHREADS_PF=OFF \
  -DCPU_BASELINE=NEON -DCPU_DISPATCH= \
  -DCMAKE_SKIP_RPATH=ON \
  -DOPENCV_ENABLE_PKG_CONFIG=ON

# --- Eigen 3.4.0 (header-only, but still installed from an immutable input) --
# Do not copy this from the mutable pixi host prefix: that used to let a local
# package repair or an unrelated developer edit silently enter a clean target
# dependency build without appearing in sources.lock.
fetch_locked eigen-3.4.0.tar.gz "$SRC_DIR/eigen-3.4.0.tar.gz"
extract "$SRC_DIR/eigen-3.4.0.tar.gz" "$SRC_DIR" "$SRC_DIR/eigen-3.4.0"
build_cmake "$SRC_DIR/eigen-3.4.0" eigen \
  -DEIGEN_BUILD_DOC=OFF \
  -DEIGEN_BUILD_PKGCONFIG=ON

# --- userspace-rcu 0.14.1 (liburcu; required by lttng-ust) ------------------
fetch_locked userspace-rcu-0.14.1.tar.bz2 "$SRC_DIR/userspace-rcu-0.14.1.tar.bz2"
extract "$SRC_DIR/userspace-rcu-0.14.1.tar.bz2" "$SRC_DIR" "$SRC_DIR/userspace-rcu-0.14.1"
build_autotools "$SRC_DIR/userspace-rcu-0.14.1" userspace-rcu

# --- LTTng-UST 2.13.8 (user-space tracer used by ROS 2 tracetools) ----------
# pkg-config is blocked by the host's application-control policy, so the
# liburcu discovery is fed through URCU_CFLAGS/URCU_LIBS instead (this makes
# configure skip the version check - 0.14.1 > 0.12 satisfies it anyway).
# OHOS musl has no pthread_cancel/pthread_setcancelstate. The tracked patch
# also disables destructor-time dlclose/abort on still-referenced handles.
fetch_locked lttng-ust-2.13.8.tar.bz2 "$SRC_DIR/lttng-ust-2.13.8.tar.bz2"
extract "$SRC_DIR/lttng-ust-2.13.8.tar.bz2" "$SRC_DIR" "$SRC_DIR/lttng-ust-2.13.8"
apply_patch_locked "$SRC_DIR/lttng-ust-2.13.8" \
  "$SRC_DIR/lttng-ust-2.13.8-ohos.patch"
export URCU_CFLAGS="-I$(cygpath "$PREFIX/include")"
export URCU_LIBS="-L$(cygpath "$PREFIX/lib") -lurcu -lurcu-common"
build_autotools "$SRC_DIR/lttng-ust-2.13.8" lttng-ust \
  --disable-numa --disable-examples

# --- popt 1.19 + libxml2 2.9.14 + lttng-tools 2.13.15 (lttng CLI/sessiond;
# liblttng-ctl is needed by ROS 2 lttngpy) -----------------------------------
fetch_locked popt-1.19.tar.gz "$SRC_DIR/popt-1.19.tar.gz"
extract "$SRC_DIR/popt-1.19.tar.gz" "$SRC_DIR" "$SRC_DIR/popt-1.19"
build_autotools "$SRC_DIR/popt-1.19" popt

fetch_locked libxml2-2.9.14.tar.xz "$SRC_DIR/libxml2-2.9.14.tar.xz"
extract "$SRC_DIR/libxml2-2.9.14.tar.xz" "$SRC_DIR" "$SRC_DIR/libxml2-2.9.14"
build_autotools "$SRC_DIR/libxml2-2.9.14" libxml2 \
  --without-python --without-lzma --without-iconv

# Every PKG_CHECK_MODULES in lttng-tools' configure is bypassed via *_CFLAGS/
# *_LIBS env vars (host WDAC blocks any pkg-config binary).
fetch_locked lttng-tools-2.13.15.tar.bz2 "$SRC_DIR/lttng-tools-2.13.15.tar.bz2"
extract "$SRC_DIR/lttng-tools-2.13.15.tar.bz2" "$SRC_DIR" "$SRC_DIR/lttng-tools-2.13.15"
PFXU="$(cygpath "$PREFIX")"
export POPT_CFLAGS="-I$PFXU/include" POPT_LIBS="-L$PFXU/lib -lpopt"
export libxml2_CFLAGS="-I$PFXU/include/libxml2" libxml2_LIBS="-L$PFXU/lib -lxml2"
export URCU_BP_CFLAGS="-I$PFXU/include" URCU_BP_LIBS="-L$PFXU/lib -lurcu-bp"
export URCU_CDS_CFLAGS="-I$PFXU/include" URCU_CDS_LIBS="-L$PFXU/lib -lurcu-cds"
export UST_CFLAGS="-I$PFXU/include" UST_LIBS="-L$PFXU/lib -llttng-ust"
export UST_CTL_CFLAGS="-I$PFXU/include" UST_CTL_LIBS="-L$PFXU/lib -llttng-ust-ctl"
build_autotools "$SRC_DIR/lttng-tools-2.13.15" lttng-tools --disable-man-pages
unset POPT_CFLAGS POPT_LIBS libxml2_CFLAGS libxml2_LIBS \
  URCU_BP_CFLAGS URCU_BP_LIBS URCU_CDS_CFLAGS URCU_CDS_LIBS \
  UST_CFLAGS UST_LIBS UST_CTL_CFLAGS UST_CTL_LIBS
unset URCU_CFLAGS URCU_LIBS

write_marker "$TARGET_DEPS_MARKER" "$TARGET_DEPS_FINGERPRINT"
printf 'TARGET_DEPS_RECIPE fingerprint=%s\n' "$TARGET_DEPS_FINGERPRINT"
echo "target deps installed into $PREFIX"
