#!/usr/bin/env bash
# Host-only fault injection; no board commands are executed.
set -euo pipefail
cd "$(dirname "$0")/.."
HDC=/host-contract-must-never-access-a-board
export HDC
source scripts/run_mdds_graph_ownership.sh
fixture="$(mktemp -d)"
trap 'rm -f "$fixture/input"; rmdir "$fixture"' EXIT
printf test > "$fixture/input"
expected=$(sha256sum "$fixture/input" | cut -d ' ' -f1)
graph_push() { return 0; }
shell() { printf '%s' "$MOCK_REMOTE_SHA"; }
for MOCK_REMOTE_SHA in '' invalid "$(printf '%064d' 0)" "$expected extra" "$expected"$'\n'"$expected"; do
  if graph_stage_artifact board "$fixture/input" /fake/lib.so "$expected"; then
    echo 'ERROR: missing/malformed/changed remote library hash passed' >&2
    exit 1
  fi
done
MOCK_REMOTE_SHA="$expected"
graph_stage_artifact board "$fixture/input" /fake/lib.so "$expected"
if graph_stage_artifact board "$fixture/input" /fake/lib.so invalid; then exit 1; fi
printf changed > "$fixture/input"
if graph_stage_artifact board "$fixture/input" /fake/lib.so "$expected"; then exit 1; fi
echo 'GRAPH_STAGE_CONTRACT result=PASS cases=missing,malformed,mismatch,extra,duplicate,exact,local_drift'
