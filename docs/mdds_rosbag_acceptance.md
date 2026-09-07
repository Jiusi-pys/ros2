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
