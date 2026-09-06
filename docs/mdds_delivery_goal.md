# MDDS delivery execution contract

User objective recorded on 2026-09-06. Status: **ACTIVE / phase 1**.

## Ordered acceptance gates

1. Complete `rmw_mdds` and MDDS on both KaihongOS RK3588A boards. ROS 2
   traffic must use DSoftBus Socket/Bytes; UDP is not an acceptance transport.
   Require working local multi-process and cross-board discovery/data, every
   installed ROS 2 CLI command's real operation, and complete graph semantics.
   Missing, skipped, substituted, or failing cases keep this gate closed.
2. Only after gate 1 passes, implement and validate the independent
   `mdds_gateway` for MDDS to/from the ROS 2 DDS implementations in the supported
   distribution. DDS-to-DDS translation is outside this objective.

No finite suite proves the absence of all bugs. Completion requires the full
agreed executable acceptance matrix, no known unresolved defects, and explicit
evidence for the tested versions, platforms, and configurations.

## Feature workflow

- Write a regression/contract test and preserve its observed RED result.
- Implement the smallest complete behavior, run the regression to GREEN, then
  related integration, negative-input, lifetime, and concurrency checks.
- Commit each feature in its owning Git repository. Preserve pre-existing
  unrelated changes; record any dependency on pre-existing unpublished work.
- Before any push, apply the user-specified `agentic-review` skill to the exact
  publishable scope, address findings, rerun affected checks, and record what
  remains unverified. A test pass is not publication approval.

## Current evidence and priorities

- Board A: `3e01ff55454d202020104033bf453b00`.
- Board B: `3e01ff55454d202020104433991c3b00`.
- Both were reachable by HDC on 2026-09-06. Source checkouts contain existing
  uncommitted work in MDDS and RMW/gateway; starting diffs were preserved under
  `../verification_evidence/goal1_20260906/`.
- Remote endpoint ownership, native topic/service separation, announcement
  wakeup, and startup callback ordering have individual fixes with regression
  evidence below. Duplicate-node cardinality and complete graph metadata still
  require additional acceptance work.
- DSoftBus currently reserves one fixed session per board/domain and one
  engine per process. Complete CLI requires multiple participants/processes;
  fix that constraint without introducing UDP.
- Existing `run_mdds_cli.sh` substitutes graph counts for `action send_goal`.
  Replace that exemption with a real command/result test. The current generic
  acceptance suite already exercises real action commands for other RMWs.
- The current board runner has no `__rmw_mdds` name exclusion; contrary wording
  in older reports/AGENTS.md is stale. Verify actual CTest registrations.

Keep design/static checks, cross-compilation, fake transport tests, real native
DSoftBus, ROS 2 E2E, CLI/graph, stress, and release provenance as separate
evidence categories. Only terminate processes owned by the current test run.

## Run ledger (2026-09-06)

- `goal1_baseline_20260906`: existing deployment passed DS-01, DS-02, DS-04,
  DS-06. Exact 30/30 messages in each direction; recorded Socket/Bytes calls and
  no sockets in the MDDS UDP port band. This is the initial deployment, not an
  ownership-fix acceptance result or a complete absence-of-UDP audit.
- `graph_ownership_red2_20260906`: real cross-board DSoftBus regression failed
  on both by-node endpoint leakage and incorrect verbose endpoint ownership.
- `ownership_codec_red_20260906` -> `ownership_codec_green_20260906`: the new
  discovery ownership codec test suite changed from failure to pass on board A.
- `ownership_rmw_red_20260906` -> `ownership_rmw_green_20260906`: ten graph
  contract tests changed from failure to pass on board A. The GREEN package
  used the rebuilt `libmdds.so` in its own test directory; shared board
  deployment libraries were not replaced.
- `ownership_fake_green_20260906`: 24/25 fake Socket/Bytes tests passed. The
  remaining test exposed its 60-second fixture announce period versus an
  immediate assertion. After correcting the fixture, all 25 passed in
  `ownership_fake_green2_20260906`.
- `graph_ownership_overlay_20260906_02`: real cross-board ownership regression
  PASS with both real process exit codes zero. Both rebuilt libraries were
  loaded from a hash-verified per-run overlay; shared board deployment stayed
  unchanged. This covers two nodes' pub/sub/service/client ownership, not the
  complete graph/CLI acceptance matrix. MDDS commit `c0ec5d7` and RMW commit
  `7477f51` contain the feature; pre-existing changes remain separate.
