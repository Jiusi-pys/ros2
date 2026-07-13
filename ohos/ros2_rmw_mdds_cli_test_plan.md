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

## Current Checkpoint (2026-07-13)

The user-approved Plan A production profile is `COMPLETE`. The current scheme-A artifacts pass P0/P1, all three
50-way service models for 30/30 rounds, cross-board QoS/types, action-bag, signed/protected SROS2, exact 16 MiB
topic/service repeat10, graph churn, both-board conformance, current-source ASAN/TSAN, a 36,000-call two-hour
service soak, and a four-client 1,789-call exact-wire 16 MiB two-hour soak.

The accepted 1 KiB gate is p95 <= 50 ms and throughput >= 100 msg/s; rmw_mdds measures 3.089 ms and
1774.141 msg/s. Fast DDS remains faster at 0.399 ms and 2482.394 msg/s, so this completion does not claim
performance equivalence. Target LeakSanitizer is unavailable and is not reported as clean. Six upstream
capability conditions remain visible as skips only after direct-loan 3/3 and allocation/serialized-size 5/5
positive supplements pass on each board.

## Preconditions

1. Deploy the same current overlay to every board under test.
2. On a board, load the deployed runtime environment. It supplies the Python
   runtime, OHOS library paths, overlay, RMW selection, and bridge path:

   ```bash
   . /data/local/tmp/rmw_mdds_env.sh
   ```

   On a host, source the matching `install/setup.bash` instead and set
   `RMW_MDDS_BRIDGE_LIBRARY` when the host test needs the production bridge.

3. Select an unused integer domain in `0..232`. Cross-device runs need two
   distinct board IDs and a working DSoftBus/LNN connection. Runners must
   reject invalid domains instead of retaining them as acceptance evidence.
4. Clear only test processes and the test broker socket before a rerun; do not
   remove unrelated board artifacts.

## P0: CLI Smoke

Run the restored smoke script on one target. It forces `rmw_mdds_cpp`, enables
broker mode by default, and validates topic graph, pub/sub, AddTwoInts service,
and Fibonacci action:

```bash
. /data/local/tmp/rmw_mdds_env.sh
ROS_DOMAIN_ID=93 RMW_MDDS_BROKER=1 \
  /data/local/tmp/ohos-colcon-rk3588a/ohos/tools/rmw_mdds_cli_smoke.sh
```

The board script uses `#!/bin/sh` and must be invoked directly as shown; `sh script`
is no longer a required workaround. `ROS2_SETUP` remains available for a POSIX
setup script only when the surrounding environment already provides the target
Python and OHOS runtime paths. Required markers:

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
bash ./ohos/test_rmw_mdds_delivery_contracts.sh
bash ./ohos/test_rmw_mdds_artifact_contracts.sh
bash ./ohos/test_rmw_mdds_action_bag_contracts.sh
./ohos/tools/run_cross_board_rmw_mdds_m2m.sh <board-a> <board-b> 93
./ohos/tools/run_cross_board_rmw_mdds_matrix.sh <board-a> <board-b> 101
./ohos/tools/run_cross_board_rmw_mdds_action_bag.sh <board-a> <board-b> 103 104
./ohos/tools/run_cross_board_rmw_mdds_sros2_policy.sh <board-a> <board-b> 105
./ohos/tools/run_cross_board_rmw_mdds_sros2_protected.sh <board-a> <board-b> 106
```

P1 requires independent markers for QoS behavior, service, action, rosbag
action record/replay, and cross-board topic delivery. Preserve the generated
board result files and runner logs.

## P2: Stress And Runtime Evidence

P2 is a hard gate, not a smoke extension. Run all supported service concurrency
models with independent domains and retain structured counters:

```bash
domain=110
for mode in processes many_clients_one_process one_client_many_requests; do
  python3 ./ohos/tools/run_rmw_mdds_service_stress_gate.py \
    --mode "$mode" --clients 50 --rounds 10 --domain "$domain" \
    --domain-stride --prestart-broker --round-timeout 75 \
    <server-board> <client-board>
  domain=$((domain + 10))
done
```

For every 50-request round, require `CLIENT_SENT=50`, `SERVER_REQ=50`,
`CLIENT_OK=50`, `CLIENT_TIMEOUT=0`, `CLIENT_ERROR=0`, and
`PROCESS_TIMEOUT=0`. `CLIENT_CREATED` must match the model: 50 for
`processes`, 50 for `many_clients_one_process`, and 1 for
`one_client_many_requests`.
Run the large-message and graph gates separately:

```bash
RMW_MDDS_COVERAGE2_ONLY=large \
RMW_MDDS_COVERAGE2_SKIP_DEFAULT_LARGE=1 \
RMW_MDDS_COVERAGE2_LARGE_EXTRA_CASES=large16m_total:16777164:10 \
RMW_MDDS_COVERAGE2_BIG_SUB_TIMEOUT=600 \
RMW_MDDS_COVERAGE2_HDC_TIMEOUT=720 \
  ./ohos/tools/run_cross_board_rmw_mdds_coverage2.sh <board-a> <board-b> 140

