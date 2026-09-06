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
- `rmw_mdds/src/graph.cpp` assigns every remote endpoint to the first node of
  its participant. Fix endpoint ownership with codec and RMW regression tests
  plus a real cross-board multi-node graph test.
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
  framing/real AF_UNIX socketpair cases from failure to pass. Additional fd
  reuse/protocol-error cases and the final run are in progress. There is still
  no production Unix listener/client or multi-process RMW acceptance.
- Commit `9b7cf95` adds executable oracles for twelve metadata CLI cases.
  Its host tests pass; real-board CLI execution/evidence collection is pending.

Remaining gates include duplicate-node cardinality, large ANNOUNCE delivery
over small physical Bytes MTUs, complete endpoint metadata/lifetime coverage,
multi-process broker integration, and the full 98-case CLI/graph/transport
matrix. Internal service-topic separation and announcement wakeup have been
fixed as individual features; those fixes do not certify the remaining gates.
