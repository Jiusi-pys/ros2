# ROS 2 `rmw_mdds_cpp` CLI Test Plan

## Scope And Result Rules

This plan validates a deployed ROS 2 overlay with
`RMW_IMPLEMENTATION=rmw_mdds_cpp`. A passing P0 or P1 lane proves only the
listed behavior. It does not certify all ROS 2 features or production
readiness.

Use explicit board-side `RESULT|...|PASS` markers and retained logs as the
authoritative result. Some host `hdc` versions can exit with status 139 after
the board command has completed; that host status alone is not a functional
failure when board-side markers and artifacts are complete.

Do not pass `--no-daemon` to `ros2 topic pub`, `ros2 service call`,
`ros2 action send_goal`, or `ros2 run`. It is accepted by graph/listing and
echo verbs, but not by those commands on the target ROS 2 CLI.

## Preconditions

1. Deploy the same current overlay to every board under test.
2. Set the target setup path and, when required, the bridge library:

   ```bash
   export ROS2_SETUP=/data/local/tmp/ohos-colcon-rk3588a/setup.bash
   export RMW_MDDS_BRIDGE_LIBRARY=/data/local/tmp/ohos-colcon-rk3588a/lib/libmdds_bridge_shared.z.so
   ```

3. Select an unused domain. Cross-device runs need two distinct board IDs and
   a working DSoftBus/LNN connection.
4. Clear only test processes and the test broker socket before a rerun; do not
   remove unrelated board artifacts.

## P0: CLI Smoke

Run the restored smoke script on one target. It forces `rmw_mdds_cpp`, enables
broker mode by default, and validates topic graph, pub/sub, AddTwoInts service,
and Fibonacci action:

```bash
ROS_DOMAIN_ID=93 \
ROS2_SETUP=/data/local/tmp/ohos-colcon-rk3588a/setup.bash \
RMW_MDDS_BROKER=1 \
/data/local/tmp/ohos-colcon-rk3588a/ohos/tools/rmw_mdds_cli_smoke.sh
```

For a host overlay, use its `install/setup.bash` instead. Required markers:

```text
RESULT|rmw_mdds_cli_topic_list|PASS
RESULT|rmw_mdds_cli_topic|PASS
RESULT|rmw_mdds_cli_service|PASS
RESULT|rmw_mdds_cli_action|PASS
RESULT|rmw_mdds_cli_smoke|PASS
```

If the installed image does not carry the helper script, invoke it from the
workspace through `hdc shell`, or use the equivalent individual commands from
the script. Confirm `RMW=rmw_mdds_cpp` is printed before accepting output.

## P1: Functional Lanes

Run the existing contracts and runners in this order:

```bash
./ohos/test_rmw_mdds_delivery_contracts.sh
./ohos/test_rmw_mdds_artifact_contracts.sh
./ohos/test_rmw_mdds_action_bag_contracts.sh
./ohos/tools/run_cross_board_rmw_mdds_m2m.sh <board-a> <board-b> 93
```

P1 requires independent markers for QoS behavior, service, action, rosbag
action record/replay, and cross-board topic delivery. Preserve the generated
board result files and runner logs.

## P2: Stress And Runtime Evidence

P2 is a hard gate, not a smoke extension. Run the service stress matrix across
the supported concurrency models and retain structured counters:

```bash
for n in 1 2 4 8 16 24 30 31 32 40 50 64; do
  for round in $(seq 1 10); do
    ./ohos/tools/run_rmw_mdds_service_stress.sh --clients "$n" --timeout 75 --round "$round"
  done
done
```

For the 50-way gate each round must report `CLIENT_CREATED=50`,
`CLIENT_SENT=50`, `SERVER_REQ=50`, `OK=50`, `TIMEOUT=0`, and `ERROR=0`.
Also run the independent soak, large-message, graph-churn, security,
cross-device, and performance runners documented by the delivery contracts.
Do not mark P2 passed from a single CLI smoke result.

## P3: Conformance And Sanitizers

Run host conformance before declaring broad parity:

```bash
ctest --test-dir build/test_rmw_implementation --output-on-failure
./ohos/tools/run_rmw_mdds_fullstack_tsan_board.sh <board-a> <board-b>
```

Record full `test_rmw_implementation`, ASAN, TSAN, and performance-baseline
results separately. An unrun or failed P2/P3 lane keeps the overall status
`INCOMPLETE`; it must not be summarized as full-feature or production-ready.
