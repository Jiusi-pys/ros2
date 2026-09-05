#!/usr/bin/env bash
# Run graph ownership against a private two-library overlay on both boards.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
HDC="${HDC:-C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00
BOARD_B=3e01ff55454d202020104433991c3b00
DEVICE_DIR=/data/local/tmp/ros2
GRAPH_HOST_PYTHON="${MDDS_HOST_PYTHON:-$PWD/.pixi/envs/default/python.exe}"
export MSYS2_ARG_CONV_EXCL='*'
shell() { timeout --kill-after=1s 10 "$HDC" -t "$1" shell "$2" </dev/null; }
source scripts/lib/mdds_owned_processes.sh

graph_sha() {
  [[ -f "$1" && ! -L "$1" ]] || return 1
  sha256sum "$1" | cut -d ' ' -f1
}

graph_remote_sha() {
  local value
  value=$(shell "$1" "if test -f '$2' && test ! -L '$2'; then sha256sum '$2' | cut -d ' ' -f1; fi") || return 1
  value="${value%$'\r'}"
  [[ "$value" =~ ^[0-9a-f]{64}$ ]] || return 1
  printf '%s' "$value"
}

graph_push() {
  timeout --kill-after=1s 20 "$HDC" -t "$1" file send "$(cygpath -aw "$2")" "$3" </dev/null >/dev/null
}

graph_stage_artifact() {
  local board="$1" source="$2" remote="$3" expected="$4" actual
  [[ "$expected" =~ ^[0-9a-f]{64}$ ]] || return 1
  [[ "$(graph_sha "$source")" == "$expected" ]] || return 1
  graph_push "$board" "$source" "$remote" || true
  actual=$(graph_remote_sha "$board" "$remote") || return 1
  [[ "$actual" == "$expected" ]] || return 1
}

graph_fetch_verified() {
  local board="$1" remote="$2" destination="$3" expected after
  expected=$(graph_remote_sha "$board" "$remote") || return 1
  [[ ! -e "$destination" && ! -L "$destination" ]] || return 1
  timeout --kill-after=1s 20 "$HDC" -t "$board" file recv "$remote" "$(cygpath -aw "$destination")" </dev/null >/dev/null || true
  [[ "$(graph_sha "$destination")" == "$expected" ]] || return 1
  after=$(graph_remote_sha "$board" "$remote") || return 1
  [[ "$after" == "$expected" ]] || return 1
  printf '%s  %s\n' "$expected" "${destination##*/}" >> "$LOGDIR/evidence.sha256"
}

graph_launch_fixture() {
  local board="$1" role="$2" seconds="$3" line='' attempt pid start record
  record="$MDDS_OWNED_REMOTE_DIR/$role.child.pid"
  mdds_owned_launch "$board" "$RENVS" \
    "python3.12 $remote_script --role supervisor --child-role $role --namespace $namespace --seconds $seconds --overlay $remote_lib --run-id $MDDS_OWNED_RUN_ID --child-record $record --status-file $MDDS_OWNED_REMOTE_DIR/$role.status.json --stop-file $MDDS_OWNED_REMOTE_DIR/source.stop" "$role.log"
  for ((attempt=0; attempt<30; attempt++)); do
    line=$(shell "$board" "if test -f '$record' && test ! -L '$record'; then cat '$record'; fi" | tr -d '\r')
    if [[ "$line" =~ ^MDDS_OWNED_PROCESS\ RUN_ID=$MDDS_OWNED_RUN_ID\ TAG=${role}_child\ PID=([0-9]+)\ START=([0-9]+)$ ]]; then
      pid="${BASH_REMATCH[1]}"; start="${BASH_REMATCH[2]}"
      # Child cleanup precedes supervisor cleanup, with exact PID/start guards.
      MDDS_OWNED_TRACKED=("$board|$pid|$start|$record|${role}_child|$role.child" "${MDDS_OWNED_TRACKED[@]}")
      return 0
    fi
    sleep 0.2
  done
  echo "ERROR: no exact child ownership record for $board/$role" >&2
  return 1
}

graph_wait_status() {
  local board="$1" role="$2" attempt ready
  for ((attempt=0; attempt<50; attempt++)); do
    ready=$(shell "$board" "if test -f '$MDDS_OWNED_REMOTE_DIR/$role.status.json' && test ! -L '$MDDS_OWNED_REMOTE_DIR/$role.status.json'; then printf GRAPH_STATUS_READY; fi" | tr -d '\r')
    [[ "$ready" != GRAPH_STATUS_READY ]] || return 0
    sleep 1
  done
  echo "ERROR: $role has no real process-exit record" >&2
  return 1
}

graph_collect_result() {
  local board="$1" role="$2" result=0
  graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$role.status.json" "$LOGDIR/$role.status.json" || return 1
  graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$role.child.pid" "$LOGDIR/$role.child.pid" || return 1
  graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$role.log" "$LOGDIR/$role.log" || return 1
  "$GRAPH_HOST_PYTHON" scripts/mdds_e2e/board_graph_ownership.py --role verify \
    --child-role "$role" --run-id "$MDDS_OWNED_RUN_ID" --namespace "$namespace" \
    --overlay "$remote_lib" --log "$LOGDIR/$role.log" \
    --status-file "$LOGDIR/$role.status.json" --child-record "$LOGDIR/$role.child.pid" || result=$?
  return "$result"
}

