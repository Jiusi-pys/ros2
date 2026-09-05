#!/usr/bin/env bash
# Execute the real remote publication command locally with a paused writer.
# Every HDC entry point is replaced before the runner is sourced.
set -euo pipefail
cd "$(dirname "$0")/.."
HDC=/host-contract-must-never-access-a-board
export HDC
source scripts/run_mdds_graph_ownership.sh
fixture="$(mktemp -d)"
writer_pid=''
cleanup() {
  if [[ -n "$writer_pid" ]]; then
    touch "$fixture/resume"
    wait "$writer_pid" 2>/dev/null || true
  fi
  rm -f "$fixture/resume" "$fixture/paused" "$fixture/result" "$fixture/owner" \
    "$fixture/source.stop" "$fixture/source.stop.pending"
  rmdir "$fixture"
}
trap cleanup EXIT
MDDS_OWNED_RUN_ID=stop_publish_contract
MDDS_OWNED_LABEL=graph_ownership
MDDS_OWNED_REMOTE_DIR="$fixture"
printf 'MDDS_RUN_OWNER RUN_ID=%s LABEL=%s\n' "$MDDS_OWNED_RUN_ID" "$MDDS_OWNED_LABEL" > "$fixture/owner"
export GRAPH_STOP_FIXTURE="$fixture" GRAPH_STOP_RUN_ID="$MDDS_OWNED_RUN_ID"
shell() {
  local command="$2"
  bash -c '
printf() {
  if [[ "$*" == *"GRAPH_STOP RUN_ID="* ]]; then
    builtin printf "GRAPH_STOP "
    touch "$GRAPH_STOP_FIXTURE/paused"
    for ((poll=0; poll<200; poll++)); do
      [[ ! -f "$GRAPH_STOP_FIXTURE/resume" ]] || break
      sleep 0.01
    done
    builtin printf "RUN_ID=%s\n" "$GRAPH_STOP_RUN_ID"
  else
    builtin printf "$@"
  fi
}
eval "$1"
' bash "$command"
}
(graph_publish_stop fake_board; printf '%s\n' "$?" > "$fixture/result") &
writer_pid=$!
for ((poll=0; poll<200; poll++)); do
  [[ ! -f "$fixture/paused" ]] || break
  sleep 0.01
done
[[ -f "$fixture/paused" ]] || { echo 'ERROR: writer never reached pause' >&2; exit 1; }
[[ ! -e "$fixture/source.stop" ]] || {
  echo 'ERROR: final stop record became visible before its complete contents' >&2
  exit 1
}
[[ -f "$fixture/source.stop.pending" ]] || exit 1
touch "$fixture/resume"
wait "$writer_pid"
writer_pid=''
[[ "$(cat "$fixture/result")" == 0 ]] || exit 1
grep -Fqx "GRAPH_STOP RUN_ID=$MDDS_OWNED_RUN_ID" "$fixture/source.stop"
expected=$(printf 'GRAPH_STOP RUN_ID=%s\n' "$MDDS_OWNED_RUN_ID" | sha256sum | cut -d ' ' -f1)
[[ "$(sha256sum "$fixture/source.stop" | cut -d ' ' -f1)" == "$expected" ]]
[[ ! -e "$fixture/source.stop.pending" ]]
if graph_publish_stop fake_board; then
  echo 'ERROR: an existing final stop record was overwritten' >&2
  exit 1
fi
[[ "$(sha256sum "$fixture/source.stop" | cut -d ' ' -f1)" == "$expected" ]]
rm -f "$fixture/source.stop"
GRAPH_STOP_RUN_ID=wrong_run
export GRAPH_STOP_RUN_ID
if graph_publish_stop fake_board; then
  echo 'ERROR: a corrupt stop record passed publication' >&2
  exit 1
fi
[[ ! -e "$fixture/source.stop" ]]
[[ -f "$fixture/source.stop.pending" ]]
echo 'GRAPH_STOP_PUBLICATION result=PASS partial_final=absent final_bytes=exact overwrite=rejected corrupt=rejected'
