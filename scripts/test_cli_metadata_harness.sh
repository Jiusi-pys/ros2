#!/usr/bin/env bash
# Host-only lifecycle faults; fake HDC is set before sourcing any harness.
set -euo pipefail
cd "$(dirname "$0")/.."
export HDC=/host-test-must-never-access-a-board
source scripts/run_mdds_cli_metadata.sh
MDDS_OWNED_RUN_ID=metadata_contract
MDDS_OWNED_REMOTE_DIR=/fake/metadata_contract
shell() { printf '%s' "$MOCK_OUTPUT"; }
MOCK_OUTPUT='MDDS_OWNED_PROCESS RUN_ID=metadata_contract TAG=metadata_child PID=12 START=34'
metadata_register_child fake_board
[[ "${MDDS_OWNED_TRACKED[0]}" == 'fake_board|12|34|/fake/metadata_contract/metadata.child.pid|metadata_child|metadata.child' ]]
for value in 0 1 1201 invalid -1; do
  if metadata_wait_limit "$value"; then exit 1; fi
done
metadata_wait_limit 1200
MOCK_OUTPUT=SUMMARY_EXISTS
if metadata_wait_terminal fake_board 2; then
  echo 'ERROR: summary existence was accepted as a process terminal' >&2
  exit 1
fi
MOCK_OUTPUT=METADATA_TERMINAL_READY
metadata_wait_terminal fake_board 2
echo 'CLI_METADATA_HARNESS_CONTRACT result=PASS owned_child=exact summary_is_not_terminal=yes wait_limit=1200'
