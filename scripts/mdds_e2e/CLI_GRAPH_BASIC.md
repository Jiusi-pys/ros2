# Live cross-board CLI batch

Run from the ROS workspace in Git Bash:

```sh
MDDS_RUN_ID=cli_graph_fresh_01 MDDS_ROS_PROFILE_MODE=implicit \
  MDDS_ROS_CLI_BATCH=basic bash scripts/run_mdds_broker_ros.sh
```

The parent first establishes the production two-board ROS/DSoftBus fixture.
While both sides hold their complete initial graph, each board executes the
actual ROS CLI entry point for:

- `topic type`: exact String type for the opposite board's alpha topic;
- `topic find`: all four fixture String topics and no extra String topic;
- `service call`: run-nonce-derived operands sent to the opposite board, with
  the exact AddTwoInts response checked.

Type/find use the supported `--no-daemon --spin-time 3` options. Service call
uses its direct node implementation. No ROS CLI daemon is spawned by this
batch. Each command is a separately waited child with bounded timeout and
recorded actual argv, PID/start identity, stdout, stderr and return code.
The surrounding ROS fixture then completes node/process retirement and native
broker cleanup, with exact libraries, metadata and no owned UDP checks.

The host rechecks functional output independently, validates log hashes and
the production broker selection, and creates one complete dual-board receipt
per case using `cli_acceptance.validate_receipt`. Results are written to
`cli_partial_manifest.json`; unexecuted cases remain NOT_RUN. This batch does
not claim daemon-mode behavior, which belongs to the remaining cases.

`cli_graph_20260907_01` passed all three commands on both boards. Together with
the earlier 12 metadata and 9 offline cases, the revalidated unique count is
24/98. These are separate batches, not a final single-release receipt.

The six functional-oracle unit cases changed from three positive failures to
all passes. They reject wrong types, extra topics and wrong service results:

```sh
.pixi/envs/default/python.exe scripts/mdds_e2e/test_cli_graph_basic.py
```
