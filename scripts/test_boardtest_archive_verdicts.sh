#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

TMP_BASE="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
TMP_ROOT="$(mktemp -d "$TMP_BASE/boardtest-verdicts.XXXXXX")"
case "$TMP_ROOT" in "$TMP_BASE"/boardtest-verdicts.*) ;; *) exit 70 ;; esac
cleanup() {
  case "$TMP_ROOT" in "$TMP_BASE"/boardtest-verdicts.*) rm -rf -- "$TMP_ROOT" ;; esac
}
trap cleanup EXIT
LOGDIR="$TMP_ROOT/log"
mkdir -p "$LOGDIR"

local_archive_tar() { tar --force-local "$@"; }
# Load exactly the verifier under test without running the board orchestrator.
eval "$(sed -n '/^verify_archive_verdicts()/,/^}/p' scripts/run_board_tests.sh)"

make_case() { # <directory>
  local root="$1" pkg
  pkg="$root/demo_pkg"
  mkdir -p "$pkg/.boardtest-verdicts"
  printf '%s\n' \
    '# BOARDTEST_EXPECTED first_case' \
    '# BOARDTEST_EXPECTED second_case' \
    '# BOARDTEST_XML second_case' \
    > "$root/driver.sh"
  printf '%s\n' \
    'BOARDTEST first_case PASS rc=0' \
    'BOARDTEST second_case PASS rc=0' \
    > "$root/stdout.log"
  printf '%s\n' 'BOARDTEST second_case PASS rc=0' \
    > "$pkg/.boardtest-verdicts/second_case"
  printf '%s\n' 'BOARDTEST first_case PASS rc=0' \
    > "$pkg/.boardtest-verdicts/first_case"
  printf 'raw privileged\n' > "$pkg/second_case.log"
  printf 'raw unprivileged\n' > "$pkg/first_case.log"
  printf '<testsuites tests="1" failures="0"/>\n' > "$pkg/second_case.xml"
}

archive_case() { # <directory>
  rm -f "$1/evidence.tar"
  tar -C "$1" -cf "$1/evidence.tar" demo_pkg
}

valid="$TMP_ROOT/valid"
mkdir -p "$valid"
make_case "$valid"
archive_case "$valid"
verify_archive_verdicts demo_pkg "$valid/evidence.tar" \
  "$valid/stdout.log" "$valid/driver.sh" >/dev/null

crlf_transport="$TMP_ROOT/crlf_transport"
cp -a "$valid" "$crlf_transport"
printf '%s\r\n' \
  'BOARDTEST first_case PASS rc=0' \
  'BOARDTEST second_case PASS rc=0' \
  > "$crlf_transport/stdout.log"
archive_case "$crlf_transport"
verify_archive_verdicts demo_pkg "$crlf_transport/evidence.tar" \
  "$crlf_transport/stdout.log" "$crlf_transport/driver.sh" >/dev/null

embedded_cr="$TMP_ROOT/embedded_cr"
cp -a "$valid" "$embedded_cr"
printf 'BOARDTEST first_case PASS\rrc=0\n%s\n' \
  'BOARDTEST second_case PASS rc=0' > "$embedded_cr/stdout.log"
archive_case "$embedded_cr"
if verify_archive_verdicts demo_pkg "$embedded_cr/evidence.tar" \
    "$embedded_cr/stdout.log" "$embedded_cr/driver.sh" >/dev/null 2>&1; then
  echo "ERROR: embedded transport CR was accepted" >&2
  exit 1
fi

double_cr="$TMP_ROOT/double_cr"
cp -a "$valid" "$double_cr"
printf '%s\r\r\n%s\n' \
  'BOARDTEST first_case PASS rc=0' \
  'BOARDTEST second_case PASS rc=0' > "$double_cr/stdout.log"
archive_case "$double_cr"
if verify_archive_verdicts demo_pkg "$double_cr/evidence.tar" \
    "$double_cr/stdout.log" "$double_cr/driver.sh" >/dev/null 2>&1; then
  echo "ERROR: double trailing transport CR was accepted" >&2
  exit 1
fi

missing_xml="$TMP_ROOT/missing_xml"
cp -a "$valid" "$missing_xml"
rm -f "$missing_xml/demo_pkg/second_case.xml"
archive_case "$missing_xml"
if verify_archive_verdicts demo_pkg "$missing_xml/evidence.tar" \
    "$missing_xml/stdout.log" "$missing_xml/driver.sh" >/dev/null 2>&1; then
  echo "ERROR: missing planned XML was accepted" >&2
  exit 1
fi

orphan_xml="$TMP_ROOT/orphan_xml"
cp -a "$valid" "$orphan_xml"
printf '<orphan/>\n' > "$orphan_xml/demo_pkg/orphan.xml"
archive_case "$orphan_xml"
if verify_archive_verdicts demo_pkg "$orphan_xml/evidence.tar" \
    "$orphan_xml/stdout.log" "$orphan_xml/driver.sh" >/dev/null 2>&1; then
  echo "ERROR: orphan XML was accepted" >&2
  exit 1
fi

different_record="$TMP_ROOT/different_record"
cp -a "$valid" "$different_record"
printf '%s\n' 'BOARDTEST second_case FAIL rc=9' \
  > "$different_record/demo_pkg/.boardtest-verdicts/second_case"
archive_case "$different_record"
if verify_archive_verdicts demo_pkg "$different_record/evidence.tar" \
    "$different_record/stdout.log" "$different_record/driver.sh" >/dev/null 2>&1; then
  echo "ERROR: stdout/archive verdict mismatch was accepted" >&2
  exit 1
fi

reordered="$TMP_ROOT/reordered"
cp -a "$valid" "$reordered"
printf '%s\n' \
  'BOARDTEST second_case PASS rc=0' \
  'BOARDTEST first_case PASS rc=0' \
  > "$reordered/stdout.log"
archive_case "$reordered"
if verify_archive_verdicts demo_pkg "$reordered/evidence.tar" \
    "$reordered/stdout.log" "$reordered/driver.sh" >/dev/null 2>&1; then
  echo "ERROR: reordered test verdicts were accepted" >&2
  exit 1
fi

echo "boardtest archive verdict/XML negative tests: PASS"