RMW_MDDS_COVERAGE2_ONLY=service_large \
RMW_MDDS_COVERAGE2_SKIP_DEFAULT_SERVICE_LARGE=1 \
RMW_MDDS_COVERAGE2_SERVICE_EXTRA_CASES=service_large16m_wire:16777049:10 \
RMW_MDDS_COVERAGE2_SERVICE_SERVER_TIMEOUT=1200 \
RMW_MDDS_COVERAGE2_SERVICE_CLIENT_TIMEOUT=900 \
RMW_MDDS_COVERAGE2_HDC_TIMEOUT=1300 \
  ./ohos/tools/run_cross_board_rmw_mdds_coverage2.sh <board-a> <board-b> 141

RMW_MDDS_GRAPH_CHURN_MODE=rclpy \
RMW_MDDS_GRAPH_CHURN_PROGRESS_INTERVAL=100 \
  ./ohos/tools/run_rmw_mdds_board_graph_churn.sh <board-a> 142 1000

RMW_MDDS_GRAPH_CHURN_MODE=rclpy_action \
RMW_MDDS_GRAPH_CHURN_PROGRESS_INTERVAL=20 \
  ./ohos/tools/run_rmw_mdds_board_graph_churn.sh <board-a> 143 100
```

The two-hour sequential service soak is an independent exact-artifact gate:

```bash
RMW_MDDS_SERVICE_REQUESTS=36000 \
RMW_MDDS_SERVICE_INTERVAL_SECONDS=0.2 \
RMW_MDDS_SERVICE_PROGRESS_INTERVAL=1000 \
RMW_MDDS_HDC_TIMEOUT_SECONDS=7800 \
RMW_MDDS_SERVICE_COMMAND_TIMEOUT_SECONDS=7700 \
  ./ohos/tools/run_cross_board_rmw_mdds_service.sh \
  <server-board> <client-board> 144
```

The concurrent exact-wire 16 MiB service soak is a second independent gate.
The argument order is client board first, then server board:

```bash
RMW_MDDS_LARGE_SOAK_DURATION_SECONDS=7200 \
RMW_MDDS_LARGE_SOAK_CLIENTS=4 \
RMW_MDDS_LARGE_SOAK_PAYLOAD_BODY_BYTES=16777049 \
RMW_MDDS_LARGE_SOAK_MIN_REQUESTS=1000 \
RMW_MDDS_LARGE_SOAK_REQUEST_TIMEOUT_SECONDS=600 \
RMW_MDDS_LARGE_SOAK_POLL_SECONDS=30 \
  ./ohos/tools/run_cross_board_rmw_mdds_large_service_soak.sh \
  <client-board> <server-board> 145
```

Require four valid DONE acknowledgements, all client and server return codes
0, aggregate sent/ok/valid/server counts equal and at least 1000, each client
elapsed at least 7200 seconds, zero timeout/error/invalid/duplicate/send error,
matching production hashes, zero process/socket residual, and both final
markers:

```text
RESULT|mdds_large_service_soak|PASS|...
cross_board_rmw_mdds_large_service_soak_ok
```

Also run the protected-security and full-stack performance runners. Do not
mark P2 passed from a single CLI smoke result or reuse a soak from another hash.

## P3: Conformance And Sanitizers

Run host and board conformance before declaring broad parity:

```bash
. ./install/setup.bash
ctest --test-dir build/test_rmw_implementation -L gtest --output-on-failure
ctest --test-dir build/test_rmw_implementation -L linter --output-on-failure
./ohos/tools/run_rmw_mdds_test_rmw_board.sh <board-a> <board-b>
```

The `gtest` command is the functional suite and must pass 64/64. Record lint
separately: in the accepted host environment, XML lint passes, cppcheck uses
its upstream slow-version skip policy, and Uncrustify 0.83.0_f reports two
format differences in clean upstream baseline files. Do not convert that
known tool/baseline result into either a 70/70 claim or an RMW functional
failure.

For each board, require 16/16 programs, 129 assertions, exactly six classified
conditional skips, direct-loan `PASS=3 SKIP=0`, capability supplement
`PASS=5 SKIP=0`, final `RESULT|rmw_mdds_test_rmw_board|PASS`, and `BOARD_RC=0`.
The six skips are not a waiver: three are rerun in direct mode, and the
publisher/subscription allocation plus serialized-size conditions are rerun in
the AArch64 capability binary.

`ohos/tools/run_rmw_mdds_fullstack_tsan_board.sh` is a **board-side** runner,
not a host deploy wrapper. Stage the matching instrumented RMW, broker, bridge,
16 test binaries, and complete message-library closure first, then invoke it
with the exact expected bridge hash:

```bash
hdc -t <board-a> shell \
  'sh /data/local/tmp/rmw_mdds_tsan_current/run_fullstack.sh \
    /data/local/tmp/rmw_mdds_tsan_current <expected-bridge-sha256>'
```

The accepted summary must contain 16 functional passes, zero test/broker
sanitizer findings, a live broker, the expected bridge SHA, and `BOARD_RC=0`.
ASAN uses the same acceptance fields with its separately instrumented artifact
set. OHOS LeakSanitizer unavailability must be recorded rather than treated as
leak-clean evidence.

Record full `test_rmw_implementation`, ASAN, TSAN, and performance-baseline
results separately. An unrun or failed P2/P3 lane keeps the overall status
`INCOMPLETE`. Under approved Plan A, completion requires every lane above plus
the explicit performance, LeakSanitizer, skip, security, and local-handoff
dispositions; P0/P1 alone must never be summarized as full-feature or
production-ready.
