#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
export HDC=/host-test-must-never-access-a-board
source scripts/run_mdds_cli_offline.sh
declare -F cli_offline_main >/dev/null
metadata_wait_limit 1200
for value in 0 1 1201 invalid; do
  if metadata_wait_limit "$value"; then exit 1; fi
done
printf 'CLI_OFFLINE_HOST_SOURCE PASS no_board_access=true\n'
