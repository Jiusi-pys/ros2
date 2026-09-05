#!/usr/bin/env bash
set -euo pipefail

if (($# != 3)); then
  echo "usage: verify_cpython_runtime.sh RUNTIME_USR BUILD_DIR LLVM_READELF" >&2
  exit 2
fi

runtime="$(realpath "$1")"
build="$(realpath "$2")"
readelf="$3"
test -x "$readelf"
dynload="$runtime/lib/python3.12/lib-dynload"

test -x "$runtime/bin/python3.12"
test -f "$runtime/lib/libpython3.12.so.1.0"
test -f "$runtime/include/python3.12/Python.h"

sha256sum "$runtime/bin/python3.12" "$runtime/lib/libpython3.12.so.1.0"
grep -E '^(VERSION|SOABI|LDVERSION|MULTIARCH|EXT_SUFFIX)[[:space:]]*=' "$build/Makefile"
grep -E 'checking for stdlib extension module' "$build/configure.log" \
  | grep -E '(_ctypes|_socket|_sqlite3|zlib|_bz2|_lzma|_ssl|_hashlib)'
grep -q '^#define ENABLE_IPV6 1' "$build/pyconfig.h"

extension_count="$(find "$dynload" -maxdepth 1 -type f -name '*.so' | wc -l)"
test "$extension_count" -ge 70
echo "extension_count=$extension_count"

for module in _ctypes _socket select _posixsubprocess zlib _bz2 _lzma _sqlite3 _ssl _hashlib; do
  matches=("$dynload/$module".*.so)
  test "${#matches[@]}" -eq 1
  test -f "${matches[0]}"
  header="$($readelf -h "${matches[0]}")"
  grep -q 'Machine:.*AArch64' <<<"$header"
  dynamic="$($readelf -d "${matches[0]}")"
  if grep -E 'RIGIN|/var/tmp|\(RPATH\)|\(RUNPATH\)' <<<"$dynamic"; then
    echo "embedded build RPATH/RUNPATH: ${matches[0]}" >&2
    exit 1
  fi
  echo "module=$module"
  grep -E 'NEEDED' <<<"$dynamic" || true
done

for dso in \
  libffi.so.8.3.1 liblzma.so.5.8.3 libbz2.so.1.0.8 \
  libsqlite3.so.0.8.6 libssl.so.3 libcrypto.so.3; do
  test -f "$runtime/lib/$dso"
  header="$($readelf -h "$runtime/lib/$dso")"
  grep -q 'Machine:.*AArch64' <<<"$header"
done

for link in \
  libffi.so:libffi.so.8.3.1 libffi.so.8:libffi.so.8.3.1 \
  liblzma.so:liblzma.so.5.8.3 liblzma.so.5:liblzma.so.5.8.3 \
  libbz2.so:libbz2.so.1 libbz2.so.1:libbz2.so.1.0 \
  libbz2.so.1.0:libbz2.so.1.0.8 \
  libsqlite3.so:libsqlite3.so.0 libsqlite3.so.0:libsqlite3.so.0.8.6 \
  libssl.so:libssl.so.3 libcrypto.so:libcrypto.so.3; do
  name="${link%%:*}"
  target="${link#*:}"
  test -L "$runtime/lib/$name"
  test "$(readlink "$runtime/lib/$name")" = "$target"
done

test "$(find "$runtime" -type f -name '*.pyc' | wc -l)" -eq 0

# OpenSSL configuration paths are consulted at runtime.  They must point only
# into the final runtime, while the compiler identity is merely provenance.
crypto_strings="$(strings -a "$runtime/lib/libcrypto.so.3")"
grep -Fq 'OPENSSLDIR: "/data/python312-rk3588a/usr/etc/ssl"' <<<"$crypto_strings"
grep -Fq 'MODULESDIR: "/data/python312-rk3588a/usr/lib/ossl-modules"' <<<"$crypto_strings"
# OpenSSL exposes its compile command as provenance.  That string is not a
# runtime lookup path; reject ephemeral directories only from the actual
# OPENSSLDIR/MODULESDIR/ENGINESDIR records above and dynamic loader metadata.
if grep -E '^(OPENSSLDIR|MODULESDIR|ENGINESDIR):.*(/var/tmp|/tmp/)' <<<"$crypto_strings"; then
  echo "OpenSSL retained an ephemeral operational directory" >&2
  exit 1
fi

# Operational installation metadata must use the final board prefix.  Build
# provenance may remain in CONFIG_ARGS/flags, but no executable or pkg-config
# prefix may point at the ephemeral build root.
grep -q '^prefix="/data/python312-rk3588a/usr"' "$runtime/bin/python3.12-config"
for pc in "$runtime"/lib/pkgconfig/*.pc; do
  grep -q '^prefix=/data/python312-rk3588a/usr$' "$pc"
done
head -1 "$runtime/bin/pydoc3.12" | grep -q '^#!/data/python312-rk3588a/usr/bin/python3.12$'

echo "VERDICT=PASS"
