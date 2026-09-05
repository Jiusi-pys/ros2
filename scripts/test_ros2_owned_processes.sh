#!/usr/bin/env bash
# Host-only fail-closed tests: remote_shell is mocked, no board is contacted.
set -euo pipefail
cd "$(dirname "$0")/.."
DEVICE_DIR=/data/local/tmp/ros2-contract-test
ROS2_RUN_ID=generic_owned_contract
. scripts/lib/ros2_owned_processes.sh
ROS2_OWNED_RUN_ID="$ROS2_RUN_ID"

remote_shell() {
  local command="$2"
  [[ "$command" != *'kill -9'* ]] || {
    echo 'normal graceful cleanup attempted SIGKILL' >&2
    return 99
  }
  [[ "$command" == *'START=456'* && "$command" == *"!= '456'"* ]] || return 98
  printf '%s' "$MOCK_RESULT"
}

for result in ROS2_OWNED_GONE ROS2_OWNED_SIGNALLED; do
  ROS2_OWNED_TRACKED=('board_a|123|456|/fake/record|tag|case.log')
  MOCK_RESULT="$result"
  ros2_owned_signal_log board_a case.log TERM
  [[ "${#ROS2_OWNED_TRACKED[@]}" -eq 0 ]] || exit 1
done
for result in ROS2_OWNED_SIGNAL_TIMEOUT ROS2_OWNED_PID_REUSED ROS2_OWNED_RECORD_INVALID; do
  ROS2_OWNED_TRACKED=('board_a|123|456|/fake/record|tag|case.log')
  MOCK_RESULT="$result"
  if ros2_owned_signal_log board_a case.log TERM; then
    echo "ERROR: unsafe cleanup result accepted: $result" >&2
    exit 1
  fi
  [[ "${#ROS2_OWNED_TRACKED[@]}" -eq 1 ]] || exit 1
done
ROS2_OWNED_TRACKED=('board_a|123|456|/fake/record|tag|case.log')
if ros2_owned_signal_log board_a case.log KILL; then
  echo 'ERROR: normal signal path accepted KILL' >&2
  exit 1
fi
[[ "${#ROS2_OWNED_TRACKED[@]}" -eq 1 ]] || exit 1

# A partial second-board setup must release only exact-owner locks, including
# the partially initialized board; a duplicate serial must mutate nothing.
ROS2_OWNED_LOCK_BOARDS=()
ROS2_OWNED_TRACKED=()
remote_shell() {
  local board="$1" command="$2"
  if [[ "$command" == *'printf ROS2_OWNED_LOCK_RELEASED'* ]]; then
    [[ "$command" == *'-mindepth 1 -maxdepth 1'* && "$command" == *'OWNER=contract'* ]] || return 97
    printf ROS2_OWNED_LOCK_RELEASED
  elif [[ "$command" == *'printf ROS2_OWNED_READY'* ]]; then
    if [[ "$board" == board_a ]]; then printf ROS2_OWNED_READY; else printf ROS2_OWNED_SETUP_FAILED; fi
  else
    return 96
  fi
}
if ros2_owned_init contract board_a board_b; then exit 1; fi
[[ "${#ROS2_OWNED_LOCK_BOARDS[@]}" -eq 0 ]] || exit 1
remote_shell() { echo 'ERROR: duplicate serial contacted a board' >&2; return 95; }
if ros2_owned_init contract board_a board_a; then exit 1; fi
# Namespace adoption requires a tracked PID/start anchor and is idempotent,
# including when emergency cleanup follows a partly completed tracing case.
ROS2_OWNED_TRACKED=('board_a|123|456|/fake/record|tag|sessiond.log')
ROS2_OWNED_REMOTE_DIR=/fake/run
adopt_phase=first
remote_shell() {
  local command="$2"
  if [[ "$command" == *'printf ROS2_NAMESPACE_ADOPTED'* ]]; then
    [[ "$adopt_phase" == first && "$command" == *"= '789'"* ]] || return 93
    printf ROS2_NAMESPACE_ADOPTED
  else
    [[ "$command" == *'/proc/123/stat'* && "$command" == *"= '456'"* && "$command" == *'/proc/1/ns/mnt'* ]] || return 92
    printf '123 456\n124 789\n'
  fi
}
ros2_owned_adopt_namespace board_a 123
[[ "${#ROS2_OWNED_TRACKED[@]}" -eq 2 ]] || exit 1
adopt_phase=second
ros2_owned_adopt_namespace board_a 123
[[ "${#ROS2_OWNED_TRACKED[@]}" -eq 2 ]] || exit 1
ROS2_OWNED_TRACKED=()
remote_shell() { echo 'ERROR: untracked anchor contacted a board' >&2; return 91; }
if ros2_owned_adopt_namespace board_a 123; then exit 1; fi

echo 'generic owned-process safety contract: PASS'