- `board_dsoftbus_contexts.py`: real board A second-context creation failed
  with `dsoftbus callback/session slot busy` under the DSoftBus-only profile.
- CLI inventory was read from both boards. Both complete entry-point metadata
  snapshots match the local manifest (22 commands, 98 total acceptance cases).
  Commit `d331d09` adds the coverage/evidence verifier; its 17 unit tests pass.
  This does not mark any of the 98 runtime acceptance cases as passed.
- `broker_codec_red2_20260906`: raw archived GTest XML records 10 tests,
  8 failures, no errors against the deliberate implementation stub. The host
  board-runner aggregate was invalidated by concurrent generation of the same
  package driver. Run board runners for the same package sequentially, even
  when targeting different boards; they share local generated files.
- `broker_codec_green_20260906` passed the initial 10 codec/reassembly cases;
  `broker_codec_final_20260906` passed all 12 after adding interleaved-transfer
  and failed-admission regressions. Commit `0f64f20` contains this test-only
  library; it is not yet a production DSoftBus broker transport.
- `announce_wake_red_20260906` recorded exactly the seven new wake tests
  failing; `announce_wake_green_20260906` passed all 34 fake-ABI cases,
  including notification-during-send and shutdown. The production-library
  regression `graph_announce_overlay_20260906_01` passed on both real boards.
  Commit `f80df03` preserves pending announcement generations.
- `service_visibility_red_20260906` recorded 14 graph failures and
  `native_namespace_data_red_20260906` recorded three data-isolation failures.
  After the RMW `rt/rq/rr` mapping, `native_namespace_green_20260906` passed all
  83 native GTest cases in six programs (six lint wrappers were skipped by the
  board runner). `graph_native_namespace_20260906_01` also passed on real
  DSoftBus. Commit `6e60bec` contains the naming change; old plain-name peers and
  the existing gateway are not compatible until their mapping is updated.
- `broker_routes_red_20260906` -> `broker_routes_green_20260906` changed all
  twelve routing cases from failure to pass. `broker_routes_final_20260906`
  passed all sixteen cases, including stale sessions and 1024 port incarnations.
  Commit `6506992` provides only the in-memory routing core; control-message
  serialization, handshake and executable broker integration are still pending.
- `broker_ipc_red_20260906` -> `broker_ipc_green_20260906` changed all fifteen
  framing/real AF_UNIX socketpair cases from failure to pass.
  `broker_ipc_final_20260906` passed seventeen cases, including fd reuse and
  protocol errors. Commit `4fd8253` contains the bounded IPC implementation.
- Commit `9b7cf95` adds executable oracles for twelve metadata CLI cases.
  `cli_metadata_20260906_01` executed all twelve on board A with real child
  status, raw output and hash-verified evidence. The partial manifest records
  12 PASS and 86 NOT_RUN; the gateway gate remains closed. Commit `dd24c72`
  contains the board harness and provenance checks.
- `mdds_components_full_20260906` passed fourteen native CTest programs:
  276 GTest cases plus two token probes, no skips. This is component evidence;
  the suite includes UDP component tests and does not establish a DSoftBus-only
  end-to-end deployment.
- `participant_startup_red_20260906` recorded three failures in four cases;
  `participant_startup_green_20260906` passed four. The participant core
  regression passed 90 cases and the fake Socket/Bytes regression passed 34.
  `graph_startup_overlay_20260906_01` passed on both real boards. Commit
  `a68258a` publishes stable transport slots before callbacks can run.
- `broker_local_protocol_red_20260906` recorded six failures in eight cases;
  `broker_local_protocol_green_20260906` passed eight. Commit `139cafc`
  supplies the bounded local handshake and routing-message codec.
- `broker_client_red2_20260906` recorded seven failures in eight cases;
  `broker_client_green_20260906` passed eight. Commit `3444d58` supplies the
  concurrent local broker client, including callback-driven stop and teardown.
  It remains a test-only transport factory with no physical DSoftBus proof.
