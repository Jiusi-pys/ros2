#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
source scripts/run_mdds_graph_ownership.sh
variant=service
policy_mode="${MDDS_ROS_PROFILE_MODE:-explicit}"
[[ "$policy_mode" == explicit || "$policy_mode" == implicit ]] || exit 2
cli_batch="${MDDS_ROS_CLI_BATCH:-none}"
[[ "$cli_batch" == none || "$cli_batch" == basic || "$cli_batch" == daemon || "$cli_batch" == action || "$cli_batch" == introspection || "$cli_batch" == parameter_read || "$cli_batch" == parameter_write || "$cli_batch" == lifecycle || "$cli_batch" == components || "$cli_batch" == standalone || "$cli_batch" == statistics || "$cli_batch" == bags || "$cli_batch" == bag_transform || "$cli_batch" == bag_burst ]] || exit 2
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
cp scripts/mdds_e2e/{cli_graph_basic,cli_graph_lists,cli_daemon,cli_daemon_guard,cli_service_graph,cli_node_info,cli_action,cli_service_echo,cli_service_events,cli_parameters,cli_parameter_changes,cli_lifecycle,board_lifecycle_fixture,cli_components,board_component_probe,component_process,cli_standalone,board_standalone_probe,bag_contract,bag_transform,bag_burst,topic_statistics,board_topic_statistics,cli_topic_statistics,board_bag_probe,cli_bag,bag_record,cli_acceptance}.py "$LOGDIR/"
cp scripts/mdds_e2e/cli_acceptance_manifest.json "$LOGDIR/"
printf '%s\n' "$nonce" > "$LOGDIR/nonce"
printf '%s\n' "$policy_mode" > "$LOGDIR/policy_mode"
printf '%s\n' "$cli_batch" > "$LOGDIR/cli_batch"
if [[ "$cli_batch" == bags || "$cli_batch" == bag_transform || "$cli_batch" == bag_burst ]]; then
  cp install_ohos/lib/librosbag2_storage_sqlite3.so install_ohos/lib/librosbag2_storage_mcap.so "$LOGDIR/"
  cp scripts/mdds_e2e/bag_python_overlay.py "$LOGDIR/"
  cp src/ros2/rosbag2/rosbag2_py/rosbag2_py/__init__.py "$LOGDIR/rosbag_init.py"
  cp src/ros2/rosbag2/rosbag2_py/rosbag2_py/_ohos_plugin_scope.py "$LOGDIR/rosbag_scope.py"
  "$GRAPH_HOST_PYTHON" scripts/mdds_e2e/bag_python_overlay.py pack --package install_ohos/Lib/site-packages/rosbag2_py --init "$LOGDIR/rosbag_init.py" --scope "$LOGDIR/rosbag_scope.py" --output "$LOGDIR/rosbag_manifest.json"
fi
if [[ "$cli_batch" == components || "$cli_batch" == standalone ]]; then
  cp install_ohos/lib/rclcpp_components/component_container "$LOGDIR/component_container"
  cp install_ohos/share/ament_index/resource_index/rclcpp_components/composition "$LOGDIR/composition.components"
  printf 'run-owned component prefix\n' > "$LOGDIR/component_package_marker"
  for name in talker listener node_like_listener server client; do cp "install_ohos/lib/lib${name}_component.so" "$LOGDIR/"; done
