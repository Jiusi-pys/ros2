#!/usr/bin/env bash
# Parent-run board-A metadata CLI batch. Source-safe for host fault tests.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
source scripts/run_mdds_graph_ownership.sh

# Retain the shared shell contract while allowing the terminal loop to shrink
# an HDC call's bound before its overall deadline.
shell() {
  timeout --kill-after=0.2s "${METADATA_HDC_SECONDS:-10}" "$HDC" -t "$1" shell "$2" </dev/null
}

metadata_wait_limit() {
  [[ "$1" =~ ^[1-9][0-9]{0,3}$ && "$1" -ge 2 && "$1" -le 1200 ]]
}

metadata_register_child() {
  local board="$1" record="$MDDS_OWNED_REMOTE_DIR/metadata.child.pid" line attempt pid start
  for ((attempt=0; attempt<30; attempt++)); do
    line=$(shell "$board" "if test -f '$record' && test ! -L '$record'; then cat '$record'; fi" | tr -d '\r')
    if [[ "$line" =~ ^MDDS_OWNED_PROCESS\ RUN_ID=$MDDS_OWNED_RUN_ID\ TAG=metadata_child\ PID=([1-9][0-9]*)\ START=([1-9][0-9]*)$ ]]; then
      pid="${BASH_REMATCH[1]}"; start="${BASH_REMATCH[2]}"
      MDDS_OWNED_TRACKED=("$board|$pid|$start|$record|metadata_child|metadata.child" "${MDDS_OWNED_TRACKED[@]}")
      return 0
    fi
    sleep 0.2
  done
  echo 'ERROR: no exact metadata child ownership record' >&2
  return 1
}

metadata_wait_terminal() {
  local board="$1" limit="$2" ready deadline progress remaining
  local METADATA_HDC_SECONDS=10
  metadata_wait_limit "$limit" || return 2
  deadline=$((SECONDS + limit))
  progress=$SECONDS
  while ((SECONDS < deadline)); do
    remaining=$((deadline - SECONDS))
    # Reserve the final second for timeout escalation and return bookkeeping.
    ((remaining > 1)) || break
    METADATA_HDC_SECONDS=$((remaining > 11 ? 10 : remaining - 1))
    ready=$(shell "$board" "if test -f '$MDDS_OWNED_REMOTE_DIR/metadata.status.json' && test ! -L '$MDDS_OWNED_REMOTE_DIR/metadata.status.json' && test -f '$MDDS_OWNED_REMOTE_DIR/metadata.archive.json' && test ! -L '$MDDS_OWNED_REMOTE_DIR/metadata.archive.json'; then printf METADATA_TERMINAL_READY; fi" | tr -d '\r')
    [[ "$ready" != METADATA_TERMINAL_READY ]] || return 0
    if ((SECONDS - progress >= 30)); then
      printf 'METADATA_WAIT RUN_ID=%s REMAINING_SECONDS=%s\n' "$MDDS_OWNED_RUN_ID" "$((deadline - SECONDS))"
      progress=$SECONDS
    fi
    sleep 1
  done
  echo 'ERROR: metadata real process/archive terminal wait expired' >&2
  return 1
}

metadata_main() {
  local wait_seconds="${MDDS_METADATA_WAIT_SECONDS:-1200}" dependency frozen digest archive_sha
  metadata_wait_limit "$wait_seconds" || { echo 'ERROR: metadata wait limit must be 2..1200 seconds' >&2; return 2; }
  # The existing graph harness supplies exact ownership, transfer, readback,
  # evidence hashes and cleanup. No board B state is needed for this batch.
  mdds_owned_init cli_metadata "$BOARD_A"
  trap 'rc=$?; trap - EXIT; mdds_owned_finish || rc=1; exit "$rc"' EXIT
  LOGDIR="ohos_test_logs/cli_metadata/$MDDS_OWNED_RUN_ID"
  [[ ! -e "$LOGDIR" && ! -L "$LOGDIR" ]] || return 1
  mkdir -p "$LOGDIR"
  for dependency in board_cli_metadata.py cli_acceptance.py cli_acceptance_manifest.json \
      board_graph_ownership.py board_cli_metadata_supervisor.py; do
    frozen="$LOGDIR/$dependency"
    cp -- "scripts/mdds_e2e/$dependency" "$frozen"
    digest=$(graph_sha "$frozen") || return 1
    [[ "$digest" =~ ^[0-9a-f]{64}$ ]] || return 1
    graph_stage_artifact "$BOARD_A" "$frozen" "$MDDS_OWNED_REMOTE_DIR/$dependency" "$digest" || return 1
    printf '%s  %s\n' "$digest" "$dependency" >> "$LOGDIR/inputs.sha256"
  done
  # Freeze the host verifier next to the identical runner/schema copies that
  # were sent to the board, so a later workspace edit cannot change replay.
  cp -- scripts/mdds_e2e/verify_cli_metadata_archive.py "$LOGDIR/verify_cli_metadata_archive.py"
  digest=$(graph_sha "$LOGDIR/verify_cli_metadata_archive.py") || return 1
  printf '%s  verify_cli_metadata_archive.py\n' "$digest" >> "$LOGDIR/inputs.sha256"
  RENVS=". $DEVICE_DIR/env.sh || exit 70; . $DEVICE_DIR/share/rmw_mdds/config/ohos_dsoftbus.env || exit 70; export PYTHONDONTWRITEBYTECODE=1;"
  mdds_owned_launch "$BOARD_A" "$RENVS" \
    "python3.12 -B $MDDS_OWNED_REMOTE_DIR/board_cli_metadata_supervisor.py --prefix $DEVICE_DIR --run-id $MDDS_OWNED_RUN_ID --board-serial $BOARD_A" metadata.log
  metadata_register_child "$BOARD_A"
  metadata_wait_terminal "$BOARD_A" "$wait_seconds"
  for dependency in metadata.status.json metadata.child.pid metadata.log metadata.archive.json; do
    graph_fetch_verified "$BOARD_A" "$MDDS_OWNED_REMOTE_DIR/$dependency" "$LOGDIR/$dependency" || return 1
  done
  archive_sha=$(graph_remote_sha "$BOARD_A" "$MDDS_OWNED_REMOTE_DIR/cli_metadata.tar") || {
    echo "ERROR: metadata archive was not published; inspect $LOGDIR/metadata.archive.json" >&2
    return 1
  }
  graph_fetch_verified "$BOARD_A" "$MDDS_OWNED_REMOTE_DIR/cli_metadata.tar" "$LOGDIR/cli_metadata.tar" || return 1
  "$GRAPH_HOST_PYTHON" -B "$LOGDIR/verify_cli_metadata_archive.py" \
    --archive "$LOGDIR/cli_metadata.tar" --archive-sha256 "$archive_sha" \
    --archive-record "$LOGDIR/metadata.archive.json" --status "$LOGDIR/metadata.status.json" \
    --child-record "$LOGDIR/metadata.child.pid" --log "$LOGDIR/metadata.log" \
    --template "$LOGDIR/cli_acceptance_manifest.json" --run-id "$MDDS_OWNED_RUN_ID" \
    --destination "$LOGDIR/evidence" --report "$LOGDIR/host_verification.json"
  mdds_owned_finish || return 1
  trap - EXIT
  printf 'METADATA_BATCH_RESULT PASS RUN_ID=%s CASES=12 REMAINING_NOT_RUN=86 GATEWAY_UNLOCKED=false EVIDENCE=%s\n' \
    "$MDDS_OWNED_RUN_ID" "$LOGDIR"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  metadata_main "$@"
fi
