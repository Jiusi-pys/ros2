#!/usr/bin/env bash
# The double-forked ROS CLI daemon is outside the worker's process group.

ros_broker_cleanup_daemons() {
  local board="$1" expected output script
  if [[ -z "${LOGDIR:-}" || ! -f "$LOGDIR/cli_daemon_guard.py" ]]; then return 0; fi
  expected=$(graph_sha "$LOGDIR/cli_daemon_guard.py") || return 1
  script="$MDDS_OWNED_REMOTE_DIR/cli_daemon_guard.py"
  output=$(shell "$board" "if test -f '$script' && test ! -L '$script' && test \"\$(sha256sum '$script' | cut -d ' ' -f1)\" = '$expected'; then . '$DEVICE_DIR/env.sh' || exit 70; if python3.12 -B '$script' cleanup --root '$MDDS_OWNED_REMOTE_DIR' > '$MDDS_OWNED_REMOTE_DIR/cli_outer_cleanup.stdout'; then printf CLI_OUTER_CLEANUP_DONE; else printf CLI_OUTER_CLEANUP_FAILED; fi; elif test ! -f '$MDDS_OWNED_REMOTE_DIR/cli.log' && test ! -f '$MDDS_OWNED_REMOTE_DIR/cli.child.pid'; then printf CLI_NOT_LAUNCHED; else printf CLI_CLEANUP_GUARD_INVALID; fi" | tr -d '\r\n') || return 1
  case "$output" in
    CLI_OUTER_CLEANUP_DONE|CLI_NOT_LAUNCHED)
      printf 'ROS_BROKER_CLI_CLEANUP board=%s result=%s\n' "$board" "$output"
      return 0 ;;
    *) printf 'ERROR: CLI cleanup failed for %s: %s\n' "$board" "$output" >&2; return 1 ;;
  esac
}

ros_broker_finish() {
  local board rc=0
  mdds_owned_stop_all || rc=1
  if [[ ${#MDDS_OWNED_TRACKED[@]} -ne 0 ]]; then
    printf 'ERROR: retaining locks while owned workers remain unresolved\n' >&2
    return 1
  fi
  for board in "${MDDS_OWNED_LOCK_BOARDS[@]}"; do
    ros_broker_cleanup_daemons "$board" || rc=1
  done
  if [[ "$rc" != 0 ]]; then
    printf 'ERROR: retaining locks while CLI daemon cleanup is unresolved\n' >&2
    return 1
  fi
  mdds_owned_release_locks
}
