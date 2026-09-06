#!/usr/bin/env bash
# Same-Context node lifetime only; parent owns board execution and RED/GREEN.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
source scripts/run_mdds_graph_ownership.sh

shell() {
  timeout --kill-after=0.2s "${LIFETIME_HDC_SECONDS:-10}" "$HDC" -t "$1" shell "$2" </dev/null
}

lifetime_parse_args() {
  LIFETIME_BOARD="$BOARD_A"
  LIFETIME_DOMAIN=51
  LIFETIME_WAIT_SECONDS=120
  LIFETIME_PACKAGE="install_ohos/Lib/site-packages/rclpy"
  LIFETIME_NATIVE=""
  LIFETIME_TOKEN="build_ohos/mdds/mdds_token_exec"
  while (($#)); do
    case "$1" in
      --board|--domain|--wait-seconds|--package|--native|--token|--run-id)
        (($# >= 2)) || return 2
        case "$1" in
          --board) LIFETIME_BOARD="$2" ;;
          --domain) LIFETIME_DOMAIN="$2" ;;
          --wait-seconds) LIFETIME_WAIT_SECONDS="$2" ;;
          --package) LIFETIME_PACKAGE="$2" ;;
          --native) LIFETIME_NATIVE="$2" ;;
          --token) LIFETIME_TOKEN="$2" ;;
          --run-id) MDDS_RUN_ID="$2" ;;
        esac
        shift 2 ;;
      *) echo "ERROR: unknown lifetime option: $1" >&2; return 2 ;;
    esac
  done
  [[ "$LIFETIME_BOARD" =~ ^[A-Za-z0-9_.-]+$ ]] || return 2
  [[ "$LIFETIME_DOMAIN" =~ ^[1-9][0-9]{0,2}$ && "$LIFETIME_DOMAIN" -le 232 ]] || return 2
  [[ "$LIFETIME_WAIT_SECONDS" =~ ^[1-9][0-9]{1,2}$ && "$LIFETIME_WAIT_SECONDS" -le 300 ]] || return 2
  if [[ -n "${MDDS_RUN_ID:-}" ]]; then
    [[ "$MDDS_RUN_ID" =~ ^[A-Za-z0-9_][A-Za-z0-9_.-]{0,95}$ ]] || return 2
  fi
}

lifetime_verify_inputs() {
  local name actual
  for name in "${!LIFETIME_HASHES[@]}"; do
    [[ "$(graph_sha "$LOGDIR/$name")" == "${LIFETIME_HASHES[$name]}" ]] || return 1
    actual=$(graph_remote_sha "$LIFETIME_BOARD" "${LIFETIME_REMOTE[$name]}") || return 1
    [[ "$actual" == "${LIFETIME_HASHES[$name]}" ]] || return 1
  done
}

lifetime_register_child() {
  local line record="$MDDS_OWNED_REMOTE_DIR/lifetime.child.pid" attempt pid start
  local LIFETIME_HDC_SECONDS=3
  for ((attempt=0; attempt<20; attempt++)); do
    line=$(shell "$LIFETIME_BOARD" "if test -f '$record' && test ! -L '$record'; then cat '$record'; fi" | tr -d '\r') || line=''
    if [[ "$line" =~ ^MDDS_OWNED_PROCESS\ RUN_ID=$MDDS_OWNED_RUN_ID\ TAG=type_lifetime_child\ PID=([1-9][0-9]*)\ START=([1-9][0-9]*)$ ]]; then
      pid="${BASH_REMATCH[1]}"; start="${BASH_REMATCH[2]}"
      MDDS_OWNED_TRACKED=("$LIFETIME_BOARD|$pid|$start|$record|type_lifetime_child|lifetime.child" "${MDDS_OWNED_TRACKED[@]}")
      graph_fetch_verified "$LIFETIME_BOARD" "$record" "$LOGDIR/lifetime.child.pid"
      return
    fi
    sleep 0.2
  done
  echo 'ERROR: missing real supervised lifetime child ownership record' >&2
  return 1
}

