# MDDS rosbag recording and cross-board replay

Run the physical fixture from Git Bash with a fresh run ID:

```bash
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=bags \
  bash scripts/run_mdds_broker_ros.sh
```

Both RK3588A boards use private, matching MDDS/RMW libraries and the existing
native DSoftBus Socket/Bytes broker. Middleware UDP fallback is prohibited and
checked through process mappings, owned sockets and the physical broker log.
The fixture also stages the complete verified rclpy overlay and a verified
rosbag2_py overlay containing the OHOS storage-interface symbol-scope fix.

For both SQLite and MCAP, each board records ten nonce-bearing String samples
from its peer on two topics. The actual CLI recorder must stop cleanly with
owned PID/start-time SIGTERM. Native SequentialReader inspection and independent
SQLite/MCAP decoding must agree on types, CDR payloads, per-topic counts and
timestamps. Metadata start/duration must match the stored samples. The actual
CLI info output is checked against those values. CLI play filters to one topic,
remaps it, and must deliver exactly five ordered samples back to the source
board. Stored files, command logs and receiver proofs are hashed into receipts.
The MCAP fixture uses `noChunking: true`; its independent parser is scoped to
this fixture, not arbitrary compressed or chunked MCAP files.

The deployed `lib` directory is a symbolic link to `Lib` on both boards.
Recorder provenance checks the kernel's canonical `Lib` storage-plugin path
and its hash, while requiring the run-owned lowercase `lib` path for MDDS/RMW.
Foreign prefixes and fallback binaries remain rejected.

Verified run `cli_bag_paths_20260907_01` (2026-09-07):

- Both formats completed record/info/play on both boards: 40 stored samples,
  20 filtered replay callbacks in total. All actual CLI exit codes were zero.
- Base fixture retained 30 cross-board messages, four service transactions,
  node retirement and peer withdrawal; all owned processes stopped.
- Partial CLI manifest SHA-256:
  `7b389680dc01eb3c7db97e2dcfa73afa472ca0526a4633dc46b95cc230f34754`.
- Thirteen payload, info and native-path unit tests passed. Nine adversarial
  receipt tests passed, including rehashed corrupt CDR, zero storage times,
  fabricated metadata/native reads, missing replay, UDP, foreign binaries and
  emergency stop. The receipt tests exposed an unclosed SQLite connection in
  the host verifier; it now closes explicitly, including on exceptions.

```bash
python scripts/mdds_e2e/check_bag_receipt.py \
  ohos_test_logs/ros_broker/cli_bag_paths_20260907_01
```

Earlier runs remain failed evidence: `cli_bag_timestamps_20260907_01` opened
storage but produced no fixture samples before its deadline; the cause remains
an unresolved stability observation. `cli_bag_matches_20260907_01` produced
the samples but the checker incorrectly expected the symlink path in maps.
Neither is counted as a successful batch.

This adds three unique CLI cases (record, info, play). Burst, convert, reindex,
the remaining CLI/graph matrix, final single-release validation and gateway
acceptance are separate, unfinished gates.

## Conversion and reindex

Mode `MDDS_ROS_CLI_BATCH=bag_transform` retains the recording/replay fixture,
then runs real SQLite-to-MCAP and MCAP-to-SQLite conversion with an explicit
single-topic output configuration. It also copies each original data file into
a fresh directory without metadata and invokes CLI reindex. Independent raw
storage decoding must preserve the selected payloads and timestamps exactly.
Reindex must preserve the entire original data-file hash, regenerate the
correct metadata, and agree with native SequentialReader inspection.

Run `cli_bag_transform_20260907_01` passed these functional checks on both
boards. All application and CLI processes exited normally. HDC disconnected
during the final B-board file collection, so the initial harness exited 1;
this infrastructure failure is retained in `collection_recovery_host.json`.
After reconnection, the five unresolved original PIDs were absent, no further
signals were sent, and both activity locks were released by exact owner.
Collection resumed with hash verification of 62 artifacts. The host ROS and
CLI validators then passed, as did 13 adversarial bag receipt checks and 11
base ROS receipt checks. Partial CLI manifest SHA-256:
`8c85b5aa2a8ee64761143f2475b24de8838ba111faac12b858803c8ce271f3a3`.

The transformation comparison has six unit tests, introduced RED before the
implementation and now GREEN. Its receipt tests reject changed timestamps,
CDR bytes, conversion options and a false initial metadata-absence claim:

```bash
python scripts/mdds_e2e/check_bag_transform_receipt.py \
  ohos_test_logs/ros_broker/cli_bag_transform_20260907_01
```

This adds convert and reindex, bringing the deduplicated acceptance ledger to
63/98. Burst, the remaining CLI/graph cases and final single-release acceptance
remain unfinished. This recovered functional evidence does not turn the
initial interrupted orchestration into a successful harness execution.

## Exact paused-player burst

Mode `MDDS_ROS_CLI_BATCH=bag_burst` records and replays both formats, then
invokes the actual paused CLI player with `--num-messages 3`. An explicit
reliable transient-local QoS override retains the three samples if discovery
finishes after the burst. The peer fixture subscribes with the matching QoS
and rejects a fourth, missing, reordered or wrong-nonce sample. After both
boards have exact receiver proofs, the host writes nonce-bound stop markers;
the command supervisor sends SIGTERM only to its owned player and requires
exit zero without emergency cleanup. The player remains alive until this
two-board barrier; its native library paths, hashes and owned UDP sockets are
inspected before stopping.

Run `cli_bag_burst_20260907_01` passed with the ordinary harness exit zero.
All four burst executions reported exactly three messages and delivered the
matching ordered callbacks. Five burst-proof unit tests and 15 adversarial
receipt tests passed. The new rehashed-log adversary initially exposed that
the checker accepted a player-reported count of four alongside three peer
callbacks; the checker now requires exactly one native count of three and
the explicit DSoftBus selection log. Partial manifest SHA-256:
`eba2ccce310db6b4fe903089fc264a6bc896053ea3ee973c01a94d73551ed6cd`.

```bash
python scripts/mdds_e2e/check_bag_burst_receipt.py \
  ohos_test_logs/ros_broker/cli_bag_burst_20260907_01
```

All seven installed `ros2 bag` verbs now have functional receipts within their
documented test scopes. The aggregate CLI/graph/transport ledger is 64/98;
the remaining 34 cases and final single-release gate are still incomplete.
