#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

tmp_base="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
tmp_root="$(mktemp -d "$tmp_base/ros2-generic-env-test.XXXXXX")"
case "$tmp_root" in "$tmp_base"/ros2-generic-env-test.*) ;; *) exit 70 ;; esac
cleanup() {
  case "$tmp_root" in "$tmp_base"/ros2-generic-env-test.*) rm -rf -- "$tmp_root" ;; esac
}
trap cleanup EXIT

template="$PWD/scripts/env_ohos_generic.template.sh"
# Finalization canonicalizes the Windows install directory to Lib. The board
# filesystem is case-sensitive, including the consumer spawned by sessiond.
grep -Fxq 'export LTTNG_CONSUMERD64_BIN="$ROS2_HOME/Lib/lttng/libexec/lttng-consumerd"' "$template"
grep -Fxq 'export LTTNG_CONSUMERD64_LIBDIR="$ROS2_HOME/Lib"' "$template"

expect_rejected() {
  local expected="$1" case_dir="$2" output="$3" rc=0
  shift 3
  env -i PATH="$PATH" ROS2_HOME="$case_dir" "$@" sh -c \
    '. "$1" || exit $?; printf UNEXPECTED_ACCEPT' sh "$template" >"$output" 2>&1 || rc=$?
  if [[ "$rc" -ne 70 ]]; then
    echo "ERROR: expected env rejection rc=70, got rc=$rc" >&2
    cat "$output" >&2
    exit 1
  fi
  grep -Fq "$expected" "$output"
  if grep -Fq UNEXPECTED_ACCEPT "$output"; then
    echo "ERROR: rejected environment continued into the payload" >&2
    exit 1
  fi
}

mkdir -p "$tmp_root/missing"
expect_rejected "incomplete or mixed generic ROS 2 deployment" \
  "$tmp_root/missing" "$tmp_root/missing.out" \
  RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
  ROS2_DEPLOY_EXPECTED_MARKER=expected \
  ROS2_RELEASE_PROVENANCE_SHA256="$(printf missing | sha256sum | cut -d ' ' -f1)"

accepted="$tmp_root/accepted"
python_root="$tmp_root/python/usr"
mkdir -p "$accepted" "$python_root/bin" "$python_root/lib" \
  "$accepted/Lib/site-packages" "$accepted/Lib/demo_nodes_cpp" "$accepted/Lib/demo_nodes_py"
printf '%s\n' expected > "$accepted/.ros2_deploy_complete"
printf '%s\n' provenance > "$accepted/release_provenance.json"
printf '#!/bin/sh\nexit 0\n' > "$python_root/bin/python3.12"
chmod +x "$python_root/bin/python3.12"
printf 'fake-libpython\n' > "$python_root/lib/libpython3.12.so.1.0"
provenance_sha="$(sha256sum "$accepted/release_provenance.json" | cut -d ' ' -f1)"

expect_rejected "clean-build receipt is missing or has the wrong digest" \
  "$accepted" "$tmp_root/receipt.out" \
  RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
  ROS2_DEPLOY_EXPECTED_MARKER=expected \
  ROS2_RELEASE_PROVENANCE_SHA256="$provenance_sha"
printf '%s\n' receipt > "$accepted/build_receipt.json"
receipt_sha="$(sha256sum "$accepted/build_receipt.json" | cut -d ' ' -f1)"

expect_rejected "deployment did not pin an accepted generic RMW implementation" \
  "$accepted" "$tmp_root/rmw.out" \
  ROS2_PYTHON_ROOT="$python_root" \
  RMW_IMPLEMENTATION=rmw_mdds_cpp \
  ROS2_DEPLOY_EXPECTED_MARKER=expected \
  ROS2_BUILD_RECEIPT_SHA256="$receipt_sha" \
  ROS2_RELEASE_PROVENANCE_SHA256="$provenance_sha"

expect_rejected "invalid provenance-bound Python runtime prefix" \
  "$accepted" "$tmp_root/python_prefix.out" \
  ROS2_PYTHON_REMOTE_PREFIX='/data/valid/../../other' \
  RMW_IMPLEMENTATION=rmw_fastrtps_cpp \
  ROS2_DEPLOY_EXPECTED_MARKER=expected \
  ROS2_BUILD_RECEIPT_SHA256="$receipt_sha" \
  ROS2_RELEASE_PROVENANCE_SHA256="$provenance_sha"

# Successful import is deliberately exercised with the real target interpreter
# and real markers by the board acceptance suite, not a fake exit-0 interpreter.
echo "generic OHOS environment rejection contract: PASS"