graph_publish_stop() {
  local ready record pending line expected owner
  record="$MDDS_OWNED_REMOTE_DIR/source.stop"
  pending="$record.pending"
  line="GRAPH_STOP RUN_ID=$MDDS_OWNED_RUN_ID"
  owner="MDDS_RUN_OWNER RUN_ID=$MDDS_OWNED_RUN_ID LABEL=$MDDS_OWNED_LABEL"
  expected=$(printf '%s\n' "$line" | sha256sum | cut -d ' ' -f1)
  [[ "$expected" =~ ^[0-9a-f]{64}$ ]] || return 1
  # Source treats the final path's existence as a completed stop request.
  # Publish only a fully written, identity/hash-verified sibling by rename.
  ready=$(shell "$1" "if test -f '$MDDS_OWNED_REMOTE_DIR/owner' && test ! -L '$MDDS_OWNED_REMOTE_DIR/owner' && grep -Fqx '$owner' '$MDDS_OWNED_REMOTE_DIR/owner' && test ! -e '$record' && test ! -L '$record' && (umask 077; set -C; printf '%s\\n' '$line' > '$pending') && test -f '$pending' && test ! -L '$pending' && grep -Fqx '$line' '$pending' && test \"\$(sha256sum '$pending' | cut -d ' ' -f1)\" = '$expected' && test ! -e '$record' && test ! -L '$record' && mv '$pending' '$record' && test \"\$(sha256sum '$record' | cut -d ' ' -f1)\" = '$expected' && grep -Fqx '$line' '$record'; then printf GRAPH_STOP_READY; else printf GRAPH_STOP_FAILED; fi" | tr -d '\r')
  [[ "$ready" == GRAPH_STOP_READY ]]
}

graph_main() {
  local lib actual board ready result=0
  local -A hashes=()
  # Freeze input hashes before taking locks or starting a process.
  for lib in libmdds.so librmw_mdds.so; do
    hashes["$lib"]=$(graph_sha "install_ohos/lib/$lib") || return 1
    [[ "${hashes[$lib]}" =~ ^[0-9a-f]{64}$ ]] || return 1
  done
  hashes[board_graph_ownership.py]=$(graph_sha scripts/mdds_e2e/board_graph_ownership.py) || return 1
  mdds_owned_init graph_ownership "$BOARD_A" "$BOARD_B"
  trap 'rc=$?; trap - EXIT; mdds_owned_finish || rc=1; exit "$rc"' EXIT
  LOGDIR="ohos_test_logs/graph_ownership/$MDDS_OWNED_RUN_ID"
  [[ ! -e "$LOGDIR" && ! -L "$LOGDIR" ]] || return 1
  mkdir -p "$LOGDIR"
  remote_script="$MDDS_OWNED_REMOTE_DIR/board_graph_ownership.py"
  remote_lib="$MDDS_OWNED_REMOTE_DIR/lib"
  namespace="/graph_${MDDS_OWNED_RUN_ID//[^A-Za-z0-9_]/_}"
  for board in "$BOARD_A" "$BOARD_B"; do
    ready=$(shell "$board" "if (umask 077; mkdir '$remote_lib') && test -d '$remote_lib' && test ! -L '$remote_lib'; then printf GRAPH_LIB_READY; fi" | tr -d '\r')
    [[ "$ready" == GRAPH_LIB_READY ]] || return 1
    for lib in libmdds.so librmw_mdds.so; do
      graph_stage_artifact "$board" "install_ohos/lib/$lib" "$remote_lib/$lib" "${hashes[$lib]}" || return 1
    done
    graph_stage_artifact "$board" scripts/mdds_e2e/board_graph_ownership.py "$remote_script" "${hashes[board_graph_ownership.py]}" || return 1
  done
  # Both overlays must pass the same frozen hash set before either activates.
  for board in "$BOARD_A" "$BOARD_B"; do
    for lib in libmdds.so librmw_mdds.so; do
      actual=$(graph_remote_sha "$board" "$remote_lib/$lib") || return 1
      [[ "$actual" == "${hashes[$lib]}" ]] || return 1
      printf '%s  %s\n' "$actual" "$remote_lib/$lib" >> "$LOGDIR/$board.artifacts.sha256"
    done
    actual=$(graph_remote_sha "$board" "$remote_script") || return 1
    [[ "$actual" == "${hashes[board_graph_ownership.py]}" ]] || return 1
    printf '%s  %s\n' "$actual" "$remote_script" >> "$LOGDIR/$board.artifacts.sha256"
  done
  RENVS=". $DEVICE_DIR/env.sh || exit 70; . $DEVICE_DIR/share/rmw_mdds/config/ohos_dsoftbus.env || exit 70; export LD_LIBRARY_PATH=$remote_lib:\$LD_LIBRARY_PATH; export ROS_DOMAIN_ID=47; export MDDS_DEBUG=1;"
  graph_launch_fixture "$BOARD_B" source 90
  graph_launch_fixture "$BOARD_A" observer 30
  graph_wait_status "$BOARD_A" observer
  graph_publish_stop "$BOARD_B"
  graph_wait_status "$BOARD_B" source
  graph_collect_result "$BOARD_A" observer || result=1
  graph_collect_result "$BOARD_B" source || result=1
  [[ "$result" == 0 ]] || return 1
  mdds_owned_finish || return 1
  trap - EXIT
  printf 'GRAPH_RUN_RESULT PASS RUN_ID=%s EVIDENCE=%s\n' "$MDDS_OWNED_RUN_ID" "$LOGDIR"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  graph_main "$@"
fi
