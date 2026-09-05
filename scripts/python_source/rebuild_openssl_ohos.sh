#!/usr/bin/env bash
set -euo pipefail

if (($# != 5)); then
  echo "usage: rebuild_openssl_ohos.sh ARCHIVE TOOLCHAIN SYSROOT OUTPUT EXPECTED_SHA256" >&2
  exit 2
fi

archive="$(realpath "$1")"
toolchain="$(realpath "$2")"
sysroot="$(realpath "$3")"
output="$(realpath -m "$4")"
expected_sha="$5"
production_prefix=/data/python312-rk3588a/usr

test ! -e "$output"
actual_sha="$(sha256sum "$archive" | cut -d' ' -f1)"
test "$actual_sha" = "$expected_sha" || {
  echo "OpenSSL archive SHA-256 mismatch: expected $expected_sha, got $actual_sha" >&2
  exit 1
}
mkdir -p "$output/source" "$output/destdir"
tar -xzf "$archive" -C "$output/source" --strip-components=1

cc="$toolchain/bin/clang --target=aarch64-linux-ohos --sysroot=$sysroot -D__MUSL__"
(
  cd "$output/source"
  env CC="$cc" AR="$toolchain/bin/llvm-ar" RANLIB="$toolchain/bin/llvm-ranlib" \
    STRIP="$toolchain/bin/llvm-strip" \
    ./Configure linux-aarch64 shared no-tests no-ui-console \
      --prefix="$production_prefix" --libdir=lib \
      --openssldir="$production_prefix/etc/ssl" \
      > "$output/configure.log" 2>&1
  make -j"$(nproc)" build_sw > "$output/build.log" 2>&1
  make DESTDIR="$output/destdir" install_sw > "$output/install.log" 2>&1
)

installed="$output/destdir$production_prefix"
test -f "$installed/lib/libssl.so.3"
test -f "$installed/lib/libcrypto.so.3"
"$toolchain/bin/llvm-strip" --strip-debug "$installed/lib/libssl.so.3"
"$toolchain/bin/llvm-strip" --strip-debug "$installed/lib/libcrypto.so.3"
if strings -a "$installed/lib/libcrypto.so.3" | grep -F "$output"; then
  echo "OpenSSL output contains its ephemeral build root" >&2
  exit 1
fi
sha256sum "$installed/lib/libssl.so.3" "$installed/lib/libcrypto.so.3"
echo "openssl_usr=$installed"
