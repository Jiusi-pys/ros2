#!/usr/bin/env bash
# Exercise the actual case function without starting or modifying a board.
set -euo pipefail
cd "$(dirname "$0")/.."
tmp_base="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
LOGDIR="$(mktemp -d "$tmp_base/ros2-case-contract.XXXXXX")"
case "$LOGDIR" in "$tmp_base"/ros2-case-contract.*) ;; *) exit 70 ;; esac
cleanup() { case "$LOGDIR" in "$tmp_base"/ros2-case-contract.*) rm -rf -- "$LOGDIR" ;; esac; }
trap cleanup EXIT
eval "$(sed -n '/^run_case() {/,/^}/p' scripts/run_ohos_generic_acceptance.sh)"
declare -A BOARD_PRODUCT=([A]=KaihongOS)
declare -A BOARD_VERSION=([A]=6.1.0.04)
declare -A BOARD_ARCH=([A]=aarch64)
ROS2_RUN_ID=host_contract
DEVICE_DIR=/not-executed
DOMAIN=181
CASE_TIMEOUT=1
HDC=not-executed
EXPECTED_RMW=rmw_fastrtps_cpp
SOURCE_SHA=source
SDK_SHA=sdk
ARCHIVE_SHA=archive
PROVENANCE_SHA=provenance
terminal_mode=valid
timeout() {
  if [[ "$terminal_mode" == valid ]]; then
    printf 'ROS2_CASE_TERMINAL RUN_ID=host_contract CASE=probe BOARD=A PRODUCT=KaihongOS VERSION=6.1.0.04 ARCH=aarch64 RC=0 RMW=rmw_fastrtps_cpp SOURCE_SHA256=source SDK_SHA256=sdk ARCHIVE_SHA256=archive PROVENANCE_SHA256=provenance\n'
  else
    printf 'HDC transport returned without a valid remote terminal\n'
  fi
}
record_pass() { [[ "$1" == probe@A && "$2" == A.probe.log ]]; }
# Neither variable may be borrowed from a caller's dynamic scope. Combining
# dependent local assignments fails under nounset before the remote call.
unset board name
run_case A probe true
test -s "$LOGDIR/A.probe.log"
terminal_mode=missing
if run_case A rejected true; then
  echo 'ERROR: missing remote terminal was accepted' >&2
  exit 1
fi
echo 'generic acceptance case contract: PASS'
