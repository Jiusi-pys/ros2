#!/usr/bin/env bash
# Host-only shell contracts: the fake HDC must never be invoked.
set -euo pipefail
cd "$(dirname "$0")/.."
export HDC=/host-contract-no-board-access
source scripts/run_mdds_broker_local.sh

expect_invalid() {
  if (export MDDS_RUN_ID=bl_test; broker_local_parse_args "$@"); then
    echo "ERROR: unsafe broker runner arguments were accepted: $*" >&2
    exit 1
  fi
}
expect_invalid --variant udp
expect_invalid --scenario unrelated
expect_invalid --board 'unsafe;target'
expect_invalid --run-id '../old'
expect_invalid --run-id 'this_identifier_is_more_than_thirty_two_bytes'
expect_invalid --unknown x
(export MDDS_RUN_ID=bl_test; broker_local_parse_args --variant baseline --scenario contexts)
(export MDDS_RUN_ID=bl_test; broker_local_parse_args --variant overlay --scenario all)
(export MDDS_RUN_ID=bl_test; broker_local_parse_args --rclpy-native '/frozen/native.so' --rclpy-package '/frozen/python/rclpy'; [[ "$BROKER_RCLPY_NATIVE" == '/frozen/native.so' && "$BROKER_RCLPY_PACKAGE" == '/frozen/python/rclpy' ]])
expect_invalid --rclpy-package '/frozen/python/rclpy'
expect_invalid --rclpy-native ''
expect_invalid --rclpy-package ''

# Existing hash helpers must reject host drift and malformed remote readback.
fixture=$(mktemp -d)
trap 'rm -f "$fixture/input"; rmdir "$fixture"' EXIT
printf known > "$fixture/input"
expected=$(graph_sha "$fixture/input")
graph_push() { return 0; }
shell() { printf '%s' "$MOCK_REMOTE_SHA"; }
for MOCK_REMOTE_SHA in '' invalid "$(printf '%064d' 0)" "$expected extra" "$expected"$'\n'"$expected"; do
  if graph_stage_artifact board "$fixture/input" /fake/input "$expected"; then
    echo 'ERROR: invalid remote hash accepted' >&2
    exit 1
  fi
done
MOCK_REMOTE_SHA="$expected"
graph_stage_artifact board "$fixture/input" /fake/input "$expected"
printf drift > "$fixture/input"
if graph_stage_artifact board "$fixture/input" /fake/input "$expected"; then
  echo 'ERROR: changed local artifact accepted' >&2
  exit 1
fi
echo 'BROKER_LOCAL_RUNNER_CONTRACT PASS no_board_access=true'
