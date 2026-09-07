# CLI-created native ROS processes

## ros2 run

```bash
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=process_run \
  bash scripts/run_mdds_broker_ros.sh
```

The fixture stages the actual demo_nodes_cpp talker executable and its
libtalker_library.so implementation into a fresh private prefix. Actual
`ros2 run demo_nodes_cpp talker` resolves that prefix and supplies explicit
node, namespace and topic remappings. Native process evidence must bind the
exact executable, argv, parent PID, process group, start time and hashes of
the executable, implementation library, MDDS and RMW. Owned UDP sockets are
rejected; native transport logs must select the DSoftBus broker.

Both CLI processes complete their initial cached/direct node queries before
a two-board start barrier. Independent peer subscribers then require at
least three consecutive Hello World samples, the expected node/type/hash/GID
metadata and matching native publication logs. Only after both peers have
received these samples does the host authorize stopping the owned process
groups with SIGINT. Both the CLI and native child must exit normally without
emergency cleanup. Each peer must observe the publisher and node disappear
before the CLI daemon's final lifecycle checks.

Run `cli_process_run_20260907_01` passed on both RK3588A boards with the
ordinary harness exit zero. It also retained the base thirty cross-board ROS
messages, four service calls and graph retirement/peer-withdrawal checks.
The partial CLI manifest SHA-256 is
`3f4fd52983fdc871991a8ad38929f890cf6c2bc5f4f17a5f58e2b7067c6dede1`.

Six process-identity unit tests were introduced RED before implementation and
passed GREEN. Eleven actual-receipt adversaries passed, covering foreign
parents/groups/binaries, UDP, emergency stop, a surviving child, stale stop
barriers, missing peer messages, incorrect endpoint types and ghost nodes.
The eleven base ROS receipt tests also passed.

```bash
python scripts/mdds_e2e/check_cli_process_receipt.py \
  ohos_test_logs/ros_broker/cli_process_run_20260907_01
```

This adds the `cli:run` case, bringing the aggregate CLI/graph/transport ledger
to 68/98. Launch/test, remaining commands, the full graph matrix, historical
stress observations and final single-release acceptance remain unfinished.
No gateway acceptance or push is implied.
