# MDDS delivery execution contract

User objective recorded on 2026-09-06 and extended on 2026-09-07.
Status: **ACTIVE / phase 1**. The current counted evidence is maintained in
`../verification_evidence/goal1_20260906/cli_verified_batches.json`; older
progress entries below remain historical records.

## Ordered acceptance gates

1. Complete `rmw_mdds` and MDDS on both KaihongOS RK3588A boards. ROS 2
   traffic must use DSoftBus Socket/Bytes; UDP is not an acceptance transport.
   Require working local multi-process and cross-board discovery/data, every
   installed ROS 2 CLI command's real operation, and complete graph semantics.
   Missing, skipped, substituted, or failing cases keep this gate closed.
2. Only after gate 1 passes, implement and validate the independent
   `mdds_gateway` for MDDS to/from the ROS 2 DDS implementations in the supported
   distribution. DDS-to-DDS translation is outside this objective.
   Multiple devices running the gateway must coordinate main-gateway ownership,
   prevent translation loops and deduplicate relayed samples. At most one
   gateway may have valid main authority in the cooperating deployment.

### Gateway coordination acceptance added on 2026-09-07

These are required, unimplemented/unverified gate-2 criteria. They do not
unlock gateway work before gate 1 passes.

- Concurrent starts, restart and rejoin must converge to one valid main when
  coordination is healthy. A role flag or periodic observation of one main is
  insufficient: forwarding must be gated by current ownership authority.
- Graceful handover, main-process crash, delayed messages, process pauses and
  network partition/recovery must not create two effective mains. A stale main
  must not continue forwarding after losing authority. When exclusive ownership
  cannot be established, safety must not be replaced by unilateral promotion.
- Bidirectional MDDS/DDS traffic must not re-enter translation through a
  gateway's own output endpoints, through another replica, or through stale
  endpoints from a previous main. Exercise cyclic topologies and reconnection.
- Retries, replay and concurrent duplicate arrivals must not produce duplicate
  translated deliveries. Preserve logical sample identity across translations
  and leadership changes; identical payloads from distinct legitimate samples
  must still be delivered. Payload hashing alone is not a sample identity.
- Validate bounded coordination/deduplication state, cleanup of retired epochs
  and endpoints, and sustained traffic under failure/recovery. Define the
  identity and lifetime contracts explicitly before implementation, then prove
  them with deterministic TDD cases and real multi-device evidence.

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
- The complete local broker ROS gate has passed. Remote Server routing and
  daemon composition are committed and component-tested. Native channel
  generation control and the physical wire pump have observed RED tests and
  are being implemented. The production factory has not switched to a physical
  broker. Two independently validated CLI batches now cover 21 distinct cases;
  77 cases lack complete receipts. This is not a merged single-release gate,
  and there has been no push.
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
- `broker_remote_adapter_red_20260906` ->
  `broker_remote_adapter_green_20260906` changed all 15 remote integration
  cases from failure to pass. The 18 local actor and 8 daemon regressions also
  passed. Commit `afcaea2` joins remote LinkSession and the existing Routes
  authority; independent Tier 3 review found no high-confidence defect.
- `token_boundary_red_20260906` reproduced library startup changing the
  caller's token and incorrectly succeeding on the unprivileged leg.
  `token_boundary_green_20260906` passed both real SDK authorization legs:
  unprivileged Socket rejection and explicit launcher success, with unchanged
  entry/exit tokens. Commit `d7f4cd2` contains the process-identity boundary.
  Read-only snapshots of both current board permission files include the
  `com.kaihong.mdds.*` native-app rule. This is startup evidence only.
- `broker_remote_daemon_red_20260906` ->
  `broker_remote_daemon_green_20260906` changed six lifecycle cases from
  failure to pass; `broker_remote_daemon_local_regression_20260906` passed
  the eight existing owned-daemon cases. Commit `8fb7708` requires physical
  driver start before READY and retires the driver before Server/listener
  teardown. Tier 3 review found no high-confidence defect. Tests inject the
  driver; the actual SDK/WirePump driver remains an integration gate.
- `reliable_reorder_red_20260906` failed three owned-child component cases;
  `reliable_reorder_green_20260906` passed three, including offset 63/64/65,
  both budget scopes and both allocation failure points. All child exits were
  observed and reaped, with no timeout/signal. Commit `4103da4` preserves
  reliable ACK holes after receive-window/memory rejection. Independent Tier 2
  review found no high-confidence defect. This isolated UDP loopback fixture
  is component evidence, not an OpenHarmony transport fallback.
