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
  tar -xf "$posix_tarball" -C "$posix_dir"  # auto-detects gz/bz2/xz
}

# --- autotools cross-compile support (userspace-rcu, lttng-ust) -------------
OHOS_SDK="${OHOS_NATIVE_SDK:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/native}"
OHOS_LLVM_BIN="$OHOS_SDK/llvm/bin"
OHOS_SYSROOT="$OHOS_SDK/sysroot"

ohos_cc_wrappers() {
  # autotools needs CC/CXX to be commands; wrap the NDK clang with the target
  # triple/sysroot baked in (the NDK's own wrappers are unusable from CMake but
  # fine here - still, explicit wrappers keep flags identical to the toolchain
  # file: aarch64-linux-ohos + sysroot + -D__MUSL__).
  local dir="$SRC_DIR/ohos-autotools-bin"
  mkdir -p "$dir"
  cat > "$dir/ohos-cc" <<EOF
#!/usr/bin/env bash
# NB: --sysroot needs a Windows-style path; clang.exe does not understand
# MSYS /c/... paths and MSYS2_ARG_CONV_EXCL='*' disables auto-conversion.
exec "$(cygpath "$OHOS_LLVM_BIN/clang.exe")" --target=aarch64-linux-ohos --sysroot="$OHOS_SYSROOT" -D__MUSL__ "\$@"
EOF
  cat > "$dir/ohos-cxx" <<EOF
#!/usr/bin/env bash
exec "$(cygpath "$OHOS_LLVM_BIN/clang++.exe")" --target=aarch64-linux-ohos --sysroot="$OHOS_SYSROOT" -D__MUSL__ "\$@"
EOF
  chmod +x "$dir/ohos-cc" "$dir/ohos-cxx"
  echo "$dir"
}

build_autotools() {  # build_autotools <srcdir> <name> [extra configure args...]
  local src="$1" name="$2"; shift 2
  echo "== building $name (autotools)"
  local wrappers; wrappers="$(ohos_cc_wrappers)"
  local bdir="$src/build-ohos"
  mkdir -p "$bdir"
  # NB: do NOT set MSYS2_ARG_CONV_EXCL here - autotools passes POSIX paths
  # (-I/c/..., ../../../src/foo.c) that MSYS must convert to Windows paths for
  # clang.exe; disabling conversion silently breaks every -I flag.
  (
    export PATH="$(cygpath "$WORKSPACE_ROOT/.pixi/envs/default/Library/bin"):$PATH"
    cd "$bdir"
    env \
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
      "$(cygpath "$src")/configure" \
        --host=aarch64-linux-musl \
        --prefix="$PREFIX" \
        --disable-static \
        "$@"
    make -j"$(nproc)"
    make install
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

# --- Bullet 3.25 (for tf2_bullet; only the math/collision/dynamics libs) ----
fetch https://github.com/bulletphysics/bullet3/archive/refs/tags/3.25.tar.gz "$SRC_DIR/bullet3-3.25.tar.gz"
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
fetch https://github.com/opencv/opencv/archive/refs/tags/4.9.0.tar.gz "$SRC_DIR/opencv-4.9.0.tar.gz"
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
  -DOPENCV_ENABLE_PKG_CONFIG=ON

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

# --- userspace-rcu 0.14.1 (liburcu; required by lttng-ust) ------------------
fetch https://lttng.org/files/urcu/userspace-rcu-0.14.1.tar.bz2 "$SRC_DIR/userspace-rcu-0.14.1.tar.bz2"
extract "$SRC_DIR/userspace-rcu-0.14.1.tar.bz2" "$SRC_DIR" "$SRC_DIR/userspace-rcu-0.14.1"
build_autotools "$SRC_DIR/userspace-rcu-0.14.1" userspace-rcu

# --- LTTng-UST 2.13.8 (user-space tracer used by ROS 2 tracetools) ----------
# pkg-config is blocked by the host's application-control policy, so the
# liburcu discovery is fed through URCU_CFLAGS/URCU_LIBS instead (this makes
# configure skip the version check - 0.14.1 > 0.12 satisfies it anyway).
# NOTE: the extracted tree carries OHOS patches (OHOS musl has no
# pthread_cancel/pthread_setcancelstate; see ust-cancelstate.c and
# lttng-ust-comm.c __MUSL__ guards). Re-extracting loses them.
fetch https://lttng.org/files/lttng-ust/lttng-ust-2.13.8.tar.bz2 "$SRC_DIR/lttng-ust-2.13.8.tar.bz2"
extract "$SRC_DIR/lttng-ust-2.13.8.tar.bz2" "$SRC_DIR" "$SRC_DIR/lttng-ust-2.13.8"
export URCU_CFLAGS="-I$(cygpath "$PREFIX/include")"
export URCU_LIBS="-L$(cygpath "$PREFIX/lib") -lurcu -lurcu-common"
build_autotools "$SRC_DIR/lttng-ust-2.13.8" lttng-ust \
  --disable-numa --disable-examples

# --- popt 1.19 + libxml2 2.9.14 + lttng-tools 2.13.15 (lttng CLI/sessiond;
# liblttng-ctl is needed by ROS 2 lttngpy) -----------------------------------
fetch http://ftp.rpm.org/popt/releases/popt-1.x/popt-1.19.tar.gz "$SRC_DIR/popt-1.19.tar.gz"
extract "$SRC_DIR/popt-1.19.tar.gz" "$SRC_DIR" "$SRC_DIR/popt-1.19"
build_autotools "$SRC_DIR/popt-1.19" popt

fetch https://download.gnome.org/sources/libxml2/2.9/libxml2-2.9.14.tar.xz "$SRC_DIR/libxml2-2.9.14.tar.xz"
extract "$SRC_DIR/libxml2-2.9.14.tar.xz" "$SRC_DIR" "$SRC_DIR/libxml2-2.9.14"
build_autotools "$SRC_DIR/libxml2-2.9.14" libxml2 \
  --without-python --without-lzma --without-iconv

# Every PKG_CHECK_MODULES in lttng-tools' configure is bypassed via *_CFLAGS/
# *_LIBS env vars (host WDAC blocks any pkg-config binary).
fetch https://lttng.org/files/lttng-tools/lttng-tools-2.13.15.tar.bz2 "$SRC_DIR/lttng-tools-2.13.15.tar.bz2"
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

echo "target deps installed into $PREFIX"
