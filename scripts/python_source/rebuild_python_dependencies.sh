#!/usr/bin/env bash
set -euo pipefail

if (($# != 4)); then
  echo "usage: rebuild_python_dependencies.sh OUTPUT_ROOT TOOLCHAIN SYSROOT OHOS_CONFIG_SUB" >&2
  exit 2
fi

ROOT="$(realpath -m "$1")"
TOOLCHAIN="$(realpath "$2")"
SYSROOT="$(realpath "$3")"
OHOS_CONFIG_SUB="$(realpath "$4")"
DOWNLOADS="$ROOT/downloads"
SOURCES="$ROOT/src"
PREFIX="$ROOT/prefix"
TARGET=aarch64-linux-ohos
CLANG="$TOOLCHAIN/bin/clang"
CLANGXX="$TOOLCHAIN/bin/clang++"
AR="$TOOLCHAIN/bin/llvm-ar"
RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
STRIP="$TOOLCHAIN/bin/llvm-strip"
TARGET_CC="$CLANG --target=$TARGET --sysroot=$SYSROOT"
TARGET_CXX="$CLANGXX --target=$TARGET --sysroot=$SYSROOT"
OHOS_CONFIG_SUB_SHA256=c2d7579743cdc855c42c8ba03b94e761182d9539ebb832e81559346e47e44a1a
PRODUCTION_PREFIX=/data/python312-rk3588a/usr

# The dependency recipe owns ROOT.  It may contain only a caller-populated,
# hash-checked download cache at entry; silently reusing a prior build/prefix
# would make an "empty workspace" receipt meaningless.
if [ -e "$ROOT" ]; then
  unexpected="$(find "$ROOT" -mindepth 1 -maxdepth 1 ! -name downloads -print -quit)"
  test -z "$unexpected" || {
    echo "dependency output root is not clean: $unexpected" >&2
    exit 1
  }
else
  mkdir -p "$ROOT/downloads"
fi
test -d "$ROOT/downloads"
unset CC CXX CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LIBS \
  CONFIG_SITE PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR \
  PYTHONHOME PYTHONPATH _PYTHON_SYSCONFIGDATA_NAME

verify_sha256() {
  local file="$1"
  local expected="$2"
  local actual
  actual="$(sha256sum "$file" | cut -d' ' -f1)"
  if [ "$actual" != "$expected" ]; then
    echo "SHA-256 mismatch for $file: expected $expected, got $actual" >&2
    exit 1
  fi
}

verify_sha256 "$DOWNLOADS/libffi-3.6.0.tar.gz" 31ff1fe32deaebfbb388727f32677bb254bf2a41382c51464c0b1837c9ee9828
verify_sha256 "$DOWNLOADS/xz-5.8.3.tar.xz" fff1ffcf2b0da84d308a14de513a1aa23d4e9aa3464d17e64b9714bfdd0bbfb6
verify_sha256 "$DOWNLOADS/bzip2-1.0.8.tar.gz" ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
verify_sha256 "$DOWNLOADS/sqlite-amalgamation-3370200.zip" cb25df0fb90b77be6660f6ace641bbea88f3d0441110d394ce418f35f7561bb0
verify_sha256 "$DOWNLOADS/openssl-3.0.16.tar.gz" 57e03c50feab5d31b152af2b764f10379aecd8ee92f16c985983ce4a99f7ef86
verify_sha256 "$OHOS_CONFIG_SUB" "$OHOS_CONFIG_SUB_SHA256"

mkdir -p "$SOURCES" "$PREFIX/include" "$PREFIX/lib" "$PREFIX/share/provenance"

tar -xzf "$DOWNLOADS/libffi-3.6.0.tar.gz" -C "$SOURCES"
tar -xJf "$DOWNLOADS/xz-5.8.3.tar.xz" -C "$SOURCES"
tar -xzf "$DOWNLOADS/bzip2-1.0.8.tar.gz" -C "$SOURCES"
python3 -m zipfile -e "$DOWNLOADS/sqlite-amalgamation-3370200.zip" "$SOURCES"
tar -xzf "$DOWNLOADS/openssl-3.0.16.tar.gz" -C "$SOURCES"

if [ ! -f "$PREFIX/lib/libffi.so" ]; then
  test ! -e "$ROOT/build-libffi"
  mkdir "$ROOT/build-libffi"
  # libffi 3.6.0 predates the OpenHarmony triplet.  Reuse the exact
  # Jiusi-pys/CPython config.sub that is already a pinned source input.
  install -m 0755 "$OHOS_CONFIG_SUB" "$SOURCES/libffi-3.6.0/config.sub"
  (
    cd "$ROOT/build-libffi"
    env CC="$TARGET_CC" CXX="$TARGET_CXX" AR="$AR" RANLIB="$RANLIB" \
      STRIP="$STRIP" CFLAGS="-O2 -fPIC -D__MUSL__ -ffile-prefix-map=$ROOT=/usr/src/python-ohos-deps" \
      "$SOURCES/libffi-3.6.0/configure" \
      --host="$TARGET" --build=x86_64-linux-gnu \
      --prefix="$PREFIX" --libdir="$PREFIX/lib" \
      --enable-shared --disable-static --disable-docs \
      >configure.log 2>&1
    make -j8 >build.log 2>&1
    make install >install.log 2>&1
  )
fi

if [ ! -f "$PREFIX/lib/liblzma.so" ]; then
  test ! -e "$ROOT/build-xz"
  mkdir "$ROOT/build-xz"
  (
    cd "$ROOT/build-xz"
    env CC="$TARGET_CC" CXX="$TARGET_CXX" AR="$AR" RANLIB="$RANLIB" \
      STRIP="$STRIP" CFLAGS="-O2 -fPIC -D__MUSL__ -ffile-prefix-map=$ROOT=/usr/src/python-ohos-deps" \
      "$SOURCES/xz-5.8.3/configure" \
      --host="$TARGET" --build=x86_64-linux-gnu \
      --prefix="$PREFIX" --libdir="$PREFIX/lib" \
      --enable-shared --disable-static --disable-doc \
      >configure.log 2>&1
    make -j8 >build.log 2>&1
    make install >install.log 2>&1
  )
fi

if [ ! -f "$PREFIX/lib/libbz2.so" ]; then
  test ! -e "$ROOT/build-bzip2"
  mkdir "$ROOT/build-bzip2"
  bzip_sources=(blocksort huffman crctable randtable compress decompress bzlib)
  bzip_objects=()
  for name in "${bzip_sources[@]}"; do
    "$CLANG" --target="$TARGET" --sysroot="$SYSROOT" -O2 -fPIC -D__MUSL__ \
      "-ffile-prefix-map=$ROOT=/usr/src/python-ohos-deps" \
      -D_FILE_OFFSET_BITS=64 -I"$SOURCES/bzip2-1.0.8" \
      -c "$SOURCES/bzip2-1.0.8/$name.c" -o "$ROOT/build-bzip2/$name.o"
    bzip_objects+=("$ROOT/build-bzip2/$name.o")
  done
  "$CLANG" --target="$TARGET" --sysroot="$SYSROOT" -shared \
    -Wl,-soname,libbz2.so.1.0 -o "$PREFIX/lib/libbz2.so.1.0.8" \
    "${bzip_objects[@]}"
  ln -s libbz2.so.1.0.8 "$PREFIX/lib/libbz2.so.1.0"
  ln -s libbz2.so.1.0 "$PREFIX/lib/libbz2.so.1"
  ln -s libbz2.so.1 "$PREFIX/lib/libbz2.so"
  install -m 0644 "$SOURCES/bzip2-1.0.8/bzlib.h" "$PREFIX/include/bzlib.h"
fi

if [ ! -f "$PREFIX/lib/libsqlite3.so" ]; then
  test ! -e "$ROOT/build-sqlite"
  mkdir "$ROOT/build-sqlite"
  "$CLANG" --target="$TARGET" --sysroot="$SYSROOT" -O2 -fPIC -D__MUSL__ \
    "-ffile-prefix-map=$ROOT=/usr/src/python-ohos-deps" \
    -DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_COLUMN_METADATA \
    -DSQLITE_ENABLE_FTS3 -DSQLITE_ENABLE_FTS4 -DSQLITE_ENABLE_FTS5 \
    -DSQLITE_ENABLE_RTREE -DSQLITE_ENABLE_UNLOCK_NOTIFY \
    -I"$SOURCES/sqlite-amalgamation-3370200" \
    -c "$SOURCES/sqlite-amalgamation-3370200/sqlite3.c" \
    -o "$ROOT/build-sqlite/sqlite3.o"
  "$CLANG" --target="$TARGET" --sysroot="$SYSROOT" -shared \
    -Wl,-soname,libsqlite3.so.0 -o "$PREFIX/lib/libsqlite3.so.0.8.6" \
    "$ROOT/build-sqlite/sqlite3.o" -ldl -lpthread -lm
  ln -s libsqlite3.so.0.8.6 "$PREFIX/lib/libsqlite3.so.0"
  ln -s libsqlite3.so.0 "$PREFIX/lib/libsqlite3.so"
  install -m 0644 "$SOURCES/sqlite-amalgamation-3370200/sqlite3.h" "$PREFIX/include/sqlite3.h"
  install -m 0644 "$SOURCES/sqlite-amalgamation-3370200/sqlite3ext.h" "$PREFIX/include/sqlite3ext.h"
fi

if [ ! -f "$PREFIX/lib/libssl.so" ]; then
  ln -s "$TOOLCHAIN" "$SOURCES/openssl-3.0.16/.ohos-toolchain"
  ln -s "$SYSROOT" "$SOURCES/openssl-3.0.16/.ohos-sysroot"
  (
    cd "$SOURCES/openssl-3.0.16"
    env CC="./.ohos-toolchain/bin/clang --target=$TARGET --sysroot=./.ohos-sysroot -D__MUSL__ -ffile-prefix-map=../..=/usr/src/python-ohos-deps" \
      AR="./.ohos-toolchain/bin/llvm-ar" \
      RANLIB="./.ohos-toolchain/bin/llvm-ranlib" \
      STRIP="./.ohos-toolchain/bin/llvm-strip" \
      ./Configure linux-aarch64 shared no-tests no-ui-console \
      --prefix="$PRODUCTION_PREFIX" --libdir=lib \
      --openssldir="$PRODUCTION_PREFIX/etc/ssl" \
      >"$ROOT/openssl-configure.log" 2>&1
    make -j8 build_sw >"$ROOT/openssl-build.log" 2>&1
    make DESTDIR="$ROOT/openssl-destdir" install_sw >"$ROOT/openssl-install.log" 2>&1
  )
  cp -a "$ROOT/openssl-destdir$PRODUCTION_PREFIX/include/." "$PREFIX/include/"
  cp -a "$ROOT/openssl-destdir$PRODUCTION_PREFIX/lib/." "$PREFIX/lib/"
fi

for library in \
  "$PREFIX/lib/libffi.so.8.3.1" "$PREFIX/lib/liblzma.so.5.8.3" \
  "$PREFIX/lib/libbz2.so.1.0.8" "$PREFIX/lib/libsqlite3.so.0.8.6" \
  "$PREFIX/lib/libssl.so.3" "$PREFIX/lib/libcrypto.so.3"; do
  "$STRIP" --strip-debug "$library"
done

for library in libffi.so liblzma.so libbz2.so libsqlite3.so libssl.so libcrypto.so; do
  test -e "$PREFIX/lib/$library" || {
    echo "missing staged dependency library: $library" >&2
    exit 1
  }
done

sha256sum \
  "$PREFIX/lib/libffi.so.8.3.1" \
  "$PREFIX/lib/liblzma.so.5.8.3" \
  "$PREFIX/lib/libbz2.so.1.0.8" \
  "$PREFIX/lib/libsqlite3.so.0.8.6" \
  "$PREFIX/lib/libssl.so.3" \
  "$PREFIX/lib/libcrypto.so.3" \
  | tee "$ROOT/target-library-sha256.txt"
file \
  "$PREFIX/lib/libffi.so.8.3.1" \
  "$PREFIX/lib/liblzma.so.5.8.3" \
  "$PREFIX/lib/libbz2.so.1.0.8" \
  "$PREFIX/lib/libsqlite3.so.0.8.6" \
  "$PREFIX/lib/libssl.so.3" \
  "$PREFIX/lib/libcrypto.so.3" \
  | tee "$ROOT/target-library-file.txt"

echo PYTHON_SOURCE_DEPENDENCIES_BUILD_OK