- `broker_dsoftbus_channel_red_20260906` recorded six failures against the
  old Engine facade. The native implementation is being tested next; source
  review additionally identified a new-Key bytes-before-up notification race
  that requires a seventh deterministic regression before correction.
- `broker_wire_pump_red_20260906` recorded all ten tests failing against
  the unimplemented pump. The fixture uses real Server/Routes/LinkSession,
  AF_UNIX and fragment codecs, with fake physical Channels. Small-MTU large
  ANNOUNCE, exact duplex payload, bounded admission, directional retirement,
  stale generation/nonce, FIFO tickets and timeout behavior await GREEN.
- `rmw_wait_validation_red_20260906` reproduced twelve failures, including
  nine real owned-child SIGSEGVs, against the old wait implementation.
  `rmw_wait_validation_green_20260906` passed all twelve exact child filters;
  `rmw_wait_actual_green_20260906` passed the actual package's twelve tests.
  Commit `bb71497` validates all collections before consuming readiness and
  rejects guard creation after shutdown. Independent Tier 2 review found no
  actionable defect in that change. The separate 16-case timeout regression
  then exposed three large-duration deadline overflows; repair is in progress.
- `cli_offline_20260906_01` passed package creation and bag-plugin listing,
  but its seven security operations failed after SROS2 supplied an EC curve
  class to cryptography 50.0.1. SROS2 commit `d6d725b` supplies the required
  instance. Host regression changed two failures to four passes; both boards
  and the local install now contain the exact backed-up, verified module fix.
  Each board generated a real P256 key/certificate and verified its signature.
  `cli_offline_20260906_02` then passed all nine real operations and independent
  host certificate/signature/file oracles. Commit `49740b8` contains the CLI
  harness. The original twelve metadata cases and these nine have disjoint,
  currently validated receipts; generating security artifacts does not prove
  MDDS enforces their policies or that cross-board transport was exercised.
- The first WirePump implementation passed nine tests; the remaining flow
  test used an insufficient delivery barrier and observed an older queued
  payload. `broker_wire_pump_delivery_barrier_green_20260906` passed all ten
  with 272 real local SENDs, complete per-destination source checks, directional
  retirement and a fresh exact surviving payload. Production WirePump was not
  changed to satisfy that fixture correction. Independent Tier 3 review then
  found a separate small-capacity negotiation defect; an eleventh test now
  requires successful 512 KiB/1 MiB negotiation and boundary behavior.
- Native channel tests reproduced bytes-before-up, stop cancellation and
  concurrent transition-order failures. Their fixes passed nine cases; a
  tenth reproduced natural retirement incorrectly promising an explicit close
  completion. `broker_channel_ten_green_20260906` passed all ten after correcting
  that status. A separate fresh-process test denied real C++ allocations during
  stop: its RED left channels open after bad_alloc, and
  `broker_channel_stop_noalloc_green_20260906` passed with zero rejected
  allocations, all three SDK fds closed, the blocked send returned and all
  channel/byte counters zero. External cleanup-owner callback reentry remains
  a specifically identified lifecycle boundary requiring its own regression.

- Native channel commit `3225164` now passes the final 15-case lifecycle suite,
  the no-allocation stop case, and all 53 unchanged legacy SDK-fake cases.
  Additional REDs covered pending-only errors, failed channel probes and
  rejected same-number channel replacement. These fixes preserve old-key
  retirement ownership. The independent native facade probe then passed real
  Socket/Bytes delivery on both boards, up to 131072 bytes in each direction.
- WirePump commit `51b501d` passes the final eleven cases, including the
  independently identified 512 KiB/1 MiB negotiation correction. The affected
  actor, local actor and daemon suites passed 15, 18 and 8 cases respectively.
- Native remote daemon commit `ac98546` has nine identical RED/GREEN tests
  (9 failures to 9 passes) and builds with `BUILD_TESTING=OFF`. Actual
  Socket/Listen startup and complete cleanup passed on both boards. The
  unmodified daemon subsequently delivered exact 3800, 65536, 131072 and
  1048576-byte payloads in both directions in `bc_remote_green_20260906_01`.
  Its actual processes mapped the board DSoftBus SDK and owned no UDP socket;
  both client and daemon processes exited zero. The identical client binary
  delivered nothing through local-only daemons in `bc_local_red_20260906_04`.
  This verifies the native broker byte path; ROS Participant integration is
  still pending. MDDS `docs/broker_dsoftbus_daemon.md` records the exact scope.
