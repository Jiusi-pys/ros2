#!/usr/bin/env bash
# Nine offline CLI candidates. Reuse the proven metadata process/archive tools.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
source scripts/run_mdds_cli_metadata.sh

cli_offline_main() {
  local wait_seconds="${MDDS_METADATA_WAIT_SECONDS:-1200}" dependency frozen digest archive_sha result=0
  local openssl="${MDDS_OFFLINE_OPENSSL:-.pixi/envs/default/Library/bin/openssl.exe}"
  metadata_wait_limit "$wait_seconds" || return 2
  [[ -f "$openssl" && ! -L "$openssl" ]] || { echo 'ERROR: offline public-signature verifier is unavailable'; return 2; }
  LOGDIR="ohos_test_logs/cli_offline/${MDDS_RUN_ID:-offline_$(date +%Y%m%dT%H%M%S)_${RANDOM}}"
  MDDS_RUN_ID="${LOGDIR##*/}"
  [[ "$MDDS_RUN_ID" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ && ! -e "$LOGDIR" && ! -L "$LOGDIR" ]] || return 2
  mkdir -p "$LOGDIR"
  local -a dependencies=(board_cli_offline.py board_cli_offline_supervisor.py cli_offline_common.py
    board_cli_metadata.py board_cli_metadata_supervisor.py board_graph_ownership.py
    cli_acceptance.py cli_acceptance_manifest.json verify_cli_metadata_archive.py verify_cli_offline_archive.py)
  for dependency in "${dependencies[@]}"; do
    cp -- "scripts/mdds_e2e/$dependency" "$LOGDIR/$dependency"
    digest=$(graph_sha "$LOGDIR/$dependency") || return 1
    [[ "$digest" =~ ^[0-9a-f]{64}$ ]] || return 1
    printf '%s  %s\n' "$digest" "$dependency" >> "$LOGDIR/inputs.sha256"
  done
  mdds_owned_init cli_offline "$BOARD_A" || return 1
  trap 'rc=$?; trap - EXIT; mdds_owned_finish || rc=1; exit "$rc"' EXIT
  for dependency in "${dependencies[@]}"; do
    digest=$(graph_sha "$LOGDIR/$dependency") || return 1
    graph_stage_artifact "$BOARD_A" "$LOGDIR/$dependency" "$MDDS_OWNED_REMOTE_DIR/$dependency" "$digest" || return 1
  done
  RENVS=". $DEVICE_DIR/env.sh || exit 70; . $DEVICE_DIR/share/rmw_mdds/config/ohos_dsoftbus.env || exit 70; export PYTHONDONTWRITEBYTECODE=1;"
  mdds_owned_launch "$BOARD_A" "$RENVS" \
    "python3.12 -B $MDDS_OWNED_REMOTE_DIR/board_cli_offline_supervisor.py --prefix $DEVICE_DIR --run-id $MDDS_OWNED_RUN_ID --board-serial $BOARD_A" metadata.log
  metadata_register_child "$BOARD_A"
  metadata_wait_terminal "$BOARD_A" "$wait_seconds"
  for dependency in metadata.status.json metadata.child.pid metadata.log metadata.archive.json; do
    graph_fetch_verified "$BOARD_A" "$MDDS_OWNED_REMOTE_DIR/$dependency" "$LOGDIR/$dependency" || return 1
  done
  archive_sha=$(graph_remote_sha "$BOARD_A" "$MDDS_OWNED_REMOTE_DIR/cli_metadata.tar") || return 1
  graph_fetch_verified "$BOARD_A" "$MDDS_OWNED_REMOTE_DIR/cli_metadata.tar" "$LOGDIR/cli_metadata.tar" || return 1
  "$GRAPH_HOST_PYTHON" -B "$LOGDIR/verify_cli_offline_archive.py" \
    --archive "$LOGDIR/cli_metadata.tar" --archive-sha256 "$archive_sha" \
    --archive-record "$LOGDIR/metadata.archive.json" --status "$LOGDIR/metadata.status.json" \
    --child-record "$LOGDIR/metadata.child.pid" --log "$LOGDIR/metadata.log" \
    --template "$LOGDIR/cli_acceptance_manifest.json" --run-id "$MDDS_OWNED_RUN_ID" \
    --destination "$LOGDIR/evidence" --report "$LOGDIR/host_verification.json" \
    --openssl "$(cygpath -am "$openssl")" || result=1
  mdds_owned_finish || result=1
  trap - EXIT
  printf 'CLI_OFFLINE_BATCH_RESULT %s RUN_ID=%s SELECTED_CASES=9 GATEWAY_UNLOCKED=false EVIDENCE=%s\n' \
    "$([[ "$result" == 0 ]] && echo PASS || echo FAIL)" "$MDDS_OWNED_RUN_ID" "$LOGDIR"
  return "$result"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then cli_offline_main "$@"; fi
