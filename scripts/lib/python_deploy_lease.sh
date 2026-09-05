# Shared exclusive lease for runtime and overlay transactions on one prefix.
# Caller provides HDC and run_shell_marker. Only exact-owner locks are removed.
PY_DEPLOY_LEASE_BOARD=
PY_DEPLOY_LEASE_PATH=
PY_DEPLOY_LEASE_OWNER=
PY_DEPLOY_TEMP_FILE=
PY_DEPLOY_TEMP_SHA=

python_deploy_acquire_lease() {
  local board="$1" prefix="$2" owner="$3"
  local lease="$prefix.python-deploy.lock"
  [[ "$prefix" =~ ^/data/[A-Za-z0-9._+-]+$ && "$owner" =~ ^[A-Za-z0-9._+-]+$ ]] || return 2
  [ -z "$PY_DEPLOY_LEASE_BOARD" ] || return 1
  # Register the intended owner before contacting HDC: a lost successful reply
  # must still be recoverable by the exact-owner EXIT handler.
  PY_DEPLOY_LEASE_BOARD="$board"
  PY_DEPLOY_LEASE_PATH="$lease"
  PY_DEPLOY_LEASE_OWNER="$owner"
  run_shell_marker "$board" \
    "umask 077; if mkdir '$lease'; then if printf '%s\\n' '$owner' > '$lease/owner'; then echo PYTHON_DEPLOY_LEASE_ACQUIRED; else rm -f '$lease/owner'; rmdir '$lease'; exit 1; fi; else exit 1; fi" \
    PYTHON_DEPLOY_LEASE_ACQUIRED >/dev/null || return 1
}

python_deploy_release_lease() {
  [ -n "$PY_DEPLOY_LEASE_BOARD" ] || return 0
  if run_shell_marker "$PY_DEPLOY_LEASE_BOARD" \
    "test ! -e '$PY_DEPLOY_LEASE_PATH' && test ! -L '$PY_DEPLOY_LEASE_PATH' && echo PYTHON_DEPLOY_LEASE_ABSENT" \
    PYTHON_DEPLOY_LEASE_ABSENT >/dev/null; then
    PY_DEPLOY_LEASE_BOARD=
    PY_DEPLOY_LEASE_PATH=
    PY_DEPLOY_LEASE_OWNER=
    return 0
  fi
  if [ -n "$PY_DEPLOY_TEMP_FILE" ]; then
    run_shell_marker "$PY_DEPLOY_LEASE_BOARD" \
      "if test ! -e '$PY_DEPLOY_TEMP_FILE'; then :; elif test ! -L '$PY_DEPLOY_TEMP_FILE' && test \"\$(sha256sum '$PY_DEPLOY_TEMP_FILE' | cut -d ' ' -f1)\" = '$PY_DEPLOY_TEMP_SHA'; then rm -f '$PY_DEPLOY_TEMP_FILE'; else exit 1; fi; echo PYTHON_DEPLOY_TEMP_RELEASED" \
      PYTHON_DEPLOY_TEMP_RELEASED >/dev/null || return 1
    PY_DEPLOY_TEMP_FILE=
    PY_DEPLOY_TEMP_SHA=
  fi
  run_shell_marker "$PY_DEPLOY_LEASE_BOARD" \
    "test -d '$PY_DEPLOY_LEASE_PATH' && test ! -L '$PY_DEPLOY_LEASE_PATH' && test -f '$PY_DEPLOY_LEASE_PATH/owner' && test ! -L '$PY_DEPLOY_LEASE_PATH/owner' && grep -Fxq '$PY_DEPLOY_LEASE_OWNER' '$PY_DEPLOY_LEASE_PATH/owner' && test \"\$(find '$PY_DEPLOY_LEASE_PATH' -mindepth 1 -maxdepth 1)\" = '$PY_DEPLOY_LEASE_PATH/owner' && rm '$PY_DEPLOY_LEASE_PATH/owner' && rmdir '$PY_DEPLOY_LEASE_PATH' && echo PYTHON_DEPLOY_LEASE_RELEASED" \
    PYTHON_DEPLOY_LEASE_RELEASED >/dev/null || return 1
  PY_DEPLOY_LEASE_BOARD=
  PY_DEPLOY_LEASE_PATH=
  PY_DEPLOY_LEASE_OWNER=
}

python_deploy_exit() {
  local rc=$?
  trap - EXIT
  python_deploy_release_lease || rc=1
  exit "$rc"
}
trap python_deploy_exit EXIT