- RMW timeout commit `63b05f9` passes all sixteen wait cases after three
  overflow regressions. Long-double CDR commit `0f3c106` changed the frozen
  27-case comparison from two big-endian read failures to zero failures;
  the actual package's 26 cases also pass on the RK3588A binary128 ABI.
  Harness commit `f1e1c17` separately fixes legitimate `+` in READY paths.

- Commits `e066dbb` and `3c7b056` add persistent broker lifetime and a foreground,
  per-domain service launcher. Seven lifecycle tests and six real POSIX
  exec/lock tests passed. The actual service rejected duplicate starts while
  preserving live native communication, then drained on both boards.
- `ros_broker_20260906_04` passed the production ROS integration subset:
  two Contexts per board, 30 total exact reliable messages, four service calls,
  duplicate-node counts and endpoint ownership, node destruction with its
  Context retained, and peer-process withdrawal. All ROS/daemon processes
  exited zero; actual library/SDK mappings and no owned UDP were checked.
  The eight negative receipt tests passed. The runner and precise boundaries
  are documented in `scripts/mdds_e2e/ROS_BROKER.md`.
- The same actual run observed endpoint type hashes as `INVALID`. Source
  inspection also confirms that enclave queries currently fill empty strings.
  These metadata gaps must be corrected before the complete graph gate passes.
- MDDS commits `eb84a16` and `4f74710` add bounded discovery USER_DATA and
  atomic local metadata creation. Codec and Participant RED/GREEN coverage
  reached 58 and 100 passing cases respectively. RMW commit `9086418` then
  propagates each Context's enclave and returns aligned node/enclave arrays.
  Its graph unit cases passed 38/38; `ros_enclave_green_20260907` passed custom
  enclaves on four actual Contexts across both boards, with the existing data,
  service and retirement checks and nine receipt-negative tests. This closes
  the tested enclave gap above, not the remaining endpoint type-hash gap.
- RMW commit `76aa9dc` closes the tested endpoint type-hash propagation gap:
  generated C/C++ message hashes and distinct request/response hashes reach the
  first MDDS announcement and local/remote endpoint info. The final 49-case
  suite and ten event regressions passed. `ros_hash_green_20260907` matched
  every active ordinary/request/reply endpoint to the generated type-description
  hashes across both boards; ten negative receipt checks passed. This does not
  certify the remaining full graph/CLI matrix or DDS XTypes negotiation.
- RMW commit `6a2d01a` makes the production OHOS default DSoftBus-only without
  a profile. Seven policy cases changed from six failures to zero; all nine
  native RMW test entries passed, while six host checks were skipped explicitly.
  Loopback event/wait fixtures use an uninstalled test library, never a
  production environment escape. `ros_no_profile_20260907` passed the actual
  two-board subset with profile/selector absent, including eleven receipt tests.

- `cli_graph_20260907_01` executed real `topic type`, `topic find` and
  `service call` commands on both boards against the live opposite-side fixture.
  All three complete dual-board receipts passed the canonical case validator.
  The surrounding data/graph/retirement fixture also passed. The full set of
  existing per-case receipts was revalidated: 24 unique CLI operations now pass.

- `cli_topic_data_20260907_03` adds complete dual-board `topic info --verbose`,
  `topic pub --once` and `topic echo --once` receipts. It checks all endpoint
  fields and QoS, exact peer callbacks, and field/filter output. The surrounding
  ROS/DSoftBus fixture and eleven receipt-negative tests passed. Revalidation
  across all four batches yields 27 unique operations; failed fixture attempts
  are retained and excluded.

- `cli_graph_lists_20260907_01` adds `topic list` and `service list` receipts,
  each comparing visible/hidden modes on both boards. Exact names/types and
  filtering passed, together with 12 oracle tests, six host receipt mutation
  tests and the surrounding ROS/DSoftBus checks. The five batches contain
  29 unique validated operations.

The expanded CLI batches above were superseded by `cli_no_daemon_20260907_01`:
source and live-process inspection found echo could reuse an old ROS CLI
daemon. Its corrected recipe explicitly disables the daemon and checks process
absence plus a free loopback port at every command boundary. The strengthened
verifier rejects the prior receipts; the replacement batch passes all eight
cases, with seven ownership and nine CLI receipt tests. Unique coverage remains
29/98. The earlier payload observations are retained as historical evidence.

