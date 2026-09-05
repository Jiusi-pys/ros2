#!/usr/bin/env bash
set -euo pipefail

if (($# != 4)); then
  echo "usage: prepare_cpython_source.sh CPYTHON_TAR JIUSI_CHECKOUT PATCH OUTPUT_DIR" >&2
  exit 2
fi

archive="$(realpath "$1")"
jiusi="$(realpath "$2")"
patch="$(realpath "$3")"
output="$(realpath -m "$4")"

verify() {
  local path="$1" expected="$2" actual
  actual="$(sha256sum "$path" | cut -d' ' -f1)"
  test "$actual" = "$expected" || {
    echo "SHA-256 mismatch for $path: expected $expected, got $actual" >&2
    exit 1
  }
}

verify "$archive" 24887b92e2afd4a2ac602419ad4b596372f67ac9b077190f459aba390faf5550
verify "$jiusi/Python-3.12.12/config.sub" c2d7579743cdc855c42c8ba03b94e761182d9539ebb832e81559346e47e44a1a
verify "$jiusi/Python-3.12.12/config.site" c7b23c773c190ccd13a35de1b149fd2bfc947df2365104ce1f1fb6c6766b76f7
verify "$patch" 01d07dc7ae46b26ed0b2da9fcbd042e9e770d18187e585e429f9c886491cdd29
test "$(git -C "$jiusi" rev-parse HEAD)" = 098f200c30f1f35051d73041e6acbad46af60904
test ! -e "$output"

mkdir -p "$output"
tar -xJf "$archive" -C "$output" --strip-components=1
install -m 0755 "$jiusi/Python-3.12.12/config.sub" "$output/config.sub"
install -m 0644 "$jiusi/Python-3.12.12/config.site" "$output/config.site"
patch -d "$output" -p1 --fuzz=0 < "$patch"

verify "$output/config.sub" c2d7579743cdc855c42c8ba03b94e761182d9539ebb832e81559346e47e44a1a
verify "$output/config.site" c7b23c773c190ccd13a35de1b149fd2bfc947df2365104ce1f1fb6c6766b76f7
echo "cpython_source=$output"
