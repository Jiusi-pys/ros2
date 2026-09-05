#!/usr/bin/env bash
# Build a relocatable CPython 3.12.7 runtime for KaihongOS/OpenHarmony.
set -euo pipefail

# Resolve repository helpers before changing directories.  BASH_SOURCE may be
# relative when this recipe is launched from a clean checkout.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<'EOF'
Usage: rebuild_cpython_ohos.sh \
  --source DIR --host-python FILE --toolchain DIR --sysroot DIR \
  --deps-prefix DIR --output DIR

All inputs must already have been verified against source_build.lock.json.
The output directory is create-only.  The packaged runtime is written below
OUTPUT/runtime/usr and retains the production prefix /data/python312-rk3588a/usr
inside CPython's build metadata.
EOF
}

source_dir=
host_python=
toolchain=
sysroot=
deps=
output=
while (($#)); do
  case "$1" in
    --source) source_dir="${2:?missing --source value}"; shift 2 ;;
    --host-python) host_python="${2:?missing --host-python value}"; shift 2 ;;
    --toolchain) toolchain="${2:?missing --toolchain value}"; shift 2 ;;
    --sysroot) sysroot="${2:?missing --sysroot value}"; shift 2 ;;
    --deps-prefix) deps="${2:?missing --deps-prefix value}"; shift 2 ;;
    --output) output="${2:?missing --output value}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

for value in source_dir host_python toolchain sysroot deps output; do
  test -n "${!value}" || { echo "missing required argument: $value" >&2; exit 2; }
done

source_dir="$(realpath "$source_dir")"
host_python="$(realpath "$host_python")"
toolchain="$(realpath "$toolchain")"
sysroot="$(realpath "$sysroot")"
deps="$(realpath "$deps")"
output="$(realpath -m "$output")"

test -f "$source_dir/configure"
test -f "$source_dir/config.site"
test -x "$host_python"
test -x "$toolchain/bin/clang"
test -d "$sysroot/usr/include"
test -f "$deps/lib/libffi.so.8.3.1"
test -f "$deps/lib/liblzma.so.5.8.3"
test -f "$deps/lib/libbz2.so.1.0.8"
test -f "$deps/lib/libsqlite3.so.0.8.6"
test -f "$deps/lib/libssl.so.3"
test -f "$deps/lib/libcrypto.so.3"
test ! -e "$output"

build_dir="$output/build"
destdir="$output/destdir"
runtime="$output/runtime/usr"
production_prefix=/data/python312-rk3588a/usr
mkdir -p "$build_dir"
cd "$build_dir"

# Do not let caller-provided build flags or Python configuration leak into the
# cross build.  Every accepted flag is set explicitly below and recorded.
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LIBS CONFIG_SITE PKG_CONFIG_PATH \
  PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR PYTHONHOME PYTHONPATH \
  _PYTHON_SYSCONFIGDATA_NAME CC CXX AR RANLIB READELF
export PYTHONDONTWRITEBYTECODE=1
export CONFIG_SITE="$source_dir/config.site"
export CC="$toolchain/bin/clang --target=aarch64-linux-ohos --sysroot=$sysroot -fuse-ld=lld"
export CXX="$toolchain/bin/clang++ --target=aarch64-linux-ohos --sysroot=$sysroot -fuse-ld=lld"
export AR="$toolchain/bin/llvm-ar"
export RANLIB="$toolchain/bin/llvm-ranlib"
export READELF="$toolchain/bin/llvm-readelf"
export CPPFLAGS="-D__MUSL__ -I$deps/include"
path_maps="-ffile-prefix-map=$source_dir=/usr/src/Python-3.12.7 -ffile-prefix-map=$output=/usr/src/python-ohos-build -fmacro-prefix-map=$source_dir=/usr/src/Python-3.12.7 -fmacro-prefix-map=$output=/usr/src/python-ohos-build"
export CFLAGS="-O2 -g0 -fPIC $path_maps"
export CXXFLAGS="-O2 -g0 -fPIC $path_maps"
export LDFLAGS="-L$deps/lib"
export PKG_CONFIG_LIBDIR="$deps/lib/pkgconfig:$deps/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=/
unset PKG_CONFIG_PATH
export LIBFFI_CFLAGS="-I$deps/include"
export LIBFFI_LIBS="-L$deps/lib -lffi"
export BZIP2_CFLAGS="-I$deps/include"
export BZIP2_LIBS="-L$deps/lib -lbz2"
export LIBLZMA_CFLAGS="-I$deps/include"
export LIBLZMA_LIBS="-L$deps/lib -llzma"
export LIBSQLITE3_CFLAGS="-I$deps/include"
export LIBSQLITE3_LIBS="-L$deps/lib -lsqlite3"
export OPENSSL_CFLAGS="-I$deps/include"
export OPENSSL_LDFLAGS="-L$deps/lib"
export OPENSSL_LIBS="-lssl -lcrypto"

