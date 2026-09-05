#!/usr/bin/env bash
# Execute lease shell commands against an isolated host fixture, never HDC.
set -euo pipefail
cd "$(dirname "$0")/.."
test_base="$(git rev-parse --path-format=absolute --git-dir)"
test_root="$(mktemp -d "$test_base/python-lease-test.XXXXXX")"
case "$test_root" in "$test_base"/python-lease-test.*) ;; *) exit 70 ;; esac
run_shell_marker() {
  local command="$2" marker="$3" output
  command="${command//\/data\//$test_root/}"
  output="$(sh -c "set -e; $command" 2>&1)" || return 1
  if [ "${lose_acquire_reply:-0}" = 1 ] && [ "$marker" = PYTHON_DEPLOY_LEASE_ACQUIRED ]; then
    return 1
  fi
  grep -Eq "^${marker}([[:space:]]|$)" <<< "$output"
}
. scripts/lib/python_deploy_lease.sh
cleanup_test() {
  local rc=$?
  trap - EXIT
  case "$test_root" in "$test_base"/python-lease-test.*) rm -rf -- "$test_root" ;; esac
  exit "$rc"
}
trap cleanup_test EXIT

python_deploy_acquire_lease board_a /data/python-verify-123456789abc owner_one
if ( PY_DEPLOY_LEASE_BOARD=; python_deploy_acquire_lease board_a /data/python-verify-123456789abc owner_two ); then
  echo 'ERROR: concurrent same-prefix lease accepted' >&2; exit 1
fi
grep -Fxq owner_one "$test_root/python-verify-123456789abc.python-deploy.lock/owner"
PY_DEPLOY_LEASE_OWNER=wrong_owner
if python_deploy_release_lease; then echo 'ERROR: wrong owner released lease' >&2; exit 1; fi
test -f "$test_root/python-verify-123456789abc.python-deploy.lock/owner"
PY_DEPLOY_LEASE_OWNER=owner_one
python_deploy_release_lease
test ! -e "$test_root/python-verify-123456789abc.python-deploy.lock"
test -z "$PY_DEPLOY_LEASE_BOARD"
python_deploy_acquire_lease board_a /data/python-verify-123456789abc owner_two
python_deploy_release_lease
lose_acquire_reply=1
if python_deploy_acquire_lease board_a /data/python-verify-123456789abc reply_lost; then
  echo 'ERROR: simulated lost reply reported success' >&2; exit 1
fi
grep -Fxq reply_lost "$test_root/python-verify-123456789abc.python-deploy.lock/owner"
test "$PY_DEPLOY_LEASE_OWNER" = reply_lost
lose_acquire_reply=0
python_deploy_release_lease
test ! -e "$test_root/python-verify-123456789abc.python-deploy.lock"
test -z "$PY_DEPLOY_LEASE_BOARD"
echo 'python deployment exclusive lease contract: PASS'