lifetime_wait_status() {
  local end=$((SECONDS + LIFETIME_WAIT_SECONDS)) ready remaining progress=$SECONDS
  local LIFETIME_HDC_SECONDS=10
  while ((SECONDS < end)); do
    remaining=$((end - SECONDS))
    ((remaining > 1)) || break
    LIFETIME_HDC_SECONDS=$((remaining > 11 ? 10 : remaining - 1))
    ready=$(shell "$LIFETIME_BOARD" "if test -f '$MDDS_OWNED_REMOTE_DIR/lifetime.status.json' && test ! -L '$MDDS_OWNED_REMOTE_DIR/lifetime.status.json'; then printf TYPE_LIFETIME_TERMINAL; fi" | tr -d '\r') || ready=''
    [[ "$ready" != TYPE_LIFETIME_TERMINAL ]] || return 0
    if ((SECONDS - progress >= 30)); then
      printf 'TYPE_LIFETIME_WAIT RUN_ID=%s REMAINING_SECONDS=%s\n' "$MDDS_OWNED_RUN_ID" "$remaining"
      progress=$SECONDS
    fi
    sleep 0.5
  done
  echo 'ERROR: no real Python teardown/exit status within lifetime deadline' >&2
  return 1
}

lifetime_exit() {
  local rc=$?
  trap - EXIT
  trap '' INT TERM HUP
  mdds_owned_finish || rc=1
  # Preserve diagnostics for early token/import failures after owned processes
  # are stopped. Missing status is an infrastructure failure, never TDD RED.
  if [[ ! -e "$LOGDIR/lifetime.log" ]]; then
    graph_fetch_verified "$LIFETIME_BOARD" "$MDDS_OWNED_REMOTE_DIR/lifetime.log" "$LOGDIR/lifetime.log" || true
  fi
  if [[ ! -e "$LOGDIR/lifetime.status.json" ]]; then
    graph_fetch_verified "$LIFETIME_BOARD" "$MDDS_OWNED_REMOTE_DIR/lifetime.status.json" "$LOGDIR/lifetime.status.json" || true
  fi
  printf 'TYPE_LIFETIME_RUN_RESULT %s RUN_ID=%s DOMAIN=%s CROSS_BOARD_PROVEN=false EVIDENCE=%s\n' \
    "$([[ "$rc" == 0 ]] && echo PASS || echo FAIL)" "$MDDS_OWNED_RUN_ID" "$LIFETIME_DOMAIN" "$LOGDIR"
  exit "$rc"
}

