#!/usr/bin/env bash
# Negative contracts that must fail before deploy_ohos.sh mutates install_ohos
# or contacts a board.
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/lib/mdds_sha256_manifest.sh

TMP_BASE="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
TMP_ROOT="$(mktemp -d "$TMP_BASE/mdds-manifest-test.XXXXXX")"
case "$TMP_ROOT" in "$TMP_BASE"/mdds-manifest-test.*) ;; *) exit 70 ;; esac
cleanup() {
  case "$TMP_ROOT" in "$TMP_BASE"/mdds-manifest-test.*) rm -rf -- "$TMP_ROOT" ;; esac
}
trap cleanup EXIT

expect_rc2() { # <expected-fragment> <command...>
  local expected="$1" output rc=0
  shift
  output="$("$@" 2>&1)" || rc=$?
  if [ "$rc" -ne 2 ]; then
    echo "ERROR: expected rc=2, got rc=$rc: $output" >&2
    return 1
  fi
  if ! grep -Fq "$expected" <<< "$output"; then
    echo "ERROR: missing expected diagnostic '$expected': $output" >&2
    return 1
  fi
}

expect_rc2 "RMW must be empty or one safe implementation identifier" \
  env 'RMW=rmw_mdds;echo_INJECTED' bash scripts/deploy_ohos.sh safe_board
expect_rc2 "duplicate board identifier: duplicate_board" \
  bash scripts/deploy_ohos.sh duplicate_board duplicate_board

mkdir "$TMP_ROOT/tree"
printf 'portable-manifest-payload\n' > "$TMP_ROOT/tree/f"
(cd "$TMP_ROOT/tree" && sha256sum ./f) > "$TMP_ROOT/raw-1"
mdds_normalize_sha256_manifest "$TMP_ROOT/raw-1" "$TMP_ROOT/normalized-1"
mdds_normalize_sha256_manifest "$TMP_ROOT/raw-1" "$TMP_ROOT/normalized-2"
cmp "$TMP_ROOT/normalized-1" "$TMP_ROOT/normalized-2"
grep -Eq '^[0-9a-f]{64}  \./f$' "$TMP_ROOT/normalized-1"
(cd "$TMP_ROOT/tree" && sha256sum -c "$TMP_ROOT/normalized-1" >/dev/null)

expect_manifest_reject() { # <case-number> <manifest-path>
  local number="$1" path="$2"
  printf '%064d *%s\n' 0 "$path" > "$TMP_ROOT/raw-bad-$number"
  if mdds_normalize_sha256_manifest \
      "$TMP_ROOT/raw-bad-$number" "$TMP_ROOT/normalized-bad-$number"; then
    echo "ERROR: unsafe SHA-256 path unexpectedly normalized: $path" >&2
    exit 1
  fi
}

expect_manifest_reject 1 'not-a-relative-path'
expect_manifest_reject 2 './../escape'
expect_manifest_reject 3 './dir/../file'
expect_manifest_reject 4 './dir/./file'
expect_manifest_reject 5 './/absolute-like'
expect_manifest_reject 6 './dir//file'
expect_manifest_reject 7 './dir\alternate-separator'
expect_manifest_reject 8 './env.sh'
expect_manifest_reject 9 './deploy_manifest.sha256'
expect_manifest_reject 10 './.mdds_deploy_complete'
expect_manifest_reject 11 './.mdds-activity-lock/owner'

echo "deploy input-contract negative tests: PASS"
