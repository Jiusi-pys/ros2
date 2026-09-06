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
  the exact AddTwoInts response checked;
- `topic info --verbose`: exact remote publisher/local subscriber ownership,
  generated type hash, distinct 16-byte GIDs and every exposed QoS field;
- `topic pub --once`: a nonce-derived Int32 received exactly once by the
  opposite board, bound to its actual callback log and receipt;
- `topic echo --once`: the opposite board's nonce-derived Int32 source,
  with reliable/volatile QoS, field selection and a matching filter.

Type/find/info use the supported `--no-daemon --spin-time 3` options. Service
call, publish and echo use their direct node implementations. No ROS CLI daemon is spawned by this
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
.pixi/envs/default/python.exe scripts/mdds_e2e/test_cli_topic_data.py
```

The seven topic-data oracle tests initially had three positive failures, then
all passed after implementing exact output checks. The first expanded physical
run, `cli_topic_data_20260907_01`, stopped at an incorrect 24-byte fixture GID
assumption; this workspace's Jazzy `rmw/types.h` specifies 16 bytes. Run `_02`
passed info and pub on both boards but rejected echo: Jazzy applies `--field`
before `--filter`, so the expression must compare integer `m`, not `m.data`.
These failed batches do not increase accepted CLI coverage.

`cli_topic_data_20260907_03` passed all six commands on both boards, including
the exact opposite-side callback for each once-only publish. The surrounding
ROS fixture and eleven negative receipt tests passed. Revalidating all four
batches yields 27 unique accepted operations out of 98; duplicate cases in
the expanded batch are counted once. Full graph and single-release CLI gates
remain open.
