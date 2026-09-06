#!/usr/bin/env bash
# Real local-only RMW integration against private, hash-frozen libraries.
# Examples (Git Bash, from ros2/):
#   ./scripts/run_mdds_broker_local.sh --variant baseline --scenario contexts
#   ./scripts/run_mdds_broker_local.sh --variant overlay --scenario all
# No build, shared deployment mutation, or cross-board DSoftBus claim occurs.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
source scripts/run_mdds_graph_ownership.sh

BROKER_BOARD="${MDDS_BROKER_BOARD:-$BOARD_A}"
BROKER_VARIANT=overlay
BROKER_SCENARIO=all
BROKER_DOMAIN=49
BROKER_COUNT=5
BROKER_LOGROOT="${MDDS_BROKER_LOGROOT:-ohos_test_logs/broker_local}"
BROKER_DAEMON_STARTED=0
BROKER_DAEMON_STOPPED=0
BROKER_DAEMON_COLLECTED=0
declare -A BROKER_SOURCES=() BROKER_HASHES=() BROKER_DESTINATIONS=() BROKER_PIDS=()

broker_local_parse_args() {
  while (($#)); do
    (($# >= 2)) || return 2
    case "$1" in
      --board) BROKER_BOARD="$2" ;;
      --variant) BROKER_VARIANT="$2" ;;
      --scenario) BROKER_SCENARIO="$2" ;;
      --run-id) export MDDS_RUN_ID="$2" ;;
      *) echo "ERROR: unknown broker-local argument: $1" >&2; return 2 ;;
    esac
    shift 2
  done
  [[ "$BROKER_VARIANT" == overlay || "$BROKER_VARIANT" == baseline ]] || return 2
  [[ "$BROKER_SCENARIO" == all || "$BROKER_SCENARIO" == contexts || "$BROKER_SCENARIO" == processes ]] || return 2
  [[ "$BROKER_BOARD" =~ ^[A-Za-z0-9_.-]+$ ]] || return 2
  [[ "$BROKER_LOGROOT" =~ ^[A-Za-z0-9][A-Za-z0-9_./-]*$ && "/$BROKER_LOGROOT/" != *"/../"* && "/$BROKER_LOGROOT/" != *"/./"* ]] || return 2
  export MDDS_RUN_ID="${MDDS_RUN_ID:-bl_$(date +%m%d%H%M%S)_${RANDOM}}"
  [[ "$MDDS_RUN_ID" =~ ^[A-Za-z0-9_]{1,32}$ ]] || return 2
  # The daemon validates a filesystem AF_UNIX address, whose NUL also needs space.
  local planned_socket="$DEVICE_DIR/.mdds-owned-runs/$MDDS_RUN_ID/b.sock"
  ((${#planned_socket} < 108)) || { echo 'ERROR: owned Unix socket path is too long' >&2; return 2; }
}

broker_local_freeze_inputs() {
  BROKER_SOURCES[libmdds.so]="${MDDS_BROKER_BASELINE_LIBRARY:-build_ohos/mdds/libmdds.so}"
  if [[ "$BROKER_VARIANT" == overlay ]]; then
    BROKER_SOURCES[libmdds.so]="${MDDS_BROKER_OVERLAY_LIBRARY:-build_ohos/mdds/broker_local_overlay/libmdds.so}"
  fi
  BROKER_SOURCES[librmw_mdds.so]="${MDDS_BROKER_RMW_LIBRARY:-build_ohos/rmw_mdds/librmw_mdds.so}"
  BROKER_SOURCES[mdds_broker_local_test_daemon]=build_ohos/mdds/mdds_broker_local_test_daemon
  BROKER_SOURCES[broker_local_ros_probe.py]=scripts/mdds_e2e/broker_local_ros_probe.py
  BROKER_SOURCES[broker_local_run.py]=scripts/mdds_e2e/broker_local_run.py
  BROKER_SOURCES[board_graph_ownership.py]=scripts/mdds_e2e/board_graph_ownership.py
  BROKER_SOURCES[profile.env]=install_ohos/share/rmw_mdds/config/ohos_dsoftbus.env
  local name
  for name in "${!BROKER_SOURCES[@]}"; do
    BROKER_HASHES[$name]=$(graph_sha "${BROKER_SOURCES[$name]}") || return 1
    [[ "${BROKER_HASHES[$name]}" =~ ^[0-9a-f]{64}$ ]] || return 1
  done
}

broker_local_verify_inputs() {
  local name remote
  for name in "${!BROKER_SOURCES[@]}"; do
    [[ "$(graph_sha "${BROKER_SOURCES[$name]}")" == "${BROKER_HASHES[$name]}" ]] || return 1
    remote=$(graph_remote_sha "$BROKER_BOARD" "${BROKER_DESTINATIONS[$name]}") || return 1
    [[ "$remote" == "${BROKER_HASHES[$name]}" ]] || return 1
  done
}

broker_local_child_record() {
  local role="$1" record line attempt pid start
  record="$MDDS_OWNED_REMOTE_DIR/$role.child.pid"
  for ((attempt=0; attempt<30; attempt++)); do
    line=$(shell "$BROKER_BOARD" "if test -f '$record' && test ! -L '$record'; then cat '$record'; fi" | tr -d '\r') || true
    if [[ "$line" =~ ^MDDS_OWNED_PROCESS\ RUN_ID=$MDDS_OWNED_RUN_ID\ TAG=${role}_child\ PID=([0-9]+)\ START=([0-9]+)$ ]]; then
      pid="${BASH_REMATCH[1]}"; start="${BASH_REMATCH[2]}"
      BROKER_PIDS[$role]="$pid"
      MDDS_OWNED_TRACKED=("$BROKER_BOARD|$pid|$start|$record|${role}_child|$role.child" "${MDDS_OWNED_TRACKED[@]}")
      # Preserve immutable identity evidence before owned cleanup removes records.
      graph_fetch_verified "$BROKER_BOARD" "$record" "$LOGDIR/$role.child.pid"
      return
    fi
    sleep 0.2
  done
  echo "ERROR: no exact child record for $role" >&2
  return 1
}

broker_local_launch() {
  local role="$1" payload argument
  shift
  broker_local_verify_inputs || return 1
  payload="python3.12 $(mdds_owned_quote "$REMOTE_RUNNER") supervise --run-id $MDDS_OWNED_RUN_ID --fixture-role $role --status-file $(mdds_owned_quote "$MDDS_OWNED_REMOTE_DIR/$role.status.json") --child-record $(mdds_owned_quote "$MDDS_OWNED_REMOTE_DIR/$role.child.pid") --command"
  for argument in "$@"; do payload+=" $(mdds_owned_quote "$argument")"; done
  mdds_owned_launch "$BROKER_BOARD" "$BROKER_ENVS" "$payload" "$role.log" || return 1
  broker_local_child_record "$role"
}

broker_local_wait_status() {
  local role="$1" seconds="${2:-160}" end ready
  end=$(( $(date +%s) + seconds ))
  while (( $(date +%s) < end )); do
    ready=$(shell "$BROKER_BOARD" "if test -f '$MDDS_OWNED_REMOTE_DIR/$role.status.json' && test ! -L '$MDDS_OWNED_REMOTE_DIR/$role.status.json'; then printf BROKER_WAIT_COMPLETE; fi" | tr -d '\r') || true
    [[ "$ready" != BROKER_WAIT_COMPLETE ]] || return 0
    sleep 0.5
  done
  echo "ERROR: missing actual child exit status for $role" >&2
  return 1
}

broker_local_collect() {
  local role="$1"
  graph_fetch_verified "$BROKER_BOARD" "$MDDS_OWNED_REMOTE_DIR/$role.status.json" "$LOGDIR/$role.status.json" || return 1
  graph_fetch_verified "$BROKER_BOARD" "$MDDS_OWNED_REMOTE_DIR/$role.log" "$LOGDIR/$role.log" || return 1
  "$GRAPH_HOST_PYTHON" scripts/mdds_e2e/broker_local_run.py verify \
    --run-id "$MDDS_OWNED_RUN_ID" --fixture-role "$role" --libdir "$REMOTE_LIB" \
    --socket "$BROKER_SOCKET" --count "$BROKER_COUNT" --log "$LOGDIR/$role.log" \
    --status-file "$LOGDIR/$role.status.json" --child-record "$LOGDIR/$role.child.pid"
}

broker_local_wait_daemon() {
  local end ready terminal socket_ready expected
  end=$(( $(date +%s) + 15 ))
  expected="MDBC_LOCAL_READY mode=experimental-local-only run_id=$MDDS_OWNED_RUN_ID domain=49 uid=0 socket=$BROKER_SOCKET pid=${BROKER_PIDS[daemon]} "
  while (( $(date +%s) < end )); do
    ready=$(shell "$BROKER_BOARD" "grep '^MDBC_LOCAL_READY ' '$MDDS_OWNED_REMOTE_DIR/daemon.log' 2>/dev/null || true" | tr -d '\r') || true
    if [[ "$ready" == "$expected"* && "$ready" != *$'\n'* ]]; then
      socket_ready=$(shell "$BROKER_BOARD" "if test -S '$BROKER_SOCKET' && test ! -L '$BROKER_SOCKET'; then printf BROKER_SOCKET_READY; fi" | tr -d '\r') || true
      [[ "$socket_ready" != BROKER_SOCKET_READY ]] || return 0
    fi
    terminal=$(shell "$BROKER_BOARD" "if test -f '$MDDS_OWNED_REMOTE_DIR/daemon.status.json'; then printf BROKER_DAEMON_EXITED; fi" | tr -d '\r') || true
    [[ "$terminal" != BROKER_DAEMON_EXITED ]] || break
    sleep 0.2
  done
  echo 'ERROR: daemon did not report its exact owned READY/socket' >&2
  return 1
}

broker_local_stop_daemon() {
  ((BROKER_DAEMON_STARTED == 1)) || return 0
  if ((BROKER_DAEMON_STOPPED == 0)); then
    mdds_owned_stop_log "$BROKER_BOARD" daemon.child || return 1
    BROKER_DAEMON_STOPPED=1
  fi
  broker_local_wait_status daemon 20 || return 1
  if ((BROKER_DAEMON_COLLECTED == 0)); then
    broker_local_collect daemon || return 1
    BROKER_DAEMON_COLLECTED=1
  fi
}

broker_local_exit() {
  local rc=$?
  trap - EXIT
  trap '' INT TERM HUP
  broker_local_stop_daemon || rc=1
  mdds_owned_finish || rc=1
  printf 'BROKER_LOCAL_RUN_RESULT %s RUN_ID=%s VARIANT=%s SCENARIO=%s PHYSICAL_DSOFTBUS_PROVEN=false EVIDENCE=%s\n' \
    "$([[ $rc -eq 0 ]] && echo PASS || echo FAIL)" "$MDDS_OWNED_RUN_ID" "$BROKER_VARIANT" "$BROKER_SCENARIO" "$LOGDIR"
  exit "$rc"
}

broker_local_main() {
  local name ready result=0
  broker_local_parse_args "$@" || return 2
  broker_local_freeze_inputs || { echo 'ERROR: missing/invalid frozen local broker input' >&2; return 1; }
  LOGDIR="$BROKER_LOGROOT/$MDDS_RUN_ID"
  [[ ! -e "$LOGDIR" && ! -L "$LOGDIR" ]] || return 1
  mkdir -p "$LOGDIR"
  mdds_owned_init broker_local "$BROKER_BOARD" || return 1
  trap broker_local_exit EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM HUP
  REMOTE_LIB="$MDDS_OWNED_REMOTE_DIR/lib"
  REMOTE_RUNNER="$MDDS_OWNED_REMOTE_DIR/broker_local_run.py"
  BROKER_SOCKET="$MDDS_OWNED_REMOTE_DIR/b.sock"
  ready=$(shell "$BROKER_BOARD" "if (umask 077; mkdir '$REMOTE_LIB') && test -d '$REMOTE_LIB' && test ! -L '$REMOTE_LIB'; then printf BROKER_STAGE_READY; fi" | tr -d '\r')
  [[ "$ready" == BROKER_STAGE_READY ]] || return 1
  for name in "${!BROKER_SOURCES[@]}"; do
    case "$name" in libmdds.so|librmw_mdds.so) BROKER_DESTINATIONS[$name]="$REMOTE_LIB/$name" ;;
      *) BROKER_DESTINATIONS[$name]="$MDDS_OWNED_REMOTE_DIR/$name" ;; esac
    graph_stage_artifact "$BROKER_BOARD" "${BROKER_SOURCES[$name]}" "${BROKER_DESTINATIONS[$name]}" "${BROKER_HASHES[$name]}" || return 1
    printf '%s  %s\n' "${BROKER_HASHES[$name]}" "${BROKER_DESTINATIONS[$name]}" >> "$LOGDIR/inputs.sha256"
  done
  ready=$(shell "$BROKER_BOARD" ". $DEVICE_DIR/env.sh || exit 70; python3.12 '$REMOTE_RUNNER' mark-executable --run-id '$MDDS_OWNED_RUN_ID' --artifact '$MDDS_OWNED_REMOTE_DIR/mdds_broker_local_test_daemon' --sha256 '${BROKER_HASHES[mdds_broker_local_test_daemon]}'" | tr -d '\r')
  [[ "$ready" == "BROKER_EXEC_READY sha256=${BROKER_HASHES[mdds_broker_local_test_daemon]}" ]] || return 1
  BROKER_ENVS=". $DEVICE_DIR/env.sh || exit 70; . $MDDS_OWNED_REMOTE_DIR/profile.env || exit 70; export RMW_IMPLEMENTATION=rmw_mdds; export ROS_DOMAIN_ID=49; export LD_LIBRARY_PATH=$REMOTE_LIB:\$LD_LIBRARY_PATH; export MDDS_BROKER_LOCAL_TEST_SOCKET=$BROKER_SOCKET;"
  broker_local_launch daemon "$MDDS_OWNED_REMOTE_DIR/mdds_broker_local_test_daemon" \
    --socket "$BROKER_SOCKET" --run-id "$MDDS_OWNED_RUN_ID" --domain 49 --uid 0 --run-ms 600000 || return 1
  BROKER_DAEMON_STARTED=1
  broker_local_wait_daemon || return 1
  if [[ "$BROKER_SCENARIO" == contexts || "$BROKER_SCENARIO" == all ]]; then
    broker_local_launch contexts python3.12 "$MDDS_OWNED_REMOTE_DIR/broker_local_ros_probe.py" \
      --mode contexts --run-id "$MDDS_OWNED_RUN_ID" --overlay-lib "$REMOTE_LIB/libmdds.so" --count "$BROKER_COUNT" || return 1
    broker_local_wait_status contexts || return 1
    broker_local_collect contexts || result=1
  fi
  if [[ "$result" == 0 && ( "$BROKER_SCENARIO" == processes || "$BROKER_SCENARIO" == all ) ]]; then
    for name in alpha beta; do
      broker_local_launch "$name" python3.12 "$MDDS_OWNED_REMOTE_DIR/broker_local_ros_probe.py" \
        --mode worker --role "$name" --run-id "$MDDS_OWNED_RUN_ID" --overlay-lib "$REMOTE_LIB/libmdds.so" --count "$BROKER_COUNT" || return 1
    done
    broker_local_wait_status alpha || return 1
    broker_local_wait_status beta || return 1
    broker_local_collect alpha || result=1
    broker_local_collect beta || result=1
    "$GRAPH_HOST_PYTHON" scripts/mdds_e2e/broker_local_run.py pair --run-id "$MDDS_OWNED_RUN_ID" \
      --alpha-status "$LOGDIR/alpha.status.json" --beta-status "$LOGDIR/beta.status.json" || result=1
  fi
  broker_local_verify_inputs || result=1
  return "$result"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  broker_local_main "$@"
fi
