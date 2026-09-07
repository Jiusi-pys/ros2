# Late joining multi-node graph snapshots

Design: establish two fully populated nodes in one source context on each
board. Each has independent pub/sub, service/client and action server/client
entities. Source readiness requires complete own and peer graph views. Only
after both source-ready records have been fetched and hash-verified may a new
observer process initialize a new ROS context.

The expected by-node sets are independently specified in
`late_graph_contract.py`. Verbose inspection covers all 25 middleware
endpoints per node, including action plumbing, parameter events and type
description service endpoints. Each record must preserve node/namespace,
type, generated type hash, direction, GID and QoS. Programmed endpoint QoS
depths differ between the two nodes. All GIDs must be unique while sharing one
participant prefix. The late snapshot must equal the established source
snapshot in full; a correct node-name list alone cannot pass.

The tests in `test_late_graph_contract.py` were supplied before implementation:
the original four checks were RED. Nine contract tests now pass, including
missing/inherited entities, wrong owners, duplicated GIDs, separate participant
prefixes and incorrect hashes. Independent observer start/initialization times
must follow local source readiness. Cross-board ordering uses source-ready
proofs and ready/go barriers, not a comparison of unsynchronized board clocks.
Both observers must terminate normally and sources must clean up before the
CLI's final graph checks.

Run from Git Bash with a fresh ID:

```bash
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=graph_late \
  bash scripts/run_mdds_broker_ros.sh
```

Verified actual HDC run `graph_late_20260907_03` on both RK3588A boards:

- Both new observers returned 0 and obtained exact peer snapshots, 25 endpoints
  for each of two source nodes. Discovery took approximately 311 ms and 317 ms
  in this run, within the bounded deadline.
- Source/observer snapshots, readiness handoff and final source files matched.
  Participant sharing, node ownership, all 14 endpoint message type hashes and
  QoS metadata checks passed through the real DSoftBus broker path.
- The enclosing native data/service/graph baseline passed, with 11 baseline
  receipt adversaries. Thirteen new evidence adversaries and 18 generic CLI
  gate tests passed.
- Final manifest SHA-256:
  `26cae8ef0ba33fd5f325aba71b42043fbd8cf03d3b5db37c5f25038ddbed8a33`.
- Late-join receipt:
  `7e48a58074588b4f164553c05bc846b0220aac45a3a7a9282f453bcb255cd750`.
- Multi-node ownership receipt:
  `0e0c1f463cec17a767b2a96533bcfe7e45e220f5f4469eabfbb4e7e36ac19b48`.
- Generated type-hash manifest:
  `9084db7ff0c6008b229c4a241600db14ce7d99e04b8eee7b685abebaa00e3238`.

Earlier attempts remain distinct:

- `_01` ran the native fixtures but collection attempted to overwrite the
  already captured source-ready file. The existing overwrite protection
  rejected it. The fix uses separate initial/final artifact names and requires
  byte equality; the protection was not weakened.
- `_02` stopped during B-board input staging; `cli_action.py` had not arrived
  and no ROS test process had started. The exact run-owned empty activity lock
  was recovered only after checking ownership, absence of PID records and
  absence of matching live processes. It is not a functional test result.

Coverage is 87/98. Hidden entities, endpoint QoS matching, duplicates across
participants, churn/failure/recovery/isolation, transport negative cases and
full advanced-functionality/unified-release validation remain outstanding.
Gateway is outside the current goal. No complete graph or release is claimed.
