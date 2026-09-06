#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
source scripts/run_mdds_graph_ownership.sh
variant=service
policy_mode="${MDDS_ROS_PROFILE_MODE:-explicit}"
[[ "$policy_mode" == explicit || "$policy_mode" == implicit ]] || exit 2
cli_batch="${MDDS_ROS_CLI_BATCH:-none}"
[[ "$cli_batch" == none || "$cli_batch" == basic ]] || exit 2
scratch=scripts/mdds_e2e
export MDDS_RUN_ID="${MDDS_RUN_ID:?explicit fresh run ID required}"
[[ "$MDDS_RUN_ID" =~ ^[A-Za-z0-9_]{1,32}$ ]] || exit 2
nonce=$("$GRAPH_HOST_PYTHON" -c 'import secrets; print(secrets.token_hex(16))' | tr -d '\r')
mdds_owned_init ros_broker "$BOARD_A" "$BOARD_B"
trap 'rc=$?; trap - EXIT; mdds_owned_finish || rc=1; exit "$rc"' EXIT
LOGDIR="ohos_test_logs/ros_broker/$MDDS_OWNED_RUN_ID"
[[ ! -e "$LOGDIR" ]]; mkdir -p "$LOGDIR"
"$GRAPH_HOST_PYTHON" scripts/mdds_e2e/ros_type_hashes.py "$(pwd -W)" "$LOGDIR/type_hashes.json"
cp build_ohos/mdds/mdds_broker_daemon "$LOGDIR/"
cp build_ohos/mdds/mdds_token_exec "$LOGDIR/"
cp build_ohos/mdds/libmdds.so "$LOGDIR/"
cp "${MDDS_ROS_RMW_LIBRARY:-build_ohos/rmw_mdds/librmw_mdds.so}" "$LOGDIR/librmw_mdds.so"
cp scripts/mdds_e2e/ros_broker_probe.py "$LOGDIR/"
cp scripts/mdds_e2e/{type_description_lifetime,broker_local_ros_probe}.py "$LOGDIR/"
cp src/ros2/rmw_mdds/rmw_mdds/config/ohos_dsoftbus.env "$LOGDIR/profile.env"
"$GRAPH_HOST_PYTHON" scripts/mdds_e2e/type_description_lifetime.py pack --package install_ohos/Lib/site-packages/rclpy --native build_ohos/rclpy/test_rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so --archive "$LOGDIR/rclpy_overlay.tar" --manifest "$LOGDIR/rclpy_package.json"
manifest_sha=$(graph_sha "$LOGDIR/rclpy_package.json")
cp scripts/mdds_e2e/{board_graph_ownership,broker_local_run}.py "$LOGDIR/"
cp "$scratch/ros_broker_supervise.py" "$LOGDIR/ros_broker_supervise.py"
cp src/Jiusi-pys/mdds/scripts/mdds_broker_service.py "$LOGDIR/"
cp scripts/mdds_e2e/{cli_graph_basic,cli_graph_lists,cli_acceptance}.py "$LOGDIR/"
cp scripts/mdds_e2e/cli_acceptance_manifest.json "$LOGDIR/"
printf '%s\n' "$nonce" > "$LOGDIR/nonce"
printf '%s\n' "$policy_mode" > "$LOGDIR/policy_mode"
for board in "$BOARD_A" "$BOARD_B"; do
  ready=$(shell "$board" "mkdir '$MDDS_OWNED_REMOTE_DIR/lib' && printf LIB_READY" | tr -d '\r'); [[ "$ready" == LIB_READY ]]
  for name in mdds_broker_daemon mdds_token_exec board_graph_ownership.py broker_local_run.py ros_broker_supervise.py mdds_broker_service.py libmdds.so librmw_mdds.so ros_broker_probe.py broker_local_ros_probe.py type_description_lifetime.py profile.env rclpy_overlay.tar rclpy_package.json type_hashes.json policy_mode cli_graph_basic.py cli_graph_lists.py cli_acceptance.py cli_acceptance_manifest.json; do
    hash=$(graph_sha "$LOGDIR/$name"); destination="$MDDS_OWNED_REMOTE_DIR/$name"; if [[ "$name" == libmdds.so || "$name" == librmw_mdds.so ]]; then destination="$MDDS_OWNED_REMOTE_DIR/lib/$name"; fi; graph_stage_artifact "$board" "$LOGDIR/$name" "$destination" "$hash"
    printf '%s  %s\n' "$hash" "$name" >> "$LOGDIR/inputs_$board.sha256"
  done
  for name in mdds_broker_daemon mdds_token_exec; do
    hash=$(graph_sha "$LOGDIR/$name")
    output=$(shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; python3.12 '$MDDS_OWNED_REMOTE_DIR/broker_local_run.py' mark-executable --run-id '$MDDS_OWNED_RUN_ID' --artifact '$MDDS_OWNED_REMOTE_DIR/$name' --sha256 '$hash'" | tr -d '\r')
    [[ "$output" == "BROKER_EXEC_READY sha256=$hash" ]]
  done
done
for board in "$BOARD_A" "$BOARD_B"; do
  ready=$(shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; export MDDS_RCLPY_MANIFEST_SHA='$manifest_sha'; python3.12 '$MDDS_OWNED_REMOTE_DIR/ros_broker_supervise.py' '$MDDS_OWNED_REMOTE_DIR' '$MDDS_OWNED_RUN_ID' prepare '$board' x '$nonce' '$variant'" | tr -d '\r')
  [[ "$ready" == "BROKER_RCLPY_READY manifest_sha256=$manifest_sha" ]] || { printf '%s\n' "$ready" >&2; exit 1; }
done
launch_role() {
  local board="$1" peer="$2" role="$3" record line found=false
  record="$MDDS_OWNED_REMOTE_DIR/$role.child.pid"
  local policy_env=". '$MDDS_OWNED_REMOTE_DIR/profile.env' || exit 70;"
  if [[ "$policy_mode" == implicit ]]; then
    policy_env="unset MDDS_DEPLOYMENT_PROFILE MDDS_TRANSPORT ROS_LOCALHOST_ONLY; export RMW_IMPLEMENTATION=rmw_mdds; export ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT;"
  fi
  mdds_owned_launch "$board" ". '$DEVICE_DIR/env.sh' || exit 70; $policy_env export LD_LIBRARY_PATH='$MDDS_OWNED_REMOTE_DIR/lib':\$LD_LIBRARY_PATH; export PYTHONPATH='$MDDS_OWNED_REMOTE_DIR/python':\$PYTHONPATH; export PYTHONDONTWRITEBYTECODE=1; export MDDS_BROKER_ROOT='$MDDS_OWNED_REMOTE_DIR/brokers'; export MDDS_RCLPY_MANIFEST_SHA='$manifest_sha'; export ROS_DOMAIN_ID=175; export MDDS_DEBUG=1;" \
    "python3.12 '$MDDS_OWNED_REMOTE_DIR/ros_broker_supervise.py' '$MDDS_OWNED_REMOTE_DIR' '$MDDS_OWNED_RUN_ID' '$role' '$board' '$peer' '$nonce' '$variant'" "$role.log"
  for ((attempt=0;attempt<30;++attempt)); do
    line=$(shell "$board" "if test -f '$record' && test ! -L '$record'; then cat '$record'; fi" | tr -d '\r')
    if [[ "$line" =~ ^MDDS_OWNED_PROCESS\ RUN_ID=$MDDS_OWNED_RUN_ID\ TAG=${role}_child\ PID=([0-9]+)\ START=([0-9]+)$ ]]; then
      MDDS_OWNED_TRACKED=("$board|${BASH_REMATCH[1]}|${BASH_REMATCH[2]}|$record|${role}_child|$role.child" "${MDDS_OWNED_TRACKED[@]}")
      found=true;break
    fi
    sleep 0.2
  done
  [[ "$found" == true ]]
  graph_fetch_verified "$board" "$record" "$LOGDIR/$board.$role.child.pid"
}
launch_role "$BOARD_A" "$BOARD_B" daemon
launch_role "$BOARD_B" "$BOARD_A" daemon
for board in "$BOARD_A" "$BOARD_B"; do
  ready=false
  for ((attempt=0;attempt<30;++attempt)); do
    value=$(shell "$board" "if test -S '$MDDS_OWNED_REMOTE_DIR/brokers/d175/b.sock' && grep -q '^MDBC_.*_READY ' '$MDDS_OWNED_REMOTE_DIR/daemon.log'; then printf DAEMON_READY; fi" | tr -d '\r')
    if [[ "$value" == DAEMON_READY ]]; then ready=true;break;fi
    sleep 0.2
  done
  [[ "$ready" == true ]]
done
launch_role "$BOARD_A" "$BOARD_B" ros
launch_role "$BOARD_B" "$BOARD_A" ros
phase_ok=true
for phase in 1 2; do
  marker=phase1.done; [[ "$phase" != 2 ]] || marker=ros.done
  for board in "$BOARD_A" "$BOARD_B"; do
    complete=false
    for ((attempt=0;attempt<100;++attempt)); do
      value=$(shell "$board" "if test -f '$MDDS_OWNED_REMOTE_DIR/$marker'; then cat '$MDDS_OWNED_REMOTE_DIR/$marker'; elif test -f '$MDDS_OWNED_REMOTE_DIR/ros.status.json'; then printf ROS_EXITED; fi" | tr -d '\r')
      if [[ "$value" == "$nonce" ]]; then complete=true;break;fi
      [[ "$value" != ROS_EXITED ]] || break
      sleep 0.5
    done
    if [[ "$complete" != true ]]; then phase_ok=false;break;fi
  done
  [[ "$phase_ok" == true ]] || break
  if [[ "$phase" == 1 ]]; then
    if [[ "$cli_batch" == basic ]]; then
      launch_role "$BOARD_A" "$BOARD_B" cli
      launch_role "$BOARD_B" "$BOARD_A" cli
      for board in "$BOARD_A" "$BOARD_B"; do graph_wait_status "$board" cli; done
    fi
    for board in "$BOARD_A" "$BOARD_B"; do
      for operation in inspect advance; do
        shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; python3.12 '$MDDS_OWNED_REMOTE_DIR/ros_broker_supervise.py' '$MDDS_OWNED_REMOTE_DIR' '$MDDS_OWNED_RUN_ID' '$operation' '$board' x '$nonce' '$variant'" >/dev/null
      done
    done
  fi
done
for board in "$BOARD_A" "$BOARD_B"; do
  if [[ "$board" == "$BOARD_B" && "$phase_ok" == true ]]; then
    shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; python3.12 '$MDDS_OWNED_REMOTE_DIR/ros_broker_supervise.py' '$MDDS_OWNED_REMOTE_DIR' '$MDDS_OWNED_RUN_ID' withdraw '$board' x '$nonce' '$variant'" >/dev/null
    withdrawn=false
    for ((attempt=0;attempt<100;++attempt)); do
      value=$(shell "$board" "if test -f '$MDDS_OWNED_REMOTE_DIR/peer_exit.done'; then cat '$MDDS_OWNED_REMOTE_DIR/peer_exit.done'; elif test -f '$MDDS_OWNED_REMOTE_DIR/ros.status.json'; then printf ROS_EXITED; fi" | tr -d '\r')
      if [[ "$value" == "$nonce" ]]; then withdrawn=true;break;fi
      [[ "$value" != ROS_EXITED ]] || break
      sleep 0.5
    done
    [[ "$withdrawn" == true ]] || phase_ok=false
  fi
  shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; python3.12 '$MDDS_OWNED_REMOTE_DIR/ros_broker_supervise.py' '$MDDS_OWNED_REMOTE_DIR' '$MDDS_OWNED_RUN_ID' finish '$board' x '$nonce' '$variant'" >/dev/null
  graph_wait_status "$board" ros
done
for board in "$BOARD_A" "$BOARD_B"; do mdds_owned_stop_log "$board" daemon.child; graph_wait_status "$board" daemon;done
for board in "$BOARD_A" "$BOARD_B"; do
  for name in ros.log ros.status.json daemon.log daemon.status.json; do
    graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$name" "$LOGDIR/$board.$name"
  done
  if [[ "$phase_ok" == true ]]; then graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/daemon.inspect.json" "$LOGDIR/$board.daemon.inspect.json"; fi
  if [[ "$cli_batch" == basic ]]; then
    graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli.status.json" "$LOGDIR/$board.cli.status.json"
    graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli_graph/results.json" "$LOGDIR/$board.cli.results.json"
    graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli_fixture.json" "$LOGDIR/$board.cli.fixture.json"
    present=$(shell "$board" "if test -f '$MDDS_OWNED_REMOTE_DIR/cli_received.json'; then printf CLI_RECEIVED; fi" | tr -d '\r')
    if [[ "$present" == CLI_RECEIVED ]]; then
      graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli_received.json" "$LOGDIR/$board.cli.received.json"
    fi
    for name in cli_topic_type cli_topic_find cli_service_call cli_topic_info cli_topic_pub cli_topic_echo cli_topic_list_visible cli_topic_list_hidden cli_service_list_visible cli_service_list_hidden; do
      graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli_graph/$name.log" "$LOGDIR/$board.$name.log"
    done
  fi
done
"$GRAPH_HOST_PYTHON" "$scratch/verify_ros_broker.py" "$LOGDIR" "$MDDS_OWNED_RUN_ID" "$variant" "$BOARD_A" "$BOARD_B"
"$GRAPH_HOST_PYTHON" "$scratch/test_ros_broker_receipt.py" "$LOGDIR" > "$LOGDIR/receipt_validation_tests.log" 2>&1
printf 'ROS_BROKER_RECEIPT_TESTS PASS count=11\n'
if [[ "$cli_batch" == basic ]]; then
  "$GRAPH_HOST_PYTHON" scripts/mdds_e2e/verify_cli_graph_basic.py "$LOGDIR" "$MDDS_OWNED_RUN_ID"
fi