- `broker_local_server_red_20260906` recorded fifteen failures; both
  `broker_local_server_green_20260906` and
  `broker_local_server_green2_20260906` passed fifteen socketpair cases.
  `broker_server_capacity_red2_20260906` then exposed the default receive
  budget rejecting an eighth client (one failure in eighteen cases).
  The first capacity run aborted during artifact transfer and is not a
  runtime result. `broker_daemon_red_20260906` recorded two failures in six
  fork/exec cases against the deliberate daemon stub. Fixes are in progress.
- `node_multiplicity_red2_20260906` recorded seven graph failures in 35 cases;
  `node_multiplicity_green_20260906` passed 35 after preserving each RMW node
  registration and removing only one equal-name registration on destruction.
  `mdds_node_multiplicity_red_20260906` exposed the announcement-side count
  defect; `mdds_node_multiplicity_green_20260906` then passed all 37 fake
  Socket/Bytes cases. The actual DSoftBus run
  `graph_duplicate_red_20260906_01` observed only one of two same-name nodes;
  `graph_duplicate_green_20260906_01` passed after the fix, with both child
  exits zero and verified private libraries. MDDS commit `b61d31f` and RMW
  commit `bbc1c57` contain this feature; enclave values remain unsupported.
- `broker_server_capacity_green_20260906` passed all eighteen actor cases
  after correcting the bounded default receive budget. The daemon review
  regression `broker_daemon_symlink_red_20260906` exposed a path-based chmod
  following a replacement symlink; its one failing case is being repaired.
- The local broker ROS probe retains real reliable acknowledgment assertions.
  Source review found `rmw_publisher_wait_for_all_acked` still returns
  UNSUPPORTED for RELIABLE publishers despite MDDS retaining per-association
  ACK watermarks. The new API passed the sixteen MDDS regressions and eight
  RMW adapter tests; the final fake suite passed 53 cases in
  `ack_ordered_fake_final2_20260906`. MDDS commit `fec6e62` and RMW commit
  `5cac4fa` implement the wait. Rebuild all affected C++ Writer consumers.
- `ordered_publish_red_20260906` reproduced four ordering/reentry failures;
  the ordered comparison passed four. Review then exposed notification loss
  after a later send exception. `ordered_exception_red_20260906` reproduced
  the exact queued-but-unnotified sample; `ordered_exception_green_20260906`
  passed all five cases after cleanup was repaired. The actual participant
  core passed 91 cases in `ordered_ack_core_green_20260906`. Commit `55de10d`
  contains this prerequisite separately from ACK waiting and old reorder work.
- Broker actor capacity and owned daemon cleanup passed 18 and 8 cases.
  Commit `1d15c55` contains that local-only implementation. The independent
  physical-link control codec passed 8 cases (`81da5a5`); its LinkSession
  state machine passed 17 (`fd1e438`). Directional reassembly retirement passed
  7 new cases and the original 12 codec cases (`1a4b97d`). These are component
  results; the Server remote adapter and physical SDK connection are pending.
- Actual local broker teardown exposed a separate rclpy defect: its default
  type-description service did not call `rcl_service_fini`, leaving two MDDS
  endpoints after node deletion. The resulting owner-less announcement was
  correctly rejected by the immutable-owner ledger and froze the old graph.
  `td_lifetime_red_20260906_01` reproduced both ownership failures;
  `td_lifetime_green_20260906_01` passed both with the Context and observer
  still alive and with retained native implementation copies handled safely.
  The rclpy fix is commit `e4c7d49`; temporary diagnostics were removed.
- `bl_fixed_rclpy_20260906_01` passed the complete local broker gate: two
  Contexts exchanged five exact samples each way and completed ACK waits;
  alpha retirement removed its graph state; beta communicated with a fresh
  gamma Context. Two distinct worker processes passed exact data, explicit
  completion barriers and peer removal. All ROS children and the daemon
  exited 0, package/library mapping and hashes matched, and cleanup passed.
  The native rclpy extension was loaded from a private package. Every result
  explicitly records `physical_dsoftbus_proven=false`; CLI remains 12/98.

Remaining gates include duplicate-node cardinality, large ANNOUNCE delivery
over small physical Bytes MTUs, complete endpoint metadata/lifetime coverage,
multi-process broker integration, and the full 98-case CLI/graph/transport
matrix. Internal service-topic separation and announcement wakeup have been
fixed as individual features; those fixes do not certify the remaining gates.
