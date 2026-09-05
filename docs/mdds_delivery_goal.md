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
  remaining first-announcement test exposed its 60-second fixture announce
  period versus an immediate assertion; correction and rerun are required.
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

Further known graph gaps to address after ownership: internal service endpoints
are exposed by topic queries, duplicate-node cardinality is collapsed, large
ANNOUNCE frames exceed small physical Bytes MTUs, and `announce_now()` does not
wake the announcement wait predicate. Each needs its own RED/GREEN feature.