fi
for board in "$BOARD_A" "$BOARD_B"; do
  ready=$(shell "$board" "mkdir '$MDDS_OWNED_REMOTE_DIR/lib' && printf LIB_READY" | tr -d '\r'); [[ "$ready" == LIB_READY ]]
  for name in mdds_broker_daemon mdds_token_exec board_graph_ownership.py broker_local_run.py ros_broker_supervise.py mdds_broker_service.py libmdds.so librmw_mdds.so ros_broker_probe.py broker_local_ros_probe.py type_description_lifetime.py profile.env rclpy_overlay.tar rclpy_package.json type_hashes.json policy_mode cli_batch cli_graph_basic.py cli_graph_lists.py cli_daemon.py cli_daemon_guard.py cli_service_graph.py cli_node_info.py cli_action.py cli_service_echo.py cli_service_events.py cli_parameters.py cli_parameter_changes.py cli_lifecycle.py board_lifecycle_fixture.py cli_components.py board_component_probe.py component_process.py cli_standalone.py board_standalone_probe.py bag_contract.py bag_transform.py bag_burst.py topic_statistics.py board_topic_statistics.py cli_topic_statistics.py board_bag_probe.py cli_bag.py bag_record.py cli_acceptance.py cli_acceptance_manifest.json; do
    hash=$(graph_sha "$LOGDIR/$name"); destination="$MDDS_OWNED_REMOTE_DIR/$name"; if [[ "$name" == libmdds.so || "$name" == librmw_mdds.so ]]; then destination="$MDDS_OWNED_REMOTE_DIR/lib/$name"; fi; graph_stage_artifact "$board" "$LOGDIR/$name" "$destination" "$hash"
    printf '%s  %s\n' "$hash" "$name" >> "$LOGDIR/inputs_$board.sha256"
  done
  for name in mdds_broker_daemon mdds_token_exec; do
    hash=$(graph_sha "$LOGDIR/$name")
    output=$(shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; python3.12 '$MDDS_OWNED_REMOTE_DIR/broker_local_run.py' mark-executable --run-id '$MDDS_OWNED_RUN_ID' --artifact '$MDDS_OWNED_REMOTE_DIR/$name' --sha256 '$hash'" | tr -d '\r')
    [[ "$output" == "BROKER_EXEC_READY sha256=$hash" ]]
  done
done
if [[ "$cli_batch" == components || "$cli_batch" == standalone ]]; then
  for board in "$BOARD_A" "$BOARD_B"; do
    prefix="$MDDS_OWNED_REMOTE_DIR/component_prefix"
    ready=$(shell "$board" "mkdir -p '$prefix/lib/rclcpp_components' '$prefix/share/ament_index/resource_index/rclcpp_components' '$prefix/share/ament_index/resource_index/packages' && printf COMPONENT_DIRS_READY" | tr -d '\r'); [[ "$ready" == COMPONENT_DIRS_READY ]]
    graph_stage_artifact "$board" "$LOGDIR/component_container" "$prefix/lib/rclcpp_components/component_container" "$(graph_sha "$LOGDIR/component_container")"
    graph_stage_artifact "$board" "$LOGDIR/composition.components" "$prefix/share/ament_index/resource_index/rclcpp_components/composition" "$(graph_sha "$LOGDIR/composition.components")"
    for name in composition rclcpp_components; do graph_stage_artifact "$board" "$LOGDIR/component_package_marker" "$prefix/share/ament_index/resource_index/packages/$name" "$(graph_sha "$LOGDIR/component_package_marker")"; done
    for name in talker listener node_like_listener server client; do graph_stage_artifact "$board" "$LOGDIR/lib${name}_component.so" "$prefix/lib/lib${name}_component.so" "$(graph_sha "$LOGDIR/lib${name}_component.so")"; done
    ready=$(shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; python3.12 '$MDDS_OWNED_REMOTE_DIR/broker_local_run.py' mark-executable --run-id '$MDDS_OWNED_RUN_ID' --artifact '$prefix/lib/rclcpp_components/component_container' --sha256 '$(graph_sha "$LOGDIR/component_container")'" | tr -d '\r'); [[ "$ready" == "BROKER_EXEC_READY sha256=$(graph_sha "$LOGDIR/component_container")" ]]
  done
fi
if [[ "$cli_batch" == bags || "$cli_batch" == bag_transform || "$cli_batch" == bag_burst ]]; then
  for board in "$BOARD_A" "$BOARD_B"; do
    for name in bag_python_overlay.py rosbag_init.py rosbag_scope.py rosbag_manifest.json; do
      graph_stage_artifact "$board" "$LOGDIR/$name" "$MDDS_OWNED_REMOTE_DIR/$name" "$(graph_sha "$LOGDIR/$name")"
    done
  done
fi
for board in "$BOARD_A" "$BOARD_B"; do
  ready=$(shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; export MDDS_RCLPY_MANIFEST_SHA='$manifest_sha'; python3.12 '$MDDS_OWNED_REMOTE_DIR/ros_broker_supervise.py' '$MDDS_OWNED_REMOTE_DIR' '$MDDS_OWNED_RUN_ID' prepare '$board' x '$nonce' '$variant'" | tr -d '\r')
  [[ "$ready" == "BROKER_RCLPY_READY manifest_sha256=$manifest_sha" ]] || { printf '%s\n' "$ready" >&2; exit 1; }
  if [[ "$cli_batch" == bags || "$cli_batch" == bag_transform || "$cli_batch" == bag_burst ]]; then
    rosbag_sha=$(graph_sha "$LOGDIR/rosbag_manifest.json")
    ready=$(shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; python3.12 '$MDDS_OWNED_REMOTE_DIR/bag_python_overlay.py' prepare --root '$MDDS_OWNED_REMOTE_DIR' --manifest-sha '$rosbag_sha'" | tr -d '\r')
    [[ "$ready" == "ROSBAG_OVERLAY_READY $rosbag_sha" ]] || { printf '%s\n' "$ready" >&2; exit 1; }
  fi
done
launch_role() {
  local board="$1" peer="$2" role="$3" record line found=false
  record="$MDDS_OWNED_REMOTE_DIR/$role.child.pid"
  local policy_env=". '$MDDS_OWNED_REMOTE_DIR/profile.env' || exit 70;"
  if [[ "$policy_mode" == implicit ]]; then
    policy_env="unset MDDS_DEPLOYMENT_PROFILE MDDS_TRANSPORT ROS_LOCALHOST_ONLY; export RMW_IMPLEMENTATION=rmw_mdds; export ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT;"
  fi
  if [[ "$cli_batch" == components || "$cli_batch" == standalone ]]; then
    policy_env="$policy_env export AMENT_PREFIX_PATH='$MDDS_OWNED_REMOTE_DIR/component_prefix':\$AMENT_PREFIX_PATH;"
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
    if [[ "$cli_batch" == components ]]; then
      launch_role "$BOARD_A" "$BOARD_B" container
      launch_role "$BOARD_B" "$BOARD_A" container
    fi
    if [[ "$cli_batch" != none ]]; then
      launch_role "$BOARD_A" "$BOARD_B" cli
      launch_role "$BOARD_B" "$BOARD_A" cli
      if [[ "$cli_batch" == bag_burst ]]; then
        for storage in sqlite3 mcap; do
          for board in "$BOARD_A" "$BOARD_B"; do
            ready=false
            for ((attempt=0;attempt<150;++attempt)); do
              value=$(shell "$board" "if test -f '$MDDS_OWNED_REMOTE_DIR/bag_${storage}_burst.json'; then printf BURST_READY; elif test -f '$MDDS_OWNED_REMOTE_DIR/cli.status.json'; then printf CLI_EXITED; fi" | tr -d '\r')
              if [[ "$value" == BURST_READY ]]; then ready=true;break;fi
              [[ "$value" != CLI_EXITED ]] || break
              sleep 0.2
            done
            [[ "$ready" == true ]] || exit 1
          done
          for board in "$BOARD_A" "$BOARD_B"; do
            shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; python3.12 '$MDDS_OWNED_REMOTE_DIR/ros_broker_supervise.py' '$MDDS_OWNED_REMOTE_DIR' '$MDDS_OWNED_RUN_ID' burst_stop_$storage '$board' x '$nonce' '$variant'" >/dev/null
          done
        done
      fi
      if [[ "$cli_batch" == standalone ]]; then
        for board in "$BOARD_A" "$BOARD_B"; do
          ready=false
          for ((attempt=0;attempt<100;++attempt)); do
            value=$(shell "$board" "if test -f '$MDDS_OWNED_REMOTE_DIR/standalone.ready'; then cat '$MDDS_OWNED_REMOTE_DIR/standalone.ready'; elif test -f '$MDDS_OWNED_REMOTE_DIR/cli.status.json'; then printf CLI_EXITED; fi" | tr -d '\r')
            if [[ "$value" == "$nonce" ]]; then ready=true;break;fi
            [[ "$value" != CLI_EXITED ]] || break
            sleep 0.2
          done
          [[ "$ready" == true ]] || exit 1
        done
        for board in "$BOARD_A" "$BOARD_B"; do
          shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; python3.12 '$MDDS_OWNED_REMOTE_DIR/ros_broker_supervise.py' '$MDDS_OWNED_REMOTE_DIR' '$MDDS_OWNED_RUN_ID' standalone_start '$board' x '$nonce' '$variant'" >/dev/null
        done
        for board in "$BOARD_A" "$BOARD_B"; do
          ready=false
          for ((attempt=0;attempt<100;++attempt)); do
            value=$(shell "$board" "if test -f '$MDDS_OWNED_REMOTE_DIR/standalone_received.json'; then printf STANDALONE_RECEIVED; elif test -f '$MDDS_OWNED_REMOTE_DIR/cli.status.json'; then printf CLI_EXITED; fi" | tr -d '\r')
            if [[ "$value" == STANDALONE_RECEIVED ]]; then ready=true;break;fi
            [[ "$value" != CLI_EXITED ]] || break
            sleep 0.2
          done
          [[ "$ready" == true ]] || exit 1
        done
        for board in "$BOARD_A" "$BOARD_B"; do
          shell "$board" ". '$DEVICE_DIR/env.sh' || exit 70; python3.12 '$MDDS_OWNED_REMOTE_DIR/ros_broker_supervise.py' '$MDDS_OWNED_REMOTE_DIR' '$MDDS_OWNED_RUN_ID' standalone_stop '$board' x '$nonce' '$variant'" >/dev/null
        done
      fi
      for board in "$BOARD_A" "$BOARD_B"; do graph_wait_status "$board" cli; done
      if [[ "$cli_batch" == components ]]; then
        for board in "$BOARD_A" "$BOARD_B"; do mdds_owned_stop_log "$board" container.child; graph_wait_status "$board" container; done
      fi
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
  elif [[ "$cli_batch" == daemon || "$cli_batch" == action || "$cli_batch" == introspection || "$cli_batch" == parameter_read || "$cli_batch" == parameter_write || "$cli_batch" == lifecycle || "$cli_batch" == components || "$cli_batch" == standalone || "$cli_batch" == statistics || "$cli_batch" == bags || "$cli_batch" == bag_transform || "$cli_batch" == bag_burst ]]; then
    graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli.status.json" "$LOGDIR/$board.cli.status.json"
    graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli.log" "$LOGDIR/$board.cli.log"
    graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli_daemon/results.json" "$LOGDIR/$board.cli.results.json"
    cli_names="status_before start status_running nodes_cached nodes_direct service_type service_find_visible service_find_hidden service_find_count service_info_cached service_info_direct node_info_alpha_cached node_info_alpha_direct node_info_beta_cached node_info_beta_direct node_info_duplicate_cached node_info_duplicate_direct node_info_alpha_hidden_cached node_info_alpha_hidden_direct stop status_after nodes_after_stop"
    if [[ "$cli_batch" == action ]]; then
      cli_names="status_before start status_running nodes_cached nodes_direct action_list action_list_count action_type action_info action_info_count action_goal stop status_after nodes_after_stop"
      graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/action_goal.json" "$LOGDIR/$board.action_goal.json"
    elif [[ "$cli_batch" == introspection ]]; then
      cli_names="status_before start status_running nodes_cached nodes_direct service_echo stop status_after nodes_after_stop"
      graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/introspection.result.json" "$LOGDIR/$board.introspection.result.json"
      graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/introspection.server.json" "$LOGDIR/$board.introspection.server.json"
    elif [[ "$cli_batch" == parameter_read ]]; then
      cli_names="status_before start status_running nodes_cached nodes_direct param_list param_get_flag param_get_count param_get_ratio param_get_text param_get_octets param_get_flags param_get_counts param_get_ratios param_get_texts param_describe_count param_describe_locked param_dump stop status_after nodes_after_stop"
      graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/parameter_state.json" "$LOGDIR/$board.parameter_state.json"
      graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli_daemon/parameters_dump.yaml" "$LOGDIR/$board.parameters_dump.yaml"
    elif [[ "$cli_batch" == parameter_write ]]; then
      cli_names="status_before start status_running nodes_cached nodes_direct param_set param_set_readback param_restore param_restore_readback param_load param_loaded_count param_loaded_flags param_loaded_ratio param_loaded_texts param_delete param_deleted_get param_deleted_list stop status_after nodes_after_stop"
      for name in parameter_state.json parameter_events.json parameter_final.json; do
        graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$name" "$LOGDIR/$board.$name"
      done
      graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli_daemon/parameter_load.yaml" "$LOGDIR/$board.parameter_load.yaml"
    elif [[ "$cli_batch" == lifecycle ]]; then
      cli_names="status_before start status_running nodes_cached nodes_direct lifecycle_nodes lifecycle_count lifecycle_initial lifecycle_list_initial lifecycle_configure lifecycle_after_configure lifecycle_list_configure lifecycle_activate lifecycle_after_activate lifecycle_list_activate lifecycle_deactivate lifecycle_after_deactivate lifecycle_cleanup lifecycle_after_cleanup lifecycle_shutdown lifecycle_after_shutdown lifecycle_list_shutdown stop status_after nodes_after_stop"
      for name in lifecycle_callbacks.json lifecycle_events.json lifecycle_final.json; do
        graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$name" "$LOGDIR/$board.$name"
      done
    fi
    if [[ "$cli_batch" == components ]]; then
      cli_names="status_before start status_running nodes_cached nodes_direct component_types component_load_primary component_load_survivor component_list_loaded component_containers component_unload_primary component_list_survivor component_unload_survivor component_list_empty stop status_after nodes_after_stop"
      for name in components_loaded.json components_retired.json components_empty.json container.log container.status.json; do
        graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$name" "$LOGDIR/$board.$name"
      done
    fi
    if [[ "$cli_batch" == statistics ]]; then
      cli_names="status_before start status_running nodes_cached nodes_direct topic_hz topic_bw topic_delay stop status_after nodes_after_stop"
      for verb in hz bw delay; do
        for name in "stats_${verb}_sent.json" "stats_${verb}_received.json" "stats_${verb}.go"; do
          graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$name" "$LOGDIR/$board.$name"
        done
      done
    fi
    if [[ "$cli_batch" == standalone ]]; then
      cli_names="status_before start status_running nodes_cached nodes_direct component_standalone stop status_after nodes_after_stop"
      for name in standalone_received.json standalone_gone.json standalone.ready standalone.start standalone.stop; do
        graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$name" "$LOGDIR/$board.$name"
      done
    fi
    if [[ "$cli_batch" == bags || "$cli_batch" == bag_transform || "$cli_batch" == bag_burst ]]; then
      cli_names="status_before start status_running nodes_cached nodes_direct bag_record_sqlite3 bag_info_sqlite3 bag_play_sqlite3 bag_record_mcap bag_info_mcap bag_play_mcap stop status_after nodes_after_stop"
      if [[ "$cli_batch" == bag_burst ]]; then
        for storage in sqlite3 mcap; do
          cli_names="$cli_names bag_burst_$storage"
          for name in "bag_${storage}_burst.json" "bag_${storage}.burst_stop" "bag_burst_${storage}.yaml"; do
            graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$name" "$LOGDIR/$board.$name"
          done
        done
      fi
      if [[ "$cli_batch" == bag_transform ]]; then
        for label in bag_convert_sqlite3_to_mcap bag_reindex_sqlite3 bag_convert_mcap_to_sqlite3 bag_reindex_mcap; do
          cli_names="$cli_names $label"
          graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$label.inspection.json" "$LOGDIR/$board.$label.inspection.json"
          if [[ "$label" == bag_convert_* ]]; then
            graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$label.yaml" "$LOGDIR/$board.$label.yaml"
          fi
        done
      fi
      graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/bag_files.json" "$LOGDIR/$board.bag_files.json"
      for storage in sqlite3 mcap; do
        for stage in inspection sent received played; do
          name="bag_${storage}_${stage}.json"
          graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$name" "$LOGDIR/$board.$name"
        done
      done
      while IFS= read -r path; do
        path="${path%$'\r'}"; name="${path//\//_}"
        graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/$path" "$LOGDIR/$board.$name"
      done < <("$GRAPH_HOST_PYTHON" scripts/mdds_e2e/bag_file_list.py "$LOGDIR/$board.bag_files.json")
    fi
    for name in $cli_names; do
      graph_fetch_verified "$board" "$MDDS_OWNED_REMOTE_DIR/cli_daemon/$name.log" "$LOGDIR/$board.$name.log"
    done
  fi
done
"$GRAPH_HOST_PYTHON" "$scratch/verify_ros_broker.py" "$LOGDIR" "$MDDS_OWNED_RUN_ID" "$variant" "$BOARD_A" "$BOARD_B"
"$GRAPH_HOST_PYTHON" "$scratch/test_ros_broker_receipt.py" "$LOGDIR" > "$LOGDIR/receipt_validation_tests.log" 2>&1
printf 'ROS_BROKER_RECEIPT_TESTS PASS count=11\n'
if [[ "$cli_batch" == basic ]]; then
  "$GRAPH_HOST_PYTHON" scripts/mdds_e2e/verify_cli_graph_basic.py "$LOGDIR" "$MDDS_OWNED_RUN_ID"
elif [[ "$cli_batch" == daemon || "$cli_batch" == action || "$cli_batch" == introspection || "$cli_batch" == parameter_read || "$cli_batch" == parameter_write || "$cli_batch" == lifecycle || "$cli_batch" == components || "$cli_batch" == standalone || "$cli_batch" == statistics || "$cli_batch" == bags || "$cli_batch" == bag_transform || "$cli_batch" == bag_burst ]]; then
  "$GRAPH_HOST_PYTHON" scripts/mdds_e2e/verify_cli_daemon.py "$LOGDIR" "$MDDS_OWNED_RUN_ID"
fi