`cli_daemon_20260907_01` then passed real daemon start/status/stop and node-list
commands on both boards. Cached and direct queries preserve all eight fixture
node rows, including duplicate multiplicity. Process identity, frozen RMW/MDDS
libraries, absence of UDP, exact loopback listener ownership, daemon exit and
successful direct queries after stopping are required. Eight output-oracle
tests, ten daemon receipt tests and eleven surrounding ROS receipt checks pass.

`cli_service_graph_20260907_01` adds service type/find/info: four visible versus
six hidden-inclusive services, exact type, and one client/server in cached and
direct modes. Both boards pass all seven batch cases, ten service-oracle tests,
thirteen daemon receipt tests and eleven ROS receipt checks.

The action-bearing node-info fixture exposed a real broker scheduling defect:
one flush per client per poll accumulated frames and closed the shared remote
link. MDDS commit `e1bc3fe` uses the existing actor budget for additional fair
visits until an idle round. Two RED regressions turn GREEN; all 32 MDDS board
test entries pass. `cli_node_green_20260907_01` then passes eight node-info views
per board, exact action/hidden-endpoint ownership, no physical rebind, and the
surrounding ROS fixture. Eighteen CLI receipt and eleven ROS receipt tests pass.

`cli_action_20260907_01` adds action list/type/info/send_goal using the required
action_tutorials_interfaces Fibonacci type. Each board sends order 5 to the
opposite server, receives all five feedback prefixes and [0, 1, 1, 2, 3, 5]
with SUCCEEDED status. The host binds that result to the same UUID and nonce
in the peer callback. Ten output-oracle tests, nine action receipt tests and
eleven surrounding ROS receipt tests pass. Cancellation and rejection remain
separate action behavior gates.

`cli_service_echo_20260907_02` adds real service echo on both boards with CONTENTS
introspection enabled at the client and server. All four events have matching
request-writer GID, sequence and nonce-derived payloads, bound to actual client
and opposite-server records. Echo stops only after complete observation and its
real SIGINT-derived return code 2 is preserved. Nine echo receipt tests, ten
event-oracle tests, nine controlled-stop tests, seventeen original acceptance
tests and eleven surrounding ROS receipt tests pass.

`cli_param_read_20260907_02` adds parameter list/get/describe/dump. All nine
scalar/array types, exact names, range/read-only constraints, and run-owned
typed YAML dumps match the peer's actual seed state. Eight output-oracle tests,
six receipt mutation tests and eleven surrounding ROS receipt tests pass.

`cli_param_write_20260907_02` adds set/load/delete. Set is read back and restored;
four YAML changes are each read back; the dynamic parameter disappears and its
subsequent get returns the expected real code 1. Seven remote parameter events,
final peer state, YAML and raw callbacks all match. Eight output-oracle, nine
absence-contract, nine mutation-receipt, seventeen acceptance and eleven ROS
receipt tests pass.

`cli_lifecycle_20260907_01` adds lifecycle nodes/get/list/set. Each board drives
the peer through configure, activate, deactivate, cleanup and shutdown, reads
back every resulting state, and verifies five callbacks plus ten state events.
Nine output-oracle, nine lifecycle receipt and eleven ROS receipt tests pass.

`cli_components_20260907_01` adds component types/load/list/unload using private
native containers and installed composition plugin bytes. Each peer container
loads two Talkers; payloads and endpoint ownership are verified, ID 1 is
unloaded while ID 2 retains its GID and publishes again, then ID 2 is removed.
Native process/library provenance, no UDP and clean exits pass, with nine
component receipt and eleven ROS receipt tests. Standalone is still separate.

`cli_standalone_20260907_02` adds the actual component standalone command.
Both boards receive the peer's Talker payload, verify native child ownership
and private libraries, then stop through SIGINT after a two-receiver barrier.
Real CLI code 0, native child disappearance and complete graph withdrawal
pass, with six ownership, twelve receipt and eleven ROS receipt tests.

There are still only 58 independently validated CLI operations out of 98, without
a complete single-release acceptance receipt. Remaining gates include
broader duplicate-node/cardinality scenarios, large ANNOUNCE delivery
over small physical Bytes MTUs, complete endpoint metadata/lifetime coverage,
the remaining multi-process integration matrix, and the full 98-case CLI/graph/transport
matrix. Internal service-topic separation and announcement wakeup have been
fixed as individual features; those fixes do not certify the remaining gates.