lifetime_main() {
  lifetime_parse_args "$@" || return 2
  MDDS_RUN_ID="${MDDS_RUN_ID:-td_$(date +%Y%m%dT%H%M%S)_${RANDOM}}"
  LOGDIR="ohos_test_logs/type_description_lifetime/$MDDS_RUN_ID"
  [[ ! -e "$LOGDIR" && ! -L "$LOGDIR" ]] || return 1
  mkdir -p "$LOGDIR"
  local name source ready result=0
  local -a pack_args=()
  local -A sources=(
    [type_description_lifetime.py]=scripts/mdds_e2e/type_description_lifetime.py
    [board_graph_ownership.py]=scripts/mdds_e2e/board_graph_ownership.py
    [broker_local_run.py]=scripts/mdds_e2e/broker_local_run.py
    [test_type_description_service_lifetime.py]=src/ros2/rclpy/rclpy/test/test_type_description_service_lifetime.py
    [profile.env]=install_ohos/share/rmw_mdds/config/ohos_dsoftbus.env
    [mdds_token_exec]="$LIFETIME_TOKEN"
    [libmdds.so]=install_ohos/lib/libmdds.so
    [librmw_mdds.so]=install_ohos/lib/librmw_mdds.so
  )
  declare -gA LIFETIME_HASHES=() LIFETIME_REMOTE=()
  # All local inputs are frozen before acquiring a board lock or launching.
  for name in "${!sources[@]}"; do
    source="${sources[$name]}"
    [[ -f "$source" && ! -L "$source" ]] || { echo "ERROR: invalid input $source" >&2; return 1; }
    cp -- "$source" "$LOGDIR/$name"
  done
  [[ -z "$LIFETIME_NATIVE" ]] || pack_args=(--native "$LIFETIME_NATIVE")
  "$GRAPH_HOST_PYTHON" -B "$LOGDIR/type_description_lifetime.py" pack \
    --package "$LIFETIME_PACKAGE" "${pack_args[@]}" --archive "$LOGDIR/rclpy_overlay.tar" --manifest "$LOGDIR/rclpy_package.json"
  "$GRAPH_HOST_PYTHON" -B "$LOGDIR/type_description_lifetime.py" plan \
    --inputs "$LOGDIR" --run-id "$MDDS_RUN_ID" --domain "$LIFETIME_DOMAIN" --output "$LOGDIR/lifetime.plan.json"
  for name in "${!sources[@]}" rclpy_overlay.tar lifetime.plan.json; do
    LIFETIME_HASHES[$name]=$(graph_sha "$LOGDIR/$name") || return 1
    [[ "${LIFETIME_HASHES[$name]}" =~ ^[0-9a-f]{64}$ ]] || return 1
    printf '%s  %s\n' "${LIFETIME_HASHES[$name]}" "$name" >> "$LOGDIR/inputs.sha256"
  done
  mdds_owned_init type_lifetime "$LIFETIME_BOARD" || return 1
  trap lifetime_exit EXIT
  trap 'exit 130' INT
  trap 'exit 143' TERM HUP
  local remote_lib="$MDDS_OWNED_REMOTE_DIR/lib" remote_helper="$MDDS_OWNED_REMOTE_DIR/type_description_lifetime.py"
  ready=$(shell "$LIFETIME_BOARD" "if (umask 077; mkdir '$remote_lib') && test -d '$remote_lib' && test ! -L '$remote_lib'; then printf TYPE_LIFETIME_LIB_READY; fi" | tr -d '\r')
  [[ "$ready" == TYPE_LIFETIME_LIB_READY ]] || return 1
  for name in "${!LIFETIME_HASHES[@]}"; do
    case "$name" in libmdds.so|librmw_mdds.so) LIFETIME_REMOTE[$name]="$remote_lib/$name" ;;
      *) LIFETIME_REMOTE[$name]="$MDDS_OWNED_REMOTE_DIR/$name" ;; esac
    graph_stage_artifact "$LIFETIME_BOARD" "$LOGDIR/$name" "${LIFETIME_REMOTE[$name]}" "${LIFETIME_HASHES[$name]}" || return 1
  done
  ready=$(shell "$LIFETIME_BOARD" ". $DEVICE_DIR/env.sh || exit 70; python3.12 -B '$remote_helper' prepare --run-root '$MDDS_OWNED_REMOTE_DIR'" | tr -d '\r')
  [[ "$ready" =~ ^TYPE_DESCRIPTION_OVERLAY_READY\ native=_rclpy_pybind11[A-Za-z0-9_.-]*\.so$ ]] || return 1
  lifetime_verify_inputs || return 1
  local renvs=". $DEVICE_DIR/env.sh || exit 70; . $MDDS_OWNED_REMOTE_DIR/profile.env || exit 70; export MDDS_TOKEN_EXEC=$MDDS_OWNED_REMOTE_DIR/mdds_token_exec; export LD_LIBRARY_PATH=$remote_lib:\${LD_LIBRARY_PATH:-}; export PYTHONPATH=$MDDS_OWNED_REMOTE_DIR/python:\${PYTHONPATH:-}; export RMW_IMPLEMENTATION=rmw_mdds; export ROS_DOMAIN_ID=$LIFETIME_DOMAIN; export MDDS_DEBUG=1; export PYTHONDONTWRITEBYTECODE=1; unset MDDS_BROKER_LOCAL_TEST_SOCKET;"
  # mdds_owned_launch execs the exact run-owned token launcher before this
  # supervisor. The supervisor independently uses it for its tracked child.
  mdds_owned_launch "$LIFETIME_BOARD" "$renvs" \
    "python3.12 -B $remote_helper supervise --run-root $MDDS_OWNED_REMOTE_DIR" lifetime.log || return 1
  lifetime_register_child || return 1
  lifetime_wait_status || return 1
  for name in lifetime.status.json lifetime.log; do
    graph_fetch_verified "$LIFETIME_BOARD" "$MDDS_OWNED_REMOTE_DIR/$name" "$LOGDIR/$name" || return 1
  done
  "$GRAPH_HOST_PYTHON" -B "$LOGDIR/type_description_lifetime.py" verify \
    --inputs "$LOGDIR" --run-root "$MDDS_OWNED_REMOTE_DIR" --output "$LOGDIR/host_verification.json" || result=1
  lifetime_verify_inputs || result=1
  return "$result"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  lifetime_main "$@"
fi
