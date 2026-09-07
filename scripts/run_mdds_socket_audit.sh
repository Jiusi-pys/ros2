#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
source scripts/run_mdds_graph_ownership.sh
export MDDS_RUN_ID="${MDDS_RUN_ID:?explicit fresh run ID required}"
[[ "$MDDS_RUN_ID" =~ ^[A-Za-z0-9_]{1,32}$ ]] || exit 2
mdds_owned_init socket_audit "$BOARD_A" "$BOARD_B"
trap 'rc=$?; trap - EXIT; mdds_owned_finish || rc=1; exit "$rc"' EXIT
LOGDIR="ohos_test_logs/socket_audit/$MDDS_OWNED_RUN_ID"
[[ ! -e "$LOGDIR" ]];mkdir -p "$LOGDIR"
nonce=$("$GRAPH_HOST_PYTHON" -c 'import secrets;print(secrets.token_hex(16))' | tr -d '\r')
printf '%s\n' "$nonce" > "$LOGDIR/nonce"
cp build_ohos/mdds/libmdds_test_socket_audit.so "$LOGDIR/"
cp scripts/mdds_e2e/{socket_audit,board_socket_audit_probe,board_graph_ownership}.py "$LOGDIR/"
for board in "$BOARD_A" "$BOARD_B"; do
  for name in nonce libmdds_test_socket_audit.so socket_audit.py board_socket_audit_probe.py board_graph_ownership.py; do
    hash=$(graph_sha "$LOGDIR/$name")
    graph_stage_artifact "$board" "$LOGDIR/$name" "$MDDS_OWNED_REMOTE_DIR/$name" "$hash"
    printf '%s  %s\n' "$hash" "$name" >> "$LOGDIR/inputs_$board.sha256"
  done
  for mode in missing positive zero; do
    mdds_owned_launch "$board" ". '$DEVICE_DIR/env.sh' || exit 70; unset MDDS_TOKEN_EXEC; export PYTHONDONTWRITEBYTECODE=1;" \
      "python3.12 '$MDDS_OWNED_REMOTE_DIR/board_socket_audit_probe.py' '$MDDS_OWNED_REMOTE_DIR' '$MDDS_OWNED_RUN_ID' '$nonce' '$board' '$mode'" "$mode.log"
    graph_wait_status "$board" "$mode"
    for suffix in log status.json child.pid; do graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$mode.$suffix" "$LOGDIR/$board.$mode.$suffix"; done
    if [[ "$mode" != missing ]];then graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$mode.result.json" "$LOGDIR/$board.$mode.result.json";fi
  done
done
"$GRAPH_HOST_PYTHON" scripts/mdds_e2e/verify_socket_audit_probe.py "$LOGDIR"
