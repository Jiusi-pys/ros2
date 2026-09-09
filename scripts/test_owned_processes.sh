#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

DEVICE_DIR=/fake/ros2
ROS2_RUN_ID=owned_helper_test
CALLS="$(mktemp "${TMPDIR:-/tmp}/owned-helper-calls.XXXXXX")"
trap 'rm -f "$CALLS"' EXIT

shell() {
  local board="$1" command="$2"
  if [[ "$command" == *"printf ROS2_OWNED_LOCK_RELEASED"* ]]; then
    printf '%s|release|%s\n' "$board" "$command" >> "$CALLS"
    printf ROS2_OWNED_LOCK_RELEASED
  elif [[ "$command" == *"printf ROS2_OWNED_READY"* ]]; then
    printf '%s|setup|%s\n' "$board" "$command" >> "$CALLS"
    if [ "$board" = board_a ]; then
      printf ROS2_OWNED_READY
    else
      # Models exact activity-owner creation followed by run-root failure.
      printf ROS2_OWNED_SETUP_FAILED
    fi
  else
    echo "unexpected mock command" >&2
    return 1
  fi
}

source scripts/lib/ros2_legacy_owned_processes.sh
if ros2_owned_init owned_helper board_a board_b; then
  echo "ERROR: partial second-board setup unexpectedly passed" >&2
  exit 1
fi
[ "${#ROS2_OWNED_LOCK_BOARDS[@]}" -eq 0 ] || {
  echo "ERROR: prior-board lock list was not unwound" >&2
  exit 1
}
[ "$(grep -c '^board_a|release|' "$CALLS")" -eq 1 ] || {
  echo "ERROR: first board did not receive one exact-owner release" >&2
  exit 1
}
[ "$(grep -c '^board_b|release|' "$CALLS")" -eq 1 ] || {
  echo "ERROR: partially initialized second board was not released" >&2
  exit 1
}
grep '^board_b|release|' "$CALLS" | grep -Fq \
  'ROS2_ACTIVITY_LOCK MODE=TEST RUN_ID=owned_helper_test OWNER=owned_helper'
grep '^board_b|release|' "$CALLS" | grep -Fq -- '-mindepth 1 -maxdepth 1'

: > "$CALLS"
if ros2_owned_init duplicate_boards board_a board_a; then
  echo "ERROR: duplicate board list unexpectedly passed" >&2
  exit 1
fi
[ ! -s "$CALLS" ] || {
  echo "ERROR: duplicate board was rejected only after remote mutation" >&2
  exit 1
}
echo "owned process helper partial-init cleanup: PASS"