env | grep -E '^(CONFIG_SITE|CC|CXX|AR|RANLIB|READELF|CPPFLAGS|CFLAGS|CXXFLAGS|LDFLAGS|PKG_CONFIG_|LIBFFI_|BZIP2_|LIBLZMA_|LIBSQLITE3_|OPENSSL_)=' \
  | sort > build-env.txt

"$source_dir/configure" \
  --host=aarch64-unknown-linux-ohos \
  --build=x86_64-linux-gnu \
  --with-build-python="$host_python" \
  --prefix="$production_prefix" \
  --enable-shared \
  --without-ensurepip \
  --with-pkg-config=yes \
  --with-openssl="$deps" \
  --with-openssl-rpath=no \
  > configure.log 2>&1

cp Makefile Makefile.before-cross-install-adjustment
sed -i 's/ scripts checksharedmods rundsymutil/ scripts rundsymutil/' Makefile
sed -i 's/sharedinstall: all/sharedinstall: sharedmods/' Makefile
python3 - Makefile "$host_python" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
old = "\t\t-DVPATH='\"$(VPATH)\"' \\\n"
new = "\t\t-DVPATH='\"/usr/src/Python-3.12.7\"' \\\n"
if text.count(old) != 1:
    raise SystemExit("CPython getpath VPATH recipe marker not found exactly once")
text = text.replace(old, new)
lines = text.splitlines(keepends=True)
matches = [
    index
    for index, line in enumerate(lines)
    if line.startswith("PYTHON_FOR_BUILD=") and line.rstrip().endswith(sys.argv[2])
]
if len(matches) != 1:
    raise SystemExit("target Makefile PYTHON_FOR_BUILD marker not found exactly once")
index = matches[0]
lines[index] = lines[index].rstrip("\n") + " -B\n"
for variable in ("PYTHON_FOR_FREEZE=", "PYTHON_FOR_REGEN?="):
    matches = [
        index
        for index, line in enumerate(lines)
        if line.startswith(variable) and line.rstrip().endswith(sys.argv[2])
    ]
    if len(matches) != 1:
        raise SystemExit(f"target Makefile {variable.rstrip('=?')} marker not found exactly once")
    index = matches[0]
    lines[index] = lines[index].rstrip("\n") + " -B\n"
path.write_text("".join(lines), encoding="utf-8")
PY
diff -u Makefile.before-cross-install-adjustment Makefile > makefile-cross-adjustment.patch || true

make -j"$(nproc)" > build.log 2>&1
make DESTDIR="$destdir" install > install.log 2>&1

installed="$destdir$production_prefix"
test -x "$installed/bin/python3.12"
mkdir -p "$runtime"
cp -a "$installed/." "$runtime/"

# Bundle every non-system DSO used by the enabled stdlib extensions, including
# the exact SONAME links recorded by DT_NEEDED.  No host or legacy board files
# are accepted here.
cp -a \
  "$deps/lib/libffi.so" "$deps/lib/libffi.so.8" "$deps/lib/libffi.so.8.3.1" \
  "$deps/lib/liblzma.so" "$deps/lib/liblzma.so.5" "$deps/lib/liblzma.so.5.8.3" \
  "$deps/lib/libbz2.so" "$deps/lib/libbz2.so.1" "$deps/lib/libbz2.so.1.0" \
  "$deps/lib/libbz2.so.1.0.8" \
  "$deps/lib/libsqlite3.so" "$deps/lib/libsqlite3.so.0" \
  "$deps/lib/libsqlite3.so.0.8.6" \
  "$deps/lib/libssl.so" "$deps/lib/libssl.so.3" \
  "$deps/lib/libcrypto.so" "$deps/lib/libcrypto.so.3" \
  "$runtime/lib/"

# Fail closed for OpenSSL configuration and trust discovery.  The deployment
# environment points only at these runtime-owned paths, so an isolated prefix
# cannot consult the legacy /data/python312-rk3588a installation.  The empty CA
# bundle intentionally provides no ambient public trust; board validation loads
# its test CA explicitly.
mkdir -p "$runtime/etc/ssl/certs" "$runtime/lib/ossl-modules"
cat > "$runtime/etc/ssl/openssl.cnf" <<'EOF'
# Deliberately minimal runtime-owned OpenSSL configuration.
openssl_conf = openssl_init

[openssl_init]
EOF
: > "$runtime/etc/ssl/cert.pem"

python3 "$script_dir/normalize_cpython_metadata.py" \
  --runtime "$runtime" \
  --source "$source_dir" \
  --build "$build_dir" \
  --host-python "$host_python" \
  --toolchain "$toolchain" \
  --sysroot "$sysroot" \
  --deps-prefix "$deps" \
  --destdir "$destdir" \
  --llvm-strip "$toolchain/bin/llvm-strip" \
  > "$output/metadata-normalization.log"

printf '%s\n' "$production_prefix" > "$output/production-prefix.txt"
printf 'runtime_usr=%s\n' "$runtime"
