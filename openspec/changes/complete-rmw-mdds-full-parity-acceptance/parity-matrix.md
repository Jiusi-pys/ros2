# rmw_mdds Full Parity Acceptance Matrix

Audit date: 2026-07-04; post-checkpoint status sync: 2026-07-06; live spot-check, MDDS bridge refresh, graph-churn, service-large, sanitizer, upstream-conformance, performance, Plan A, rosbag runtime-link, connection-recovery, endpoint-capacity, loan-lifecycle, matched-event, native action-bag, graph-guard, broker subscription shared-loan, allocator-aware generator/typesupport, and current AArch64 conformance rechecks: 2026-07-07/12
Change: `complete-rmw-mdds-full-parity-acceptance`

This matrix is the current acceptance boundary for the broad "perfect/all ROS 2 middleware features" goal. A `proven` row has current code, test, contract, or board evidence. An `incomplete` row blocks production/full-feature completion until it has a RED test/contract, implementation, runtime evidence, or explicit user acceptance as out of scope. This audit does not accept any unsupported row as out of scope yet.

2026-07-12 dynamic shared-arena and broker-lifecycle follow-up, plus current-source rapid-restart refresh:

- The approved C/C++ message-memory-resource API, generated allocator propagation, introspection/Fast RTPS
  typesupport integration, bridge-owned dynamic publisher loans, and broker-owned dynamic subscription arenas are
  implemented. Dedicated board probes cover String, sequence, nested dynamic storage, filters, invalid returns,
  two-slot pressure, cleanup, and bidirectional remote delivery.
- Both boards read back current RMW `4eb87ee4...`, broker `d754adba...`, broker dynamic probe `58d4f756...`, runner
  `db694210...`, and historical non-sanitized production bridge `e2c6a99c...`. Seven local cases per board and three dynamic
  shapes in both remote directions pass with `pool_count=0` and `BOARD_RC=0`.
- Current host evidence is package CTest 24/24, upstream 16/16, full ASAN 24/24 with leak detection, full TSAN
  24/24, and the script/artifact contracts. The new broker-stop regression raises `test_ipc_broker` to 35/35 and
  proves the broker calls the configured bridge shutdown exactly once.
- Current board conformance uses one production-style broker per board suite and passes 16/16 programs, 129
  assertions, six classified conditional skips, and zero failures per board. Without restarting SoftBus after
  that suite, domain 93 passes String in both directions and domain 94 passes topic 40/40, AddTwoInts, and
  Fibonacci action through one shared broker lifecycle.
- The rapid cross-process RED was `SOFTBUS_TRANS_BIND_REQUEST_DENIED`: ten failures in 60 seconds protect the
  fixed local/peer/network bind key for 600 seconds. The state expires automatically; restarting SoftBus was only
  a workaround. A process-scoped outgoing local name now preserves the fixed listener/peer routing while avoiding
  inherited denial state. Each board passes 14/14 restarts and immediate bidirectional delivery without a SoftBus
  restart or wait, closing this blocker.
- The current-source affected refresh rebuilt backend test `6030f35d...` and production bridge `c3b614b2...`.
  Both boards pass 171/171, including the 63-byte base-name compatibility regression, and 14/14 restart cycles
  with the exact bridge mapped and no kill fallback; immediate domains 126/127 pass topic 40/40, service, and
  action in both directions without a SoftBus restart. Broader rows
  in this section remain tied to `e2c6a99c...` unless explicitly repeated.
- A post-test artifact audit detected a separate checkout reusing the same OpenHarmony out directory and briefly
  overwriting one runtime alias. The foreign `ac505e5b...` file was retained separately and the active alias was
  restored to verified `c3b614b2...`; future exact-artifact gates require checkout-isolated out directories.
- Current-source full-stack ASAN and TSAN each pass 16/16 programs per board with zero test/broker finding. TSAN
  clean and deliberate-race controls return 0 and fail-closed 66 respectively. The production artifacts also pass
  the 50-process service hard gate for 10/10 rounds with all create/send/server/response counts at 50 and zero
  timeout/error.
- The same production line now passes P0 smoke 11/11 per board; a 9/9 complex-message/QoS matrix; strict
  transient-local late join and liveliness; signed protected SROS2 with authenticated/encrypted activation,
  authorized 60-message delivery, and unauthorized denial; three consecutive native action-bag runs; rosbag2
  88/88 record/play; exact 16 MiB topic/service repeat3; node/topic/service churn 1000/1000; and action churn
  100/100.
- Full-stack performance completes all 631/631 rmw_mdds samples from 128 B through 4 MiB with no missing ack or
  publish error. At 1 KiB, rmw_mdds is p95 1.874 ms and 345.987 msg/s versus Fast DDS at 0.395 ms and
  2520.369 msg/s. The design gate passes, but the measured product gap remains explicit.
- The current two-hour sequential service soak passes 36,000/36,000 client/server calls at a 0.2-second pace,
  zero timeout/error, and 7355.501/7367.729 seconds client/server elapsed; no trigger process remains afterward.
- Task 5.5 remains open until the complete P2/P3 matrix is audited and because the small-message performance gap
  still requires product acceptance. Missing host lint executables remain tooling gaps, not functional failures.

2026-07-12 broker subscription shared-loan follow-up:

- Fixed-size single-scalar broker subscriptions now use a broker-owned versioned file pool, descriptor-only IPC,
  read-only client mapping, exact return, stale/bounds/ownership checks, disconnect reclaim, and a bounded RELIABLE
  pending queue. Ordinary typed, serialized, and sequence takes auto-return their slots after copy-compatible decode.
- The primary test was RED when broker subscriptions reported `can_loan_messages=false`; the capacity extension was
  RED when the 33rd RELIABLE sample was dropped with 32 loans pinned. Both are GREEN, including remote fake-bridge
  ingress, duplicate/foreign return rejection, more than one pool rotation, and unlink after disconnect.
- Host evidence is package CTest 24/24, upstream `rmw_mdds_cpp` 16/16, affected ASAN 4/4, zero-copy contract PASS,
  and focused changed-path TSAN PASS. Full legacy `test_ipc_broker` TSAN remains red only on an existing fake bridge
  counter race, so this does not claim the historical test stub is fully race-free.
- Both boards run RMW `7f4184b0...`, broker `e9e1f2f0...`, and bridge `58169fc3...`. A-to-B and B-to-A Int32 each
  passed 40/40. Each subscriber showed a `0600` pool, `r--s` mapping, and zero pool files after exit; board-side
  `RESULT|board_broker_subscription_loan|PASS` artifacts retain exact hashes.
- This checkpoint closed fixed-scalar broker subscription loans. The later dynamic shared-arena checkpoint above
  supersedes its complex-shape limitation; Task 5.5 remains open for the independent production gates listed there.

2026-07-11 host test-stub and allocator follow-up:

- The fake bridge registry, counters, QoS, loan queues, and payload state are synchronized; callbacks execute from
  lock-free snapshots, and payload accessors retain thread-local copies. The complete TSAN `test_ipc_broker` now
  passes 34/34, closing the historical fake counter/payload lifetime race.
- Matching nothrow scalar/array allocation overloads close the ASAN `operator new vs free` mismatch in the global
  loan-arena replacement. Five security negative tests no longer retain pointers into temporary RMW error strings.
  Focused TSAN passes 3/3.
- Dynamic deserialization no longer allocates a `SerializedPayload_t` buffer before replacing it with the caller's
  buffer. ASAN `detect_leaks=1` passes pub/sub 37/37 and bridge dynamic-take 10/10 without the prior 29/30-byte
  leak. Normal package CTest is 24/24, upstream `rmw_mdds_cpp` is 16/16, and the delivery umbrella passes.
- Full package ASAN and TSAN are each 23/24. Both stop on the dynamic publisher-loan functional arena-address
  assertion because sanitizer allocators bypass the DSO-global allocation hook; neither reports a leak,
  invalid-access, or race finding for that test.
- Both boards now carry RMW `e2b9cbe1...` with unchanged broker `e9e1f2f0...` and bridge `58169fc3...`. Direct
  production-bridge subscription loans pass 3/3 per board, and broker-mode String pub/sub passes once in each
  direction. Dynamic typesupport `0a2f76d4...` passes the five board `Dynamic*` tests 5/5 per board. The prior
  fixed-scalar broker-pool proof remains attached to RMW `7f4184b0...`.

2026-07-11 full-stack RMW performance follow-up:

- The same two-board rclpy RELIABLE workload first passed all 631 requests with Fast DDS but stopped near 60 small
  requests with rmw_mdds; server counts stopped at the same value. A package RED proved generic RELIABLE topic
  publishers bypassed the service-only MDDS unacked-sample backpressure.
- Generic RELIABLE topic bridge publishers now use an independent default limit of 32 unacknowledged samples,
  capped by nonzero KEEP_LAST depth; service/client endpoints retain their default limit of 1 and BEST_EFFORT
  remains unblocked. Focused semantics pass 4/4, `test_ipc_broker` passes 33/33, package CTest passes 23/23, and the performance contract plus seven
  probe unit tests pass.
- RMW `6bba62f7...`, broker `77a037c6...`, and bridge `58169fc3...` passed three independent valid-domain
  rmw_mdds/Fast DDS comparisons across 128B, 1KiB, 64KiB, 1MiB, and 4MiB. Every server observed 631/631 requests
  with no invalid request, missing acknowledgement, or publish error. rmw_mdds 1KiB p95 was 1.881--2.391 ms and
  throughput was 342.936--357.201 msg/s, inside the 50 ms / 100 msg/s design gate.
- Invalid Fast DDS domains 234/236 are excluded rather than treated as product failures. The runner now rejects
  non-numeric or out-of-range domains before HDC. This closes the valid full-stack RMW performance row, not Task
  5.5 or the remaining security, zero-copy-breadth, skip-disposition, and stability rows.

2026-07-11 native action-bag and graph-guard follow-up:

- The first real cross-board record run proved that remote graph snapshots reached the recorder process, but
  rosbag2 remained blocked in `wait_for_graph_change()` because `UpdateGraphCache()` did not trigger node graph
  guards. `GraphUpdateTriggersNodeGraphGuardCondition` captured this as a deterministic package RED.
- Graph updates now compare encoded endpoint content, avoiding heartbeat-epoch-only wakeups, and trigger all live
  node graph guards only when that content changes. Guard state is atomic and `rmw_wait()` consumes it with an
  atomic exchange. Package CTest passed 23/23 after the fix.
- The host native action record/info/play gate passed 10/10 consecutive runs through `rmw_mdds_cpp`. Both RK3588A
  boards then ran production RMW `3927ec62...`, broker `5dbdd9d2...`, bridge `58169fc3...`, and action probe
  `cbdd236e...`. Three consecutive cross-board runs on domain pairs 209/210, 211/212, and 213/214 each reported one
  action, send-goal request/response 2/2, cancel-goal 1/1, get-result 2/2, replay goals=2, cancel=1, and
  `BOARD_RC=0`.
- The script contract exposed an obsolete explicit `librmw_implementation.so` preload in the new runner. After it
  was removed, a runtime-selectable-only rerun on domains 215/216 produced the same exact counts, identified
  `rmw_mdds_cpp`, and ended both board phases with `BOARD_RC=0`.
- A post-run audit found stale broker socket files with no live process. Cleanup now removes runtime sockets and PID
  files on both boards. The cleanup-fixed domains 217/218 rerun passed with the same action counts, after which both
  boards reported process=0, socket=0, PID file=0, and `BOARD_RC=0`.
- The changed RMW was rebuilt as TSAN artifact `e7656568...`; with broker `55f94657...` and bridge `546c7aba...`,
  each board passed the 16-program gate with zero functional failure, zero current test/broker TSAN report, a live
  broker, and `BOARD_RC=0`.
- Clean temporary worktrees applied the complete `rcl`, `rclcpp`, and `rosbag2` patch artifacts. The action-bag
  contract verifies source surfaces, CLI options, deployment, cross-board runner, graph regression, and workspace
  patch routing.
- The final host delivery umbrella passed after the no-preload correction, including all security/zero-copy/
  artifact/action-bag contracts, package CTest 23/23, the accepted upstream-derived suite 16/16, type-description,
  and all CLI lanes.
- This closes the dedicated native action-bag CLI blocker and the affected graph-guard/TSAN regression. Task 5.5
  remains open on valid full-stack RMW performance, broader security combinations, dynamic/broker subscription
  zero-copy breadth, explicit remaining skip disposition, and remaining long-stability/P2/P3 evidence.

2026-07-11 lifecycle shutdown follow-up:

- Repeated `ros2 lifecycle get` exposed an intermittent real heap abort that the old retry loop could hide. A
  post-start GDB capture showed two `get_type_description` service broker readers from separate rmw contexts still
  updating the graph after the main Python thread entered `_dl_fini`.
- `IpcClient` instances are now tracked by `rmw_context_t`; `rmw_shutdown()` stops only the matching context's
  readers using `shutdown()`, join, and close. A deterministic package RED showed service response publication
  still succeeded after shutdown. The GREEN test also proves that shutting down one context leaves another live
  context operational.
- The host reproducer passed 100/100 Release iterations under strengthened glibc allocator checks. Package CTest
  passed 23/23, the accepted upstream-derived suite passed 16/16, and the full CLI delivery umbrella passed.
- The lifecycle runner fails immediately on `rc>=128` and retains the failed attempt logs, preventing a later retry
  from relabeling `SIGABRT` or `SIGSEGV` as PASS.
- Production artifacts RMW `da23c6d7...`, broker `5dbdd9d2...`, and bridge `58169fc3...` passed a real 20/20
  cross-board lifecycle state-request run through `rmw_mdds_cpp` with `BOARD_RC=0`.
- The changed RMW was rebuilt as TSAN artifact `6b04c69a...`. With broker `55f94657...` and instrumented bridge
  `546c7aba...`, both boards again passed the 16-program full-stack gate with zero functional or TSAN failure, a
  live broker, and `BOARD_RC=0`.
- This closes the lifecycle shutdown/false-PASS blocker. Task 5.5 remains open on the independent production gates
  listed below; this follow-up is not a full-feature or production-ready certification.

2026-07-11 full-stack TSAN follow-up:

- Both RK3588A boards were hash-verified with instrumented RMW `241d11b3...`, broker `55f94657...`, and
  MDDS/DSoftBus bridge `546c7aba...`. The accepted `test_client` and `test_service` binaries were
  `e5c8f68e...` and `c01a86b7...`.
- A test-only compatibility object converts the OHOS clang 15 TSan runtime's broken post-finalization path into
  authoritative process results after TSan decides whether a report exists. The clean negative control returns 0;
  a deliberate race emits `RMW_MDDS_TSAN_REPORT` and returns 66, so the gate fails closed.
- The initial focused run produced 7/16 clean programs and nine race reports. An unhooked diagnostic run located a
  real `IpcClient::Stop()` close-versus-`recv()` race. The implementation now shuts down the socket, joins the
  reader, and only then closes the descriptor.
- Two remaining rc=139 cases were invalid fixture teardown: the upstream-derived QoS tests destroyed a node before
  its client/service child. Disclosed patch `0008-rmw-implementation-destroy-qos-test-entities.patch` adds explicit
  scoped destruction; this gate is not represented as an unmodified upstream test suite.
- With the TSAN-instrumented bridge loaded, the repository runner passed all 16 arm64 OHOS programs on each board.
  Both summaries reported `functional_pass=16`, `functional_fail=0`, `test_tsan_fail=0`,
  `broker_tsan_files=0`, `broker_tsan_text=0`, `broker_alive=1`, the expected bridge hash, and `BOARD_RC=0`.
- A complete default rebuild restored the normal output bridge `58169fc3...`; it has no ASAN/TSAN dependency or
  dynamic symbol and retains `__cfi_check`. The production output is distinct from the accepted TSAN artifact.
- This closes the exact-artifact full-stack TSAN conformance/bridge race gate. At that checkpoint, Task 5.5 remained
  open on valid full-stack RMW performance, broader security/action-bag, then-unclosed broker-loan breadth, explicit
  remaining skip disposition, and other P2/P3 stability evidence. Lower blocked-TSAN statements are historical
  checkpoints superseded by this follow-up.

2026-07-11 full-stack ASAN follow-up:

- Both RK3588A boards were hash-verified with instrumented RMW `78068556...`, static-runtime broker
  `aa73a715...`, MDDS/DSoftBus bridge `2982e8c8...`, and test bundle `f42676f9...`.
- Dynamic-runtime RED runs either conflicted with the broker's static ASAN shadow map or reached an allocator
  interposition bad-free in the board `libhisysevent` static-construction path. The accepted model therefore owns
  one static ASAN runtime in the host executable and leaves the fully instrumented bridge hooks for that process
  runtime to resolve.
- The default-off GN argument `mdds_bridge_asan_use_process_runtime` is valid only with `is_asan=true`. The
  generated bridge has 48 unresolved ASAN hooks, no dynamic ASAN `DT_NEEDED`, and every hook is exported by the
  static-runtime broker.
- With bridge loading and cross-board graph sync active, each board passed the same 16 arm64 OHOS
  `test_rmw_implementation` programs: `functional_pass=16`, `functional_fail=0`, `test_asan_fail=0`,
  `broker_asan_files=0`, `broker_asan_text=0`, `broker_alive=1`, and `BOARD_RC=0`.
- A complete default production rebuild also passed. Its bridge `1ab5cb31...` has no ASAN dependency or symbol
  and retains CFI cross-DSO, `__cfi_check`, and `--no-undefined`. This closes the exact-artifact full-stack ASAN
  invalid-access gate without changing the production link model. OHOS LeakSanitizer is unavailable, so leak
  cleanliness is not certified.
- Task 5.5 remains open on valid full-stack RMW performance, broader security/action-bag, dynamic/broker
  subscription zero-copy breadth, explicit upstream skips, and remaining P2/P3 stability evidence. Lower statements
  that call full-stack ASAN incomplete are dated historical checkpoints superseded by this section.

2026-07-11 independent reliability control-channel follow-up:

- The approved DSoftBus OpenSpec `separate-reliability-control-channel` is implemented at 21/21 tasks and passes
  strict validation. ACKNACK, HEARTBEAT, and GAP frames are decoded into an independent
  `DATA_TYPE_MESSAGE`/`SendMessage` path; DATA remains on `DATA_TYPE_BYTES`/`SendBytes`.
- The retained exact-artifact 900-second gate on bridge `d8386ad3...` completed 265/265 near-cap service requests
  with zero functional error or unmatched/duplicate ACK. Its ACK wait p95/max was 9.380/15.844 seconds, with zero
  waits at or above 30 seconds, satisfying the approved p95 threshold of 11.771 seconds.
- The later current bridge `9b6a5f2d...` was rechecked in both board load paths. Current-worktree binaries
  `MddsDSoftBusBackendTest=70bd5d7f...` and `MddsLaneManagerTest=ad573e46...` passed focused control 7/7,
  pinned-slot lifecycle 4/4, full backend 168/168, and lane-manager 36/36 on each RK3588A, all with
  `BOARD_RC=0`.
- The 900-second latency values remain exact-artifact evidence for `d8386ad3...`; they are not relabeled as a new
  `9b6a5f2d...` performance run. The current recheck proves implementation and regression retention. Together
  these results close the previous shared-TCP-stream ACK-tail design blocker, but do not constitute a full-stack
  RMW performance certification.
- Task 5.5 remains open on valid full-stack RMW performance, broader security/action-bag, dynamic/broker
  subscription zero-copy breadth, explicit upstream skips, and remaining P2/P3 evidence.

2026-07-11 subscription-loan, upstream-conformance, and matched-event follow-up:

- Both RK3588A boards were hash-verified with RMW `e5e6459e...`, broker `5dbdd9d2...`, and bridge
  `9b6a5f2d...`. The broker and bridge hashes remained unchanged because both fixes are inside the RMW library.
- Returning a subscription loan that was never taken from that subscription previously reached
  `MessageAdapter::DestroyMessage()` and freed the foreign pointer. The package-local RED aborted with
  `free(): invalid pointer`, and the pre-fix board run terminated with signal 11. The return path now requires a
  matching active-loan registry entry and returns `RMW_RET_ERROR` without freeing a foreign pointer. Package CTest
  passes 23/23, the focused ASAN regression passes, and the zero-copy contract still rejects copy fallbacks.
- The three upstream subscription-loan cases are capability-conditional: broker mode correctly reports
  `can_loan_messages=false` because its queue cannot provide a bridge-backed zero-copy loan, while direct bridge
  mode runs the cases. After cleaning test-only external brokers, the production DSoftBus bridge passed all three
  direct-loan cases on both boards with `BOARD_RC=0`; the cross-built fake bridge independently passed 3/3.
- Upstream allocator and serialized-size tests intentionally skip when the implementation returns supported rather
  than `RMW_RET_UNSUPPORTED`. Valid-input board tests therefore provide the acceptance evidence: publisher and
  subscription allocation init/fini plus fixed, bounded-sequence, and bounded-string serialized-size cases passed
  4/4 on each board. These skip sites are not missing-feature evidence.
- A fresh full `test_rmw_implementation` run exposed a real matched-event race on one board: callbacks observed two
  completed local registrations, but `rmw_take_event()` reused a five-second-fresh graph snapshot containing only
  one endpoint while the broker's 100 ms graph coalescer was pending. Deterministic publisher and subscription RED
  tests both returned count 1 instead of 2. A successful endpoint-registration ACK now invalidates graph-cache
  freshness so the next query obtains an authoritative snapshot without disabling burst coalescing.
- Host verification passed broker-mode 10/10, upstream event 4/4, package CTest 23/23, and the zero-copy contract.
  Both boards then passed upstream event 4/4 and all 16 `test_rmw_implementation` programs using fresh broker
  sockets. Broker-mode `test_subscription` was 28 PASS / 3 capability SKIP / 0 FAIL; direct mode covered those
  three cases at 3/3 PASS.
- The exact artifact passed a default parameter/type-description-enabled N=50 run at all client/server markers
  50/50 in 6.326 seconds and a two-board formal N=50 run at 50/50 in 15.974 seconds. Both had zero timeout/error or
  process timeout. Test-only brokers and sockets were removed afterward.
- At that checkpoint Task 5.5 remained open. This closed the audited upstream-skip classification, foreign-loan crash, matched-event
  race, and affected N=50 regression only. The independent control-channel follow-up above separately closes the
  prior ACK-tail blocker. Usable TSAN evidence, valid full-stack RMW performance, broader security/action-bag,
  then-unclosed broker-loan breadth, and remaining P2/P3 evidence still prevented
  production/full-feature completion.

2026-07-11 default-node graph-burst and shutdown-race follow-up:

- Both boards were read back with RMW `b2ebceda...`, broker `5dbdd9d2...`, and unchanged bridge
  `9b6a5f2d...`. This supersedes only the graph/registration risk recorded for the preceding capacity artifact.
- Old default-node N=50 reached only 41 created clients, 16 sent/server-visible requests, and 14 responses within
  its 139-second outer cap. N=16 graph debug generated 571 full graph broadcasts, 23,096 bridge match queries,
  and an endpoint-target fan-out product of 3,309,183.
- Host RED used 24 simultaneous IPC connections. All observed the complete final graph, but the synchronous
  implementation emitted 602 graph frames. A single worker now coalesces dirty endpoint/match/sync events for
  100 ms, broadcasts one complete snapshot, and retains the 1.5-second cross-board heartbeat.
- Removing the old heartbeat sleep exposed a real Stop race under ASAN: endpoint cleanup and node/graph-sync
  cleanup concurrently erased the bridge subscriber vector. Stop now joins graph, accept, and client threads
  before unregistering node/graph-sync bridge entities. The same graph RED now emits one 24-endpoint/24-target
  broadcast. The complete 30-test `test_ipc_broker` gtest suite passed 10/10 ordinary repetitions and 20/20
  ASAN repetitions after detecting the pre-fix race; the ordinary package CTest passed 23/23, and script
  contracts pass. The ASAN evidence is scoped to `test_ipc_broker`, not all 23 package CTest targets.
- Current RK3588A default-node N=50 reached 50 create/wait/send/server/response markers in 9 seconds with zero
  timeout/error and all client rc values 0. Client-broker graph work fell to 71 broadcasts and 3,222 match
  queries. Ten repeated default-node rounds each reached client/server 50/50 in 9--10 seconds.
- The isolated minimal-node 50-process hard gate also passed 10/10 independent-domain rounds with zero timeout,
  error, or process timeout and 11.696--14.068 seconds elapsed. Both supplemental 50-request models passed, both
  boards were clean afterward, and no new broker/Python faultlog was created.
- This closes the default-node concurrent graph/registration throughput risk from the capacity checkpoint. Task
  5.5 remains open on explicit upstream skips, usable TSAN evidence, valid full-stack RMW performance,
  broader security/action-bag, and other independent P2/P3 rows.

2026-07-11 endpoint-capacity and broker-registration follow-up:

- Both RK3588A boards were read back with RMW `7c5e5e36...`, broker `0679e219...`, and bridge
  `9b6a5f2d...`. Earlier artifact sets retain their scoped evidence and are not relabeled as current.
- RED board tests exhausted MDDS publishers/subscribers at 64/64 and the remote endpoint DB at 128. A host RED
  test also showed that the broker ACKed a client registration after one required bridge direction failed and
  leaked the successfully created direction.
- MDDS now uses one shared capacity contract: 512 publishers, 512 subscribers, matching reliability writers and
  readers, and 1024 remote endpoints. The broker now returns an explicit error and unwinds partial shared or
  non-shared bridge state unless every required direction was created.
- Host `test_ipc_broker` passed 29/29, including request-publisher and response-subscriber failure rollback;
  package CTest passed 23/23 and script contracts passed. Exact board binaries passed `MddsPubSubTest` 199/199
  and `MddsEndpointDbTest` 24/24 with `BOARD_RC=0`.
- A no-keeper N=16 cross-board run reached 16 create/wait/send/server/response markers. The formal 50-process
  small-service gate then passed 10/10 independent-domain rounds; every round reported
  `CLIENT_CREATED=50 CLIENT_SENT=50 SERVER_REQ=50 CLIENT_OK=50`, zero client/process timeout or error, and
  13.779-14.176 seconds elapsed. `many_clients_one_process` and `one_client_many_requests` both passed 50/50,
  and both boards ended with no relevant process or broker socket.
- A separate default-node 50-process probe, which also enabled parameter and type-description services, reached
  only `CLIENT_CREATED=41 CLIENT_SENT=16 SERVER_REQ=16 CLIENT_OK=14` before its 139-second outer cap. This does
  not replace the isolated small-service gate. At this capacity checkpoint it remained an explicit concurrent
  graph-churn/registration risk; the newer follow-up above records its subsequent closure.
- Task 5.5 remains open. This closes the current small-service capacity/admission blocker only; explicit upstream
  skips, usable TSAN evidence, valid full-stack RMW performance, broader security/action-bag, and remaining
  independent P2/P3 rows still prevent production/full-feature completion.

2026-07-11 latest MDDS connection-recovery and 16MiB sync (supersedes earlier bridge-current labels):

- Both RK3588A boards were hash-verified with RMW `0397797e...`, broker `810a97f5...`, bridge
  `c59c351a...`, protected probe `6aa06d13...`, and overlay SoftBus client `e1771298...`. Both bridge load
  paths have the same hash.
- The DSoftBus connection manager now caps initial `BINDING` and `FAILED` admission at the 4104-frame recovery
  window, drains that full window in one retry batch, rejects concurrent dispatch while a flush is active, and
  prevents any active flush slot from LRU eviction. RK3588A RED/GREEN tests cover each defect; the final backend
  binary passed 158/158 with `BOARD_RC=0`.
- Exact-wire 16MiB service concurrent4x10 passed on three independent domains: each run reached client/server
  40/40 with all process rc values 0 and no timeout/error, aggregate 120/120. A sequential long60 run reached
  sent/server-valid/client-valid 60/60.
- Topic, service-request, and service-response exact-cap/`+1` cases passed 6/6. Exact-cap samples were delivered;
  the three `+1` cases were rejected with explicit `RCLError` at publish, `call_async`, or `send_response`, with
  no unintended peer delivery. Domain 1328 is excluded as launcher-invalid because host HDC processes crashed
  and the cleanup trap stopped the board helpers before a functional verdict.
- The final bridge also passed the affected Task 6.3 board lanes: native M2M 3/3; gateway pub/sub 8/8 plus
  service, action, parameters, and lifecycle; protected SROS2 signed-policy and authenticated/encrypted activation
  on both boards, authorized delivery of 60 messages, and unauthorized publish denial.
- A time-driven two-board 2h concurrent large-message soak then passed 1,791/1,791 validated 16MiB-class near-cap
  service round trips with four concurrent clients, four valid DONE acknowledgements, all client/server return
  codes 0, and zero timeout/error/invalid/duplicate/send-error counts.
- A separate 15-minute graph-debug sample passed 121/121. All 238 broker backpressure waits had unique resumes;
  aggregate wait min/p50/p95/p99/max was 25/1,356/23,542/37,294/46,042 ms. Four 30-second diagnostic timeout
  records subsequently resumed, and no wait reached 60 seconds. The old stuck symptom is closed, but the measured
  p95/max remains a production performance risk.
- A strict lane reliability-control priority experiment was rejected after real-board comparison. The experiment
  passed focused and backend 158/158 tests but completed only 70/100 required 15-minute round trips versus
  121/121 on `c59c351a...`; aggregate p50/mean wait regressed from 1.356/5.763s to 8.551/10.457s. The change was
  reverted, both boards were restored to `c59c351a...`, and the restored backend again passed 158/158. The next
  performance design must address the shared TCP_DIRECT byte stream after the lane, using a RED-gated separate
  control channel or feedback-based pacing rather than strict lane preemption.
- At this connection-recovery checkpoint, Task 5.5 remained open and the measured ACK tail was still unresolved.
  The independent control-channel follow-up above subsequently closed that item. Full-stack sanitizer, a usable
  TSAN clean gate, valid full-stack RMW performance, broader security, dedicated action-bag CLI, explicit upstream
  skips, and remaining P2/P3 production evidence are still not closed.

2026-07-10 latest exact-artifact Plan A and runtime-link sync (supersedes earlier "current artifact" labels):

- Controlled broker auto-start now uses `posix_spawn()` with file actions and `POSIX_SPAWN_SETSID`; the
  multithreaded client no longer calls `fork()`, `setsid()`, or `execl()`. The security contract was observed RED
  on `execl()` before the change and GREEN afterward. Package CTest passed 23/23, the full host delivery umbrella
  passed, and the OHOS artifact imports `posix_spawn*` without the removed process APIs.
- Both RK3588A boards were hash-verified with `librmw_mdds_cpp.so=0397797e...`, broker `810a97f5...`, bridge
  `75053fcc...`, protected probe `6aa06d13...`, `librosbag2_cpp.so=c3888074...`,
  `librosbag2_transport.so=8aadaa92...`, and `rosbag2_py/_transport.so=adf77294...`.
- DSoftBus built through 1975/1975 aggregate actions and 109/109 selected test actions. Four focused RK3588A
  binaries passed 54/54, 198/198, 48/48, and 156/156 tests, for 456/456 with `BOARD_RC=0` in every binary.
- Current-artifact 50-process service stress passed 10/10 fresh-domain rounds. Every round reported
  `CLIENT_CREATED=50 CLIENT_SENT=50 SERVER_REQ=50 CLIENT_OK=50`, with zero client timeout, client error, and
  process timeout. `many_clients_one_process` passed 50/50, and `one_client_many_requests` passed with one client
  entity and 50/50 requests.
- The stale-bridge 4MiB failure was reproduced as `bridge_publish rc=-2` and traced to a bridge built before the
  16MiB MDDS source changes. After deploying bridge `75053fcc...`, exact-wire service passed 4MiB single 1/1,
  4MiB repeat 10/10, 16MiB single 1/1, and 16MiB repeat 10/10 on each of three independent domains (30/30).
- Current-artifact native M2M passed 3/3 (topic 40/40, service sum 42, Fibonacci action `SUCCEEDED`), strict
  transient-local late join passed `retained=1 expected=1`, and signed protected SROS2 passed signed policy plus
  authenticated/encrypted activation on both boards, authorized delivery of 58 messages, and unauthorized denial.
- The no-preload rosbag RED was real: the old OHOS rosbag2 C++ libraries and all eight `rosbag2_py` extensions
  directly linked Fast DDS RMW and `ros2 bag record` terminated with signal 11 before creating a bag. Rebuilding
  them against runtime-selectable `rmw_implementation`, extending artifact/deploy contracts, and deploying the
  Python 3.12 extensions produced a no-preload PASS with 2 recorded files, 88 recorded messages, and 88 replayed
  messages.
- Task 5.5 remains open. The exact current artifacts still lack repeated 16MiB `4 clients x 10 requests`,
  large-message soak, oversized/error propagation, complete upstream allocator/serialized-size/loaned-subscription
  coverage, full-stack ASAN, a usable TSAN clean gate, valid RMW performance comparison, broader security,
  dedicated action-bag CLI, and the remaining P2/P3 evidence. Both boards ended with `NO_RELEVANT_PROCESS` and
  `NO_RMW_SOCKET`.

2026-07-10 final current-artifact implementation and verification sync:

- DSoftBus gateway commit `6551d9cb7` makes node-sync topics configurable and reports matched MDDS subscribers. ROS commits `92ff23a` and `c06ecd5` share service request bridge publishers, preserve node-sync domain identity, render domain-scoped gateway configs, and harden the board runners.
- Both RK3588A boards were hash-verified with `librmw_mdds_cpp.so=c086e335dc90e5c6f29a7823d8f041a33a8472a7fb1900a6b775b143a5b9702e`, `rmw_mdds_broker=810a97f58dd256bb4f6965f8f1850acb8c1e773880b88ff4f6eea238da9da334`, bridge `f853b438639396f1ef1841c267d1e16cb43e6aee9c8af3c46a99fa2905ea5e38`, gateway `eb195e166097e2e7c1022ef832218d8107a6a1d79d317dca038cf5a8d64c270b`, and protected probe `6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`.
- Host evidence passed package CTest 23/23, focused broker/pubsub CTest 2/2, gateway logic CTest 5/5, full-parity loaned/signed-security/network-flow contracts, and independent script/zero-copy/SROS2-policy/artifact contracts. The umbrella security/delivery policy still fails because controlled broker auto-start uses `execl()`; this remains explicit.
- Current-board service stress passed 10/10 process rounds at 50/50 with zero timeout/error/process-timeout. `many_clients_one_process` passed 50/50 and `one_client_many_requests` passed with one client entity and 50/50 requests.
- Current-board functional evidence passed gateway matrix 8/8, gateway service sum 42, gateway Fibonacci action, real `ros2 param` set/get value 4242, lifecycle transition to inactive, strict transient-local late join `retained=1 expected=1`, native M2M 3/3, signed protected SROS2 authorized/denied cases, and cross-board loaned-message delivery with zero allocator/cannot-loan fallback markers.
- Task 4.4 is closed by these explicit markers. Task 5.5 remains open: current-artifact repeated/concurrent/long-soak 16MiB service evidence, oversized/error propagation, the `execl()` policy decision, upstream skips, full-stack sanitizer, valid RMW performance, broader security, action-bag CLI, and remaining P2/P3 stability gates are incomplete.

2026-07-09 latest status sync:

- The no-prestart 50-way small-service stress blocker is now closed on the latest RK3588A deployment. The current board hashes are `librmw_mdds_cpp.so=3c75294eed05c196c632bfc710d4e8deb46c96d9881bf9287d409280b6ae3a15`, `rmw_mdds_broker=140c6c92fe4fb4491e37611537f398c5c0ec8868682d359057e060787fd09559`, protected probe `6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`, bridge `9e442ca143b34aeae07b88f48092a01861e7190d259fd461867c249a476dcad0`, and softbus client `e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`.
- Root causes fixed: broker-mode `rmw_service_server_is_available` no longer blocks on a synchronous targeted graph refresh for every miss, and `IpcBroker::Start` now creates graph-sync bridge endpoints before accepting client connections. The stress harness also now preserves `summary.json`, cleans stress/broker/socket/lock state after a run, and treats `one_client_many_requests` as one expected client entity with 50 expected sends/responses.
- Host verification passed: `test_broker_mode` 9/9, `test_broker_process --gtest_filter='*Broker*:*Graph*:*Service*'` 17/17, `test_service_inproc --gtest_filter='*ServiceAvailability*:*Wait*Service*'` 2/2, `python3 -m py_compile ohos/tools/run_rmw_mdds_service_stress_gate.py`, and `ohos/test_rmw_mdds_script_contracts.sh` with `rmw_mdds_script_contracts_ok`.
- RK3588A verification passed: domain 1200 graph-debug no-prestart smoke reached `CLIENT_CREATED=50`, `CLIENT_SENT=50`, `CLIENT_OK=50`, `SERVER_REQ=50`, and `PROCESS_TIMEOUT=0`; domains 1210..1219 produced `SERVICE_STRESS_SUMMARY|pass=10|fail=0|rounds=10`; domain 1225 `many_clients_one_process` reached 50/50; domain 1227 `one_client_many_requests` reached 50/50 with `CLIENT_CREATED=1`; both boards were clean afterward with `NO_RELEVANT_PROCESS` and `NO_RMW_SOCKET`.
- This supersedes the earlier no-prestart broker-autostart FAIL evidence for the small-service stress gate. It does not close the broad production/full-feature goal: the latest proven 16MiB concurrent service evidence is still failing (`domain 448: SENT=8 RESP=4 TIMEOUT=4 SERVER_REQ=8`, `domain 449 keepalive: SENT=8 RESP=4 SERVER_REQ=4 after 120s`), and the remaining P2/P3 gates still need evidence.

2026-07-10 P3 board and performance sync:

- Direct board hash re-read now supersedes the earlier "current deployment" labels: both RK3588A boards carry
  `librmw_mdds_cpp.so=a9456b23ab3d7f599f5d0db4ee9acc23197f30c06d593c1b12aacf84a504795e`,
  `rmw_mdds_broker=67a7ad42a8f0525d5bc6ad97e43a5d92728cbaad73bb48fa342ae76588f03657`,
  actual loaded bridge `95a86d4fc1363efef7ae04c2e62ec3770921fc25ae4d9c8887df47f752ac3e97`, and protected probe
  `6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`. Earlier artifact sets retain only
  their scoped historical evidence.
- Upstream `test_rmw_implementation` was cross-built and run on RK3588A: 15/16 test programs passed. The only
  failing program, `test_subscription`, had 26 PASS, 3 capability SKIP, and 2 FAIL for typed and serialized
  `ignore_local_publications`. This is a current board functional blocker, not an evidence gap.
- Bridge-enabled ASAN produced 15/16 functional programs, zero test-process ASAN findings, and zero broker ASAN
  logs, but LeakSanitizer is unsupported and the MDDS/DSoftBus bridge was not fully instrumented. This is focused
  partial evidence, not a full-stack sanitizer pass. The TSAN gate is blocked because both static and dynamic OHOS
  runtimes SIGSEGV during `End Tsan report (Finalize)` even for list-only execution with no RMW initialization.
- A current-source native MDDS versus Fast DDS cross-board run used 128B, 1KiB, 16KiB, 64KiB, 200KiB,
  1MiB (`30x2` each), and 4MiB (`10x1`) in BEST_EFFORT and RELIABLE modes. Fast DDS passed 14/14 cases at
  100% delivery and zero timeout. MDDS produced persistent 11/14, fresh-echo 9/9, and same-size sender-churn 10/10
  observations, but the latest focused diagnosis proves that the MDDS persistent results are contaminated by the
  benchmark harness and cannot certify transport delivery or lifecycle cleanup.
- With a fresh persistent echo, seven separate RELIABLE sender processes passed 128B through 200KiB at `60/60`,
  then produced 1MiB `59/60` and 4MiB `5/10` for 5/7 cases. A single sender process running all seven sizes in one
  invocation passed 128B through 64KiB at `10/10`, then produced 200KiB `5/10`, 1MiB `6/10`, and 4MiB `6/10`.
  Every partial case still printed `[PASS]` and exited zero, so neither BEST_EFFORT preconditioning nor process
  teardown/recreation is required to reproduce the harness symptom.
- The confirmed correlation bug is in `mdds_demo`: `WarmupProbe()` sends five additional probes after the first echo;
  `XdevLatencyEchoCallback()` records latency only for a plausible timestamp but increments `g_xdevEchoRecvCount`
  for every echo; `BenchRunXdevPubSubLatency()` resets the counters immediately after warmup and uses that count to
  advance serial measurement. Delayed warmup echoes therefore advance measurement and let large-payload cases stop
  before all measurement echoes are processed. Seven sizes sent exactly `7 x (6 warmup + 10 measurement) = 112`
  messages; the echo writer reached `lastSN=112` and retained `firstSN=107 lastSN=112` at sender exit, consistent
  with premature benchmark completion.
- The second confirmed benchmark bug is false-green result semantics: `PrintResultsTable()` sets success from
  `totalSuccess > 0`, so partial delivery reports `[PASS]` and rc 0. The MDDS persistent performance gate is therefore
  **invalid/incomplete pending harness repair and rerun**, not a confirmed lifecycle/transport failure. Endpoint/lane
  cleanup or rebind remains a separate hypothesis requiring an independent RED contract. The prior follow-up archive
  SHA-256 is `c0b5825242bee94ed38cb42c6466acf734db0b90fcc28746f6a97f20db75c05a`; the new board A/B diagnosis archives are
  `d2f9c46c9bba734a9b4c4e984b67a76ddf00544c91d7fb46bddb2793dc8fa03f` and
  `5d80ec8a26a808c5cae80b22e299ccfe5f6070d3506a6d75a9d5e41f51d05d33`.
- The performance harness is native MDDS versus native Fast DDS, not a ROS 2/RMW full-stack benchmark. The valid
  Fast DDS baseline remains useful, but MDDS latency/delivery certification requires a corrected harness rerun and
  full-stack RMW performance remains open. After the benchmark, both brokers
  were restored, `ros2 topic list --no-daemon` returned board child rc 0 on both boards under
  `RMW_IMPLEMENTATION=rmw_mdds_cpp`, and a cross-board restore smoke delivered 5/5 samples.

2026-07-06 status sync:

- DSoftBus/MDDS HEAD is `30d53a2c2 <test><29168><补充rmw_mdds service压力门禁><source:int;none>`; the current worktree additionally contains DSoftBus `maxSendSize`, defragmenter, and pending-queue startup-burst RED/GREEN fixes for the large-payload investigation. The pending-queue unit gate is green on RK3588A, and the corrected coverage2 topic plus service large-payload gates now pass through 1.5MiB.
- A status-document recheck before this sync confirmed ROS2 HEAD `e50c63d3f` on `jazzy-ubuntu-20.04`, DSoftBus branch `mdds-claude`, and both RK3588A targets visible through `hdc list targets`. `hdc` again returned host exit `139` after valid target output, so this sync continues to judge board evidence from board-side files and markers instead of the host `hdc` process exit alone.
- A live re-read/rerun for this status pass confirmed the lightweight gates and the `ee2a285.../0ce6c5...` RK3588A service-stress repair overlay behavior. `bash enhance/mdds/tests/scripts/test_bridge_protected_transport_contract.sh` returned `bridge_protected_transport_contract_ok`; `bash ohos/test_rmw_mdds_script_contracts.sh` returned `rmw_mdds_script_contracts_ok`; that overlay was rebuilt with `./ohos/colcon_rk3588a.sh rmw_mdds_cpp` and deployed to both RK3588A boards with `rmw_mdds_deploy_ok`. Its deployed hashes were `librmw_mdds_cpp.so=ee2a285ca5484f1d90f4cf35e4272efa998ad0b32a8bed7153401d8a83e126a5`, `rmw_mdds_broker=0ce6c5eb3bfe2e028df3103d3cb46499d6b2e909565fc600af34db4a4dc3427d`, `libmdds_bridge_shared.z.so=8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc`, `rmw_mdds_bridge_protected_transport_probe=6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`, and `libsoftbus_client.z.so=e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`. The later late-IPC deployment supersedes only the rmw/broker hashes for the current boards; the full P0/P1/P2/P3 matrix has not been rerun as one complete suite after the later deploys.
- The retained RK3588A service stress directory `/data/local/tmp/rmw_mdds_service_stress_gate50` remains historical PASS evidence. The later `e46ec303...` overlay removed the old 30/50 request-admission symptom but exposed a cleanup failure: default `processes --clients 50 --timeout 75` reached 50/50 request/response and then failed with `PROCESS_TIMEOUT=39`; cleanup-marker round 61 showed `CLIENT_DESTROY_START=50`, `CLIENT_DESTROY_DONE=13`, and `CLIENT_SHUTDOWN_DONE=13`. That cleanup blocker is now superseded. With the `ee2a285...` service-stress repair overlay, smoke round 82 passed with `SERVER_REQ=50`, `CLIENT_OK=50`, `PROCESS_TIMEOUT=0`, `CLIENT_DESTROY_DONE=50`, and `CLIENT_SHUTDOWN_DONE=50`; hard-gate rounds 83-92 produced 10/10 board-side `summary.json` PASS files with `SERVER_REQ=50`, `CLIENT_OK=50`, and `PROCESS_TIMEOUT=0`. A later live recheck on the recorded `e347fe.../fb70c341...` rmw/broker plus `d314e6cd...` bridge deployment also passed smoke round 301, hard-gate rounds 302-311 10/10, and supplemental split-model rounds 312/313. The later `50e8ee.../3555d7...` availability deployment had not rerun this full service-stress gate at that checkpoint; this is now superseded by the latest `3c75294e.../140c6c92...` deployment, which passed domains 1210..1219 10/10 plus split-model domains 1225/1227.
- The original CLI 50-way service directory under `/data/local/tmp/rmw_mdds_cli_service50` remains present; spot re-read of `call_1.log` and `call_50.log` showed valid AddTwoInts responses. The structured `service_stress_gate50` summaries remain the authoritative 50-way stress evidence because they carry per-round created/sent/server/ok/error counters.
- A fresh sequential service-soak probe with `ohos/tools/run_cross_board_rmw_mdds_service.sh` first exposed a real P2 blocker: 10 sequential Trigger requests passed on RK3588A domain 281 (`client_sent=10`, `client_ok=10`, `server_req=10`, zero timeout/error), but 20 sequential requests failed on domain 282 with `TRIGGER_CLIENT_TIMEOUT index=12`, `client_sent=12`, `client_ok=11`, `timeout=1`, and the server log reaching only `trigger_count=11` without `TRIGGER_SERVER_DONE`. After adding `RmwMddsIpcBroker.ServiceBridgeTransportUsesExpandedInternalHistory` and expanding broker service/client bridge transport QoS beyond public service depth 10, full `test_ipc_broker` passed 14/14 and RK3588A reruns passed domain 283 with 20/20, domain 284 with 1000/1000, and domain 285 with 10000/10000 sequential requests. This closes the request-12 blocker for the current broker-mode sequential gate; it does not close 2h stability or the remaining P2/P3 evidence matrix.
- Latest recorded host delivery evidence from 2026-07-06 is current for the host build tree: after adding an `ipc_protocol` guard so impossible endpoint-list counts fail before `vector::reserve()` and refreshing broker graph snapshots before broker-mode matched-event current-count calculations, the focused upstream event test passed `1/1`, and `bash ohos/test_rmw_mdds_delivery_contracts.sh` ended with `rmw_mdds_delivery_contracts_ok`, including package CTest `23/23`, `test_rmw_implementation` rmw_mdds subset `16/16`, type-description, and host CLI pub/sub, service, action, params, lifecycle, graph, QoS, transient-local, and message-info PASS markers. This latest refresh was then rebuilt/deployed to RK3588A and covered by the focused board reruns listed here; it was not followed by a full P0/P1/P2/P3 rerun.
- Latest recorded host upstream-conformance evidence from 2026-07-06 remains: `source install/setup.bash && ctest --test-dir build/test_rmw_implementation -R '__rmw_mdds_cpp$' --output-on-failure` passes `16/16`; full `source install/setup.bash && ctest --test-dir build/test_rmw_implementation --output-on-failure` passes `69/70`, with the only failure being the missing host `cppcheck` executable.
- A board-side graph churn runner exists at `ohos/tools/run_rmw_mdds_board_graph_churn.sh`. Earlier 2026-07-06 runs proved default CLI/cross-process 100-round graph churn, then exposed and fixed rclpy type-description cleanup and inactive broker connection reaping issues; historical domain 267 rclpy fast-mode passed 1000/1000. Latest recorded graph reruns passed domain 286 rclpy node/topic/service graph churn 100/100 with all created/destroyed counters at `100`, and domain 287 rclpy_action graph churn 100/100 with action created/destroyed counters at `100`. Long-soak graph gates are still not proven.
- Current graph artifact caveat from the live re-read: the retained `summary.txt` files now directly show the default CLI node/topic/service 100/100 PASS and the rclpy_action 100/100 PASS. The rclpy fast-mode 1000/1000 result remains valid as recorded evidence from the prior run, but it was not freshly re-read from the current retained summary because that summary has since been overwritten by later graph-churn runs.
- `ohos/tools/run_cross_board_rmw_mdds_coverage2.sh` was corrected so large topic payloads are generated and validated inside rclpy instead of through overlong `ros2 topic pub` YAML arguments or truncated `ros2 topic echo` text. It now supports `RMW_MDDS_COVERAGE2_ONLY=large|service_large|transient|liveliness|bag`, configurable large-lane warmup/match timeout, publisher/subscriber completion markers, service request/response payload validation through `rcl_interfaces/srv/SetParameters`, and optional extra large-payload cases through `RMW_MDDS_COVERAGE2_LARGE_EXTRA_CASES` / `RMW_MDDS_COVERAGE2_SERVICE_EXTRA_CASES`. Current isolated RK3588A evidence: liveliness domain 141 PASS (`received=16`), bag domain 138 PASS (`recorded_messages=56`, `played_received=51`), and strict transient-local late-joiner replay passed 5/5 on domains 150-154 (`late_joiner_got_retained=12`).
- Large payload history: domains 160/161/166/173/184/193/194 previously showed 512KiB/1MiB PASS and 1.5MiB FAIL while investigating DSoftBus `maxSendSize`, defragmenter timestamp refresh, and pending-queue startup-burst capacity. Those DSoftBus unit blockers have RED/GREEN evidence and RK3588A PASS markers; the pending-queue focused group passed `6/6` with `BOARD_EXIT=0`, and bridge sha `f1d24c5431b4e01472603b298e1dbcff09302da3a3d42a876dcd0b8a8e257127` is deployed on both boards.
- Pre-MDDS-16MiB-refresh large payload evidence: coverage2 large/topic and service lanes passed 512KiB, 1MiB, 1.5MiB, exact-total 4MiB topic, and corrected 4MiB service-wire cases on RK3588A. The early 2026-07-07 16MiB probes remained failing at that checkpoint: `large16m:16777216:1` and `large16m_total:16777164:1` hit publisher encode-frame failure, while `large16m_ipc_budget:16777143:1` let the publisher finish but left the subscriber at `received=0 valid=0`. Those logs are retained as historical root-cause evidence for broker IPC envelope and downstream MDDS/DSoftBus delivery gaps, not as the current exact-total 16MiB topic status.
- MDDS 16MiB bridge/fragment refresh on 2026-07-07: DSoftBus/MDDS raised `MDDS_MAX_PAYLOAD_SIZE` to 16MiB, kept a 512-byte frame-payload allowance, expanded the defragmenter bitmap to `MDDS_FRAGMENT_MASK_WORDS`, passed the focused OHOS build and RK3588A unit gates, rebuilt/deployed bridge sha `037553404f7a9d9a37ad69343f0f8a013ec66230d0da53af1c177a3ad55c18ef` on both boards, and passed the cross-board exact-total 16MiB topic case `large16m_total_after_mddsfix:16777164:1` with `RESULT|cov2_large16m_total_after_mddsfix|PASS|received=1|valid_len=1`. The generated `String.data` is exactly 16MiB. The body=16MiB helper case `large16m_full_user_after_mddsfix:16777216:1` still fails because helper prefix/suffix bytes push the actual topic string above the exact 16MiB boundary. This is a partial large-payload pass, not production/full-feature closure.
- Repeated 16MiB large-message recheck on 2026-07-08 CST: host and both boards had no active coverage2/graph/service-stress/ROS 2 test process after the run; `hdc` still returned host exit 139 after valid board-side output. Domain 328 `large16m_total_repeat3:16777164:3` passed with `RESULT|cov2_large16m_total_repeat3|PASS|received=3|valid_len=3` and board logs `COV_BIGPUB_DONE size=16777164 times=3` plus `COV_BIGSUB_DONE size=16777164 received=3 valid=3`. Domain 329 `service_large16m_wire_repeat3:16777049:3` passed with `RESULT|cov2_service_large16m_wire_repeat3|PASS|sent=3|server_req=3|server_valid=3|client_ok=3|response_valid=3`, client log `COV_BIGSVC_CLIENT_DONE size=16777049 sent=3 ok=3 response_valid=3 service_available=1`, and server log `COV_BIGSVC_SERVER_DONE size=16777049 requests=3 valid=3`. Publisher/client `Signal 15` lines after DONE markers are harness cleanup, not board-side feature failures.
- Higher-count 16MiB large-message follow-up on 2026-07-08 CST: exact-total 16MiB topic repeat10 passed on domain 330 with `RESULT|cov2_large16m_total_repeat10|PASS|received=10|valid_len=10` and board logs `COV_BIGPUB_DONE size=16777164 times=10` plus `COV_BIGSUB_DONE size=16777164 received=10 valid=10`. An intermediate service exact-wire 16MiB repeat10 run on domain 331 failed with `server_req=0`, and follow-up repeat3 reruns on domains 332/335 also failed, while 4MiB repeat3 and 16MiB single-sample sanity passed. After fixing the diagnostic payload to embed `REQIDXnnnnnn_` inside the fixed-length service body and tightening the coverage2 service gate to exact counters, the fixed-index rerun passed: domain 157 topic exact-total 16MiB repeat10 reached `received=10 valid_len=10`, and domain 158 service exact-wire 16MiB repeat10 reached `sent=10 server_req=10 server_valid=10 client_ok=10 response_valid=10`. A later same-day multiround hard-gate rerun separated standalone from production evidence: domain 162 4MiB control, domain 163 16MiB single, and domain 164 standalone 16MiB repeat10 all passed, but consecutive 16MiB repeat10 cases on domains 165/166/167 returned `pass=1 fail=2`; rerun2 and rerun3 both stopped before the server callback with `sent=2 server_req=0 server_valid=0 client_ok=0 response_valid=0`. This reopens the current service 16MiB multiround production gate as a real P2 blocker.
- Service availability-gating follow-up on 2026-07-08 CST: host regression tests for the graph-only availability hypothesis pass after marking remote graph-sync service endpoints as non-local for availability checks: `RmwMddsIpcBroker.MarksRemoteGraphServicesAsNonLocal`, `RmwMddsBrokerMode.RemoteGraphOnlyServiceDoesNotSatisfyAvailability`, full `test_ipc_broker` 15/15, full `test_broker_mode` 8/8 with 1 disabled, and `rmw_mdds_script_contracts_ok`. The AArch64 rebuild was deployed to both RK3588A boards with `librmw_mdds_cpp.so=50e8ee180d959ee588ccb66929eaf3eaffef6be6a72ce1e5c83dec6fc669d3f3`, `rmw_mdds_broker=3555d7aecf61fb14284f3160b6546d6861ab23b4f66eaf287fa5e910d8db725d`, protected probe `6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`, bridge `d314e6cd6d3937f37d9d5ea853225e9c581eafc3222145122860fbfc81c04a99`, and softbus client `e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`. The same 3-case service exact-wire 16MiB repeat10 hard gate still failed once after this fix: `fix1` reported `sent=2 server_req=0 server_valid=0 client_ok=0 response_valid=0` and client `service_available=1 error=ExternalShutdownException`; `fix2` and `fix3` both passed 10/10, so the summary was `COVERAGE2_SUMMARY|pass=2|fail=1`. This keeps the 16MiB service multiround P2 blocker open on the latest deployment.
- Source review after the latest 4MiB rerun confirms the current 4MiB boundary is no longer blocked for the corrected topic/service-wire probes. Current DSoftBus/MDDS source now has `MDDS_RELIABLE_MAX_PAYLOAD_SIZE=MDDS_MAX_PAYLOAD_SIZE`, `DSoftBusSendService()` fragments payloads larger than `MDDS_DEFAULT_FRAGMENT_SIZE` with `MddsFragmenterSend()`, and the frame/defrag path uses `MDDS_MAX_FRAME_PAYLOAD_SIZE` while preserving the final DSoftBus user-payload cap. Those focused unit gates are green on RK3588A, and both the topic 4MiB cross-board lane and corrected service 4MiB wire lane are green. The old `service_large4m_exact:4194250:1` failure should be retained only as an oversized negative case because its request wire is above the 4MiB user-payload cap.
- Additional DSoftBus unit verification: the full RK3588A `MddsDefragmenterTest` suite passed `13/13` with `BOARD_EXIT=0`; `MddsDSoftBusBackendTest.MaxSendSizeForcesFragmentationBelowDSoftBusLimit_001b` passed with `BOARD_EXIT=0`; and the pending-queue startup-burst regression has moved from RED (`actual: 2048 vs 64`, `BOARD_EXIT=1`) to GREEN after increasing `MDDS_CONN_MAX_PENDING` to `MDDS_MAX_FRAGMENTS_PER_MSG * 8`, moving large drain buffers off the sweeper thread stack, and aligning the mock socket send-capture capacity. The RK3588A focused regression group `PendingQueueAcceptsLargeStartupBurst_040kc:PendingQueueFullAndFlushOnBind_040k:PendingQueueFullReturnsError_058:ConnManagerMemoryEstimate*` passed `6/6` with `BOARD_EXIT=0`.
- Current DSoftBus bridge source now routes `MddsBridgeActivateProtectedTransport()` to `MddsConnManagerActivateProtectedTransport()`. The local DSoftBus contract `bash enhance/mdds/tests/scripts/test_bridge_protected_transport_contract.sh` emits `bridge_protected_transport_contract_ok`, the RK3588A focused unit gate `MddsDSoftBusBackendTest --gtest_filter="MddsConnManagerTest.ProtectedTransport*"` passes 7/7 with `BOARD_RC=0` including `ProtectedTransportUsesChannelTypeForEncryptionLookup`, and the full `MddsDSoftBusBackendTest` passes 140/140 with `BOARD_RC=0`. This proves the connection manager rejects unencrypted incoming channels, rejects existing unencrypted channels, fails closed after the encryption-state retry window, tolerates short DSoftBus encryption-info visibility delay, and queries the real DSoftBus channel type for `GetEncryptByChannelId()`.
- Current protected-SROS2 board-harness status: the protected probe is deployed on both RK3588A boards with sha `6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`; the historical protected-harness bridge sha `8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc` passed on the `ee2a285.../0ce6c5...` rmw/broker overlay. The recorded board rmw/broker hashes after the late IPC deployment were `e347fe583811c9e3082476b804bae5a638f553756b1e27ef5d2e9e75225413b6` and `fb70c341e896fcf45a7341b9732edc1bdb4f7ffc6cc1a8051fd1c16c5cc6abab`; after the MDDS 16MiB bridge/recv-queue refresh, current bridge sha is `d314e6cd6d3937f37d9d5ea853225e9c581eafc3222145122860fbfc81c04a99`. The d314 bridge has rerun the approved protected harness on domain 315: signed policy PASS on both boards, protected transport activation PASS on both boards, authorized protected pub/sub `received=60`, unauthorized publish denied, and final `cross_board_rmw_mdds_sros2_protected_ok`. The latest rmw/broker hashes are now `50e8ee.../3555d7...`, but the protected harness has not been rerun on that exact overlay in this checkpoint. The prior domain-221 failure is retained as root-cause evidence: it showed signed policy and activation PASS, but authorized protected pub/sub `received=0`; hilog and source review proved `ClientGetChannelIdAndTypeBySocketId()` returned `businessType` while `GetEncryptByChannelId()` required `channelType`.
- Current graph-churn recheck on the same `e347fe.../fb70c341...` rmw/broker plus `d314e6cd...` bridge deployment passed all three focused 100-round lanes on RK3588A with `RMW_IMPLEMENTATION=rmw_mdds_cpp`: domain 316 `RMW_MDDS_GRAPH_CHURN_MODE=rclpy` node/topic/service reported `GRAPH_CHURN_PASS=100`, `GRAPH_CHURN_FAIL=0`, topics/services created/destroyed all `100`, and `rmw_mdds_board_graph_churn_fast_ok`; domain 317 `RMW_MDDS_GRAPH_CHURN_MODE=rclpy_action` reported `GRAPH_CHURN_PASS=100`, action created/destroyed `100/100`, and `rmw_mdds_board_graph_churn_action_ok`; domain 318 `RMW_MDDS_GRAPH_CHURN_MODE=cli` with CLI timeout 8 reported `GRAPH_CHURN_PASS=100`, topics/services created/destroyed all `100`, and `rmw_mdds_board_graph_churn_ok`.
- The same current deployment then passed a 2h rclpy node/topic/service graph soak on domain 326: `GRAPH_CHURN_TARGET_ROUNDS=50000`, `GRAPH_CHURN_ROUNDS=19770`, `GRAPH_CHURN_PASS=19770`, `GRAPH_CHURN_FAIL=0`, topics/services created and destroyed all `19770`, `GRAPH_CHURN_ELAPSED_SEC=7201.019`, `GRAPH_CHURN_DURATION_MET=1`, `RMW_IMPLEMENTATION=rmw_mdds_cpp`, and `rmw_mdds_board_graph_churn_fast_ok`.
- The current deployment also passed a 2h rclpy action graph soak on domain 327: `GRAPH_CHURN_TARGET_ROUNDS=50000`, `GRAPH_CHURN_ROUNDS=13719`, `GRAPH_CHURN_PASS=13719`, `GRAPH_CHURN_FAIL=0`, actions created/destroyed `13719/13719`, `GRAPH_CHURN_ELAPSED_SEC=7201.497`, `GRAPH_CHURN_DURATION_MET=1`, `RMW_IMPLEMENTATION=rmw_mdds_cpp`, and `rmw_mdds_board_graph_churn_action_ok`. These close the current rclpy node/topic/service and action-specific 2h graph-soak checkpoints, but not concurrent graph churn, sanitizer, or performance gates.
- Current cross-board action introspection CLI recheck on the same deployment passed on domain 320: `ros2 action list -t` showed `/fibonacci [action_tutorials_interfaces/action/Fibonacci]`, `ros2 action type /fibonacci` returned `action_tutorials_interfaces/action/Fibonacci`, `ros2 action info /fibonacci -t` showed `/fibonacci_action_server [action_tutorials_interfaces/action/Fibonacci]`, and `ros2 action send_goal /fibonacci action_tutorials_interfaces/action/Fibonacci "{order: 5}" --feedback` returned goal accepted, four feedback messages, result sequence `0,1,1,2,3,5`, and `SUCCEEDED`. The board marker was `RESULT|rmw_mdds_cross_board_action_cli|PASS|domain=320|ready=1|accepted=1|feedback=4|result=1|succeeded=1|sequence=5|type_rc=0|info_rc=0`. This closes action introspection CLI for the current deployment; dedicated action bag CLI remains limited by the current image lacking a `ros2 bag record --action` surface.
- The scoped OpenSpec/local handoff evidence remains useful, but the broader production/full-feature claim is not complete until the incomplete rows below are closed.

2026-07-07 live spot-check:

- Re-ran lightweight local/document gates: `bash enhance/mdds/tests/scripts/test_bridge_protected_transport_contract.sh` returned `bridge_protected_transport_contract_ok`, `bash ohos/test_rmw_mdds_script_contracts.sh` returned `rmw_mdds_script_contracts_ok`, and `openspec validate --changes --strict` in the ROS 2 workspace reported `4 passed, 0 failed`.
- `hdc list targets` again printed both RK3588A targets and then exited `139` on the host. This remains a host-transport artifact; current status uses board-side files and markers as the result authority.
- Direct board hash re-read on both RK3588A boards matched the service-stress repair overlay at that checkpoint: `librmw_mdds_cpp.so=ee2a285ca5484f1d90f4cf35e4272efa998ad0b32a8bed7153401d8a83e126a5`, `rmw_mdds_broker=0ce6c5eb3bfe2e028df3103d3cb46499d6b2e909565fc600af34db4a4dc3427d`, `libmdds_bridge_shared.z.so=8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc`, and `libsoftbus_client.z.so=e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`. This hash set is historical for the service-stress checkpoint; current bridge after the later MDDS recv-queue refresh is `d314e6cd6d3937f37d9d5ea853225e9c581eafc3222145122860fbfc81c04a99`.
- Direct re-read of service-stress `summary.json` files for that `ee2a285...` overlay shows the default `processes` N=50 gate passed: smoke round 82 passed 50/50 with `PROCESS_TIMEOUT=0`, and hard-gate rounds 83-92 produced 10/10 PASS summary files with `SERVER_REQ=50`, `CLIENT_OK=50`, and `PROCESS_TIMEOUT=0`.
- Direct retained graph summaries show node/topic/service graph churn 100/100 PASS on one board and rclpy_action 100/100 PASS on the other board. The prior domain 286/287 command records remain recorded evidence, but the domain 286 string was not present in the current retained summary files during this spot-check. Direct retained service-soak logs show 10000/10000 client/server completion; the domain 288/289 labels come from prior command records rather than fields encoded in the re-read client/server logs.
- Direct retained large-payload and bag re-read at this spot-check matched the then-current blocker/pass rows: `large16m_p.log` and `large16m_total_p.log` both contained `MDDS broker publish failed: failed to encode frame`, their subscribers reported `size=16777216 received=0 valid=0` and `size=16777164 received=0 valid=0`, and `large16m_ipc_budget_p.log` contained `COV_BIGPUB_DONE size=16777143 times=1 subscriptions=1` while its subscriber reported `size=16777143 received=0 valid=0`. Those large-payload rows are superseded by the MDDS bridge refresh for exact-total 16MiB topic status. service bag info still reports `/add_two_ints` event count 156, service event playback echo count is 156, service request playback server count is 39, and action playback feedback/status counts are 10/4 with zero `Broken pipe` or publish-failure matches in the playback logs.

2026-07-07 late IPC fix recheck:

- Host IPC/broker focused regression after increasing the broker IPC sample-user-payload budget passed 5/5:
  `test_ipc_protocol`, `test_ipc_transport`, `test_ipc_broker`, `test_broker_mode`, and `test_broker_process`.
- The OHOS `rmw_mdds_cpp` overlay was rebuilt and redeployed to both RK3588A boards. Board-side hash re-read
  matched `librmw_mdds_cpp.so=e347fe583811c9e3082476b804bae5a638f553756b1e27ef5d2e9e75225413b6`,
  `rmw_mdds_broker=fb70c341e896fcf45a7341b9732edc1bdb4f7ffc6cc1a8051fd1c16c5cc6abab`,
  `libmdds_bridge_shared.z.so=8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc`,
  `libsoftbus_client.z.so=e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`, and
  protected probe `6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`.
- The exact-total and IPC-budget 16MiB probes were rerun after that deploy. `large16m_total_after_ipcfix`
  and `large16m_ipc_budget_after_ipcfix` both let the publisher reach `COV_BIGPUB_DONE`, but the subscriber
  still reported `COV_BIGSUB_DONE ... received=0 valid=0`. This supersedes the first-layer broker
  encode-frame diagnosis for near-16MiB samples; 16MiB remains an active blocker in downstream MDDS/DSoftBus
  payload/fragment/queue delivery or error propagation.
- The focused OHOS build `./build.sh --product-name khd_rk3588_a --ccache --no-prebuilt-sdk -T
  MddsMessageFrameTest` ended with `khd_rk3588_a build success`.

2026-07-07 current status-doc sync:

- Re-ran the lightweight local/document gates before this edit: `bridge_protected_transport_contract_ok`,
  `rmw_mdds_script_contracts_ok`, and the ROS 2 workspace `openspec validate --changes --strict` with
  `4 passed, 0 failed`.
- `hdc list targets` printed both RK3588A targets and then returned the known host exit 139. Direct board
  hash re-read on both boards matched the late IPC overlay before the later MDDS bridge refresh:
  `librmw_mdds_cpp.so=e347fe583811c9e3082476b804bae5a638f553756b1e27ef5d2e9e75225413b6`,
  `rmw_mdds_broker=fb70c341e896fcf45a7341b9732edc1bdb4f7ffc6cc1a8051fd1c16c5cc6abab`,
  bridge `8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc`,
  softbus client `e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`,
  and protected probe `6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`.
- Retained 16MiB logs after the late IPC fix showed publisher completion but subscriber failure:
  `large16m_total_after_ipcfix` has `COV_BIGPUB_DONE size=16777164` and
  `COV_BIGSUB_DONE size=16777164 received=0 valid=0`; `large16m_ipc_budget_after_ipcfix` has
  `COV_BIGPUB_DONE size=16777143` and `COV_BIGSUB_DONE size=16777143 received=0 valid=0`.
- A later MDDS bridge/fragment refresh supersedes the 4MiB source-constant check for current bridge capacity:
  `MDDS_MAX_PAYLOAD_SIZE` is now 16MiB, frame payload keeps a 512-byte allowance, and the fragment bitmap scales
  with `MDDS_MAX_FRAME_PAYLOAD_SIZE`. Focused OHOS build and RK3588A unit gates passed, both boards report bridge
  sha `037553404f7a9d9a37ad69343f0f8a013ec66230d0da53af1c177a3ad55c18ef`, and the exact-total 16MiB topic probe
  `large16m_total_after_mddsfix:16777164:1` passed with `received=1 valid_len=1`. At that checkpoint service
  16MiB was still open; the recv-queue fix below supersedes it for single-sample exact-wire only. Repeated/concurrent
  large-message, soak, security rerun, sanitizer, error-propagation, and performance gates remain incomplete.
- Service exact-wire threshold was re-read on the same current `e347fe.../fb70c341...` rmw/broker plus
  `037553...` bridge deployment. Board-side serialization checks mapped exact request-wire bodies to 8MiB
  `8388443`, 12MiB `12582745`, 14MiB `14679897`, 15MiB `15728473`, and 16MiB `16777049`. Retained
  `/data/local/tmp/coverage2` logs show 8/12/14MiB all passed with `sent=1`, `server_req=1`, `server_valid=1`,
  `client_ok=1`, and `response_valid=1`; 15/16MiB both failed with `sent=1` but `server_req=0` and
  `client_ok=0`. At that checkpoint this kept service large-message classified as incomplete beyond 14MiB and
  pointed to a request admission/receive/defrag/queue/take-path blocker before the server callback.
- Recv-queue burst-capacity fix on 2026-07-07 supersedes that 15/16MiB `server_req=0` result for the
  single-sample exact-wire cases. The focused RK3588A regression
  `MddsDSoftBusBackendTest.RecvQueueAcceptsFullFragmentBurstWhileDispatchBlocked_033e` failed before the fix at
  `burst index=256 maxFragments=513` with `BOARD_RC=1`, then passed after DSoftBus/MDDS changed the queue
  capacity to `RECV_QUEUE_CAPACITY=(MDDS_MAX_FRAGMENTS_PER_MSG * 4)`. Both boards now report
  `libmdds_bridge_shared.z.so=d314e6cd6d3937f37d9d5ea853225e9c581eafc3222145122860fbfc81c04a99`, and
  `service_large15m_wire_exact_after_recvqfix:15728473:1` plus
  `service_large16m_wire_exact_after_recvqfix:16777049:1` both passed with `sent=1`, `server_req=1`,
  `server_valid=1`, `client_ok=1`, and `response_valid=1`. This closes only the single-sample service
  exact-wire 15/16MiB blocker; higher-count/concurrent/longer-soak large-message, oversized/error-propagation, sanitizer,
  broader security matrix, and performance gates remain incomplete.

| Surface | Current evidence source | Classification | Minimum RED test or executable contract if incomplete |
| --- | --- | --- | --- |
| Init and context lifecycle | `rmw_init.cpp` initializes context, RTPS/broker mode, and security policy; `test_lifecycle` and upstream/package CTest are recorded in `complete-rmw-mdds-feature-closure/tasks.md` and `complete-rmw-mdds-zero-copy-security/tasks.md`. | proven | None. Keep package CTest and upstream `test_rmw_implementation` gates. |
| Nodes | `rmw_node.cpp` creates/destroys nodes and graph guard conditions; `test_lifecycle` covers context/node/guard lifecycle; board doctor reported `middleware name : rmw_mdds_cpp`. | proven | None. Keep lifecycle and board doctor gates. |
| Wait sets | `rmw_wait.cpp` handles subscriptions, guards, services, clients, and events; `test_service_inproc` covers service-ready wait behavior and `test_lifecycle` covers guard readiness consumption. | proven | None. Keep focused wait/lifecycle tests. |
| Guard conditions | `rmw_guard_condition.cpp` implements create/destroy/trigger; `test_lifecycle` covers graph guard and explicit guard triggering through `rmw_wait`. | proven | None. Keep lifecycle guard tests. |
| Graph APIs | Symbol-surface test exports graph APIs; `test_graph` covers topic/node endpoint info, GIDs, QoS, type hashes, and enclaves; host CLI graph probe is included in `ohos/test_rmw_mdds_delivery_contracts.sh`. | proven | None. Keep `test_graph` and host CLI graph probe. |
| Pub/sub typed APIs | `rmw_create_publisher`, `rmw_create_subscription`, `rmw_publish`, `rmw_take`, sequence take, and message-info paths are implemented; package CTest, host CLI pub/sub/message-info, native M2M, and gateway matrix evidence are recorded in prior task files. | proven | None. Keep package CTest, host CLI pub/sub, message-info, native M2M, and gateway matrix lanes. |
| Service APIs | `rmw_create_service`, `rmw_create_client`, `rmw_send_request`, `rmw_take_request`, `rmw_send_response`, `rmw_take_response`, and availability checks are implemented; `test_service_inproc` covers AddTwoInts, Trigger, array service payloads, wait behavior, client cleanup, and pre-ACK delivery. Current artifacts passed 10/10 independent-process rounds at 50/50 with zero timeout/error/process-timeout, plus 50/50 `many_clients_one_process` and `one_client_many_requests` models. | proven for service API and current-artifact small-service 50-way gate | Keep unit, host CLI, native M2M, gateway service, and structured stress gates. Large-message service concurrency remains a separate P2 row and must not inherit this small-service PASS. |
| Client APIs | Client creation, request send, response take, GID, actual QoS, callbacks, and service availability are covered by service code paths and `test_service_inproc`; gateway service evidence verifies cross-RMW client behavior. | proven | None. Keep service/client focused tests and gateway service lane. |
| Actions | ROS 2 actions are exercised over their RMW service/topic primitives: host CLI Fibonacci action, native dual-board M2M Fibonacci action, and gateway action with send_goal/get_result counters, feedback/status movement, accepted goal, exact result sequence, and `SUCCEEDED` markers. | proven | None. Keep host CLI action, native M2M action, and gateway action lanes. |
| Serialized APIs | `rmw_publish_serialized_message`, `rmw_take_serialized_message`, serialize/deserialize, and serialized message sizing are exported; `test_bridge_backend` covers serialized bridge publish/take and `test_rmw_implementation` passed. | proven | None. Keep serialized bridge and upstream conformance tests. |
| Dynamic APIs | `rmw_take_dynamic_message` and `rmw_take_dynamic_message_with_info` delegate through serialized take and `rosidl_dynamic_typesupport_dynamic_data_deserialize`; dynamic tests are recorded in `complete-rmw-mdds-feature-closure/tasks.md`. | proven | None for current RMW dynamic-take API. Keep dynamic take host tests and bridge-loaned dynamic take regression. |
| Generated message type breadth | `MessageAdapter::Init` prefers generic CDR callbacks and keeps introspection metadata; tests include String, Int32, Int32MultiArray, nested fields, and service array payloads. | proven | None for current representative coverage. Add more conformance fixtures if a new ROS IDL shape fails. |
| Loaned messages: fixed-size raw shape | Direct bridge publisher/subscriber loans retain their existing true-loan paths. Broker subscriptions now request a pool only for a single fixed-size scalar raw layout, map it read-only, receive descriptor-only frames, return exact loan ids, and auto-return ordinary/serialized/sequence copy takes. RED/GREEN coverage includes 32 pinned slots plus one deferred RELIABLE sample, bridge-origin delivery, duplicate/foreign return, stale/bounds validation, disconnect unlink, host ASAN/focused TSAN, and bidirectional board 40/40 with `0600`/`r--s` evidence. | proven for direct bridge and fixed-scalar broker subscription loans | Keep the zero-copy contract, package/protocol tests, and the two-board pool/mapping marker. Do not generalize this row to complex broker subscription shapes. |
| Loaned messages: unbounded, sequence, dynamic, or filtered shapes | `MddsLoanArena` covers approved dynamic publisher loans. Broker protocol v2 now uses separate page-aligned immutable serialized payload and client-writeable typed-arena regions; generated messages are allocator-constructed in the mapped arena and destroyed before exact-id return. Host tests cover String, sequence, nested, filter rejection, ordinary/serialized auto-return, decode failure, pressure, teardown, disconnect, bounds, versions, and cross-process delivery. Both boards pass seven local cases and String/sequence/nested delivery in both directions with zero pool residue. | proven for the approved String, sequence, nested, and filter lifecycle scope | Keep protocol/pool/RMW/zero-copy tests and exact-artifact dual-board probes. Add a new shape-specific RED gate before extending beyond the documented 16 MiB payload, 32 MiB arena, or generated-type coverage. |
| QoS compatibility and durability | `rmw_qos_profile_check_compatible` delegates to `rmw_dds_common`, and host QoS tests remain green. The current production artifact passes the 9/9 message/QoS matrix, including best-effort, and the strict transient-local late join delivers exactly one retained sample. | proven on current production artifact | Keep QoS unit tests and rerun the deterministic late-joiner lane on any bridge whose transport behavior changes. |
| QoS and RMW events | Matched, deadline, liveliness, incompatible QoS, incompatible type, and message-lost events are enforced. A deterministic RED showed broker-mode callbacks at 2 while `rmw_take_event()` returned a stale graph count of 1. A successful endpoint-registration ACK now invalidates graph-cache freshness; unregister/cache clear already invalidated it. Publisher/subscription RED tests are GREEN, host upstream event is 4/4, and both RK3588A boards pass upstream event 4/4 on RMW `e5e6459e...`. | proven | Keep both immediate post-registration matched-status regressions, full package `test_event`, and upstream event on a fresh broker socket. |
| Content filters | `rmw_subscription_set_content_filter` and get APIs delegate to broker filter logic; `test_pubsub_inproc` and `test_event` cover string/numeric filters, nested fields, parameters, LIKE/IN/BETWEEN, boolean grouping, unary NOT, and filtered delivery. | proven | None. Keep content-filter unit tests. |
| Type identity and RIHS | `MessageAdapter::TypeHash`, IPC protocol type-hash fields, `test_graph`, `test_identity`, and `run_rmw_mdds_type_description_probe.sh` cover endpoint type hashes, GIDs, enclaves, and `RIHS01_` type-description service for `std_msgs/msg/String`. | proven | None. Keep graph/identity tests and type-description probe. |
| Security: fail-closed and local SROS2 policy | `LoadSecurityPolicy` requires readable `governance.xml` and `permissions.xml` in enforce mode, records publish/subscribe grants, and publisher/subscription creation checks topic access; host SROS2 policy contract and board SROS2 policy harness passed. | proven | None for local policy enforcement. Keep fail-closed, host policy, and board policy contracts. |
| Security: signed artifacts, governance semantics, and transport protection | `LoadSecurityPolicy()` verifies detached RSA/SHA-256 governance and permissions signatures, validates the identity chain and subject binding, and fails closed for missing/tampered protected artifacts. The current design performs side-effect-free protected-transport capability preflight in the client and authenticated+encrypted activation in the broker that owns the MDDS transport before opening its isolated `.protected` socket. The current production bridge passes signed-policy validation and authenticated+encrypted activation on both RK3588A boards; authorized protected pub/sub receives 60/60 samples and an unauthorized publisher is denied. | approved protected harness proven / broader security matrix incomplete | Keep signed-artifact, policy, bridge activation, checksum, protected board, and exact-artifact sanitizer gates. Add security-specific long-soak evidence and any still-uncovered security combinations before broader certification. |
| Network-flow metadata: direct RTPS | `rmw_publisher_get_network_flow_endpoints` and subscription equivalent report the direct RTPS user-data UDP endpoint when an RTPS participant exists; `NetworkFlowEndpointsReportDirectRtpsUserDataPort` covers this path. | proven | None for direct RTPS. Keep network-flow unit test. |
| Network-flow metadata: broker or MDDS bridge mode | In broker mode `rmw_init` skips the RTPS participant, and `rmw_publisher_get_network_flow_endpoints` plus `rmw_subscription_get_network_flow_endpoints` now report one explicit non-IP broker endpoint with unknown transport/protocol, zero port, and `rmw_mdds_broker` address metadata. `ohos/test_rmw_mdds_full_parity_red_contracts.sh` reports `RESULT|rmw_mdds_full_parity_broker_network_flow|PASS`; direct RTPS metadata remains covered separately. | proven | None for broker local IPC metadata. If future MDDS/DSoftBus transport exposes IP-like endpoint data, add a transport-specific endpoint contract instead of overloading the broker metadata row. |
| Logging | `rmw_set_log_severity` accepts defined severities and rejects unknown values; `test_logging` covers both paths. | proven | None. Keep `test_logging`. |
| Host delivery | Current package CTest and upstream `rmw_mdds_cpp` pass 24/24 and 16/16. Full package ASAN with leak detection and full package TSAN with halt-on-error each pass 24/24; dynamic publisher-loan ASAN/TSAN each pass 13/13. Runtime C/C++ functional tests pass 9/9 and 2/2, Fast RTPS typesupport passes its two gtests plus CLI pytest, all scoped contracts pass, and the delivery umbrella reaches its final marker. | current host functional and sanitizer delivery proven / aggregate lint tooling incomplete | Keep package, upstream, full sanitizer, generated-memory-resource, typesupport, and delivery gates. Install missing host lint tools before claiming a complete lint matrix. |
| Board delivery | RMW `4eb87ee4...`, broker `d754adba...`, broker dynamic probe `58d4f756...`, runner `db694210...`, and historical bridge `e2c6a99c...` match on both boards. That artifact passes the persistent-broker 16-program AArch64 suite, dynamic arenas, P0/P1, 50-way, signed SROS2, action-bag, rosbag2, exact 16 MiB, churn, performance, and the two-hour service soak. Current bridge `c3b614b2...` separately passes backend 171/171 on each board, including long-name compatibility, 14/14 mapped restarts per board with no kill fallback, and bidirectional topic/service/action 3/3 without a SoftBus restart. | broad historical exact-artifact evidence plus current affected restart/M2M evidence proven | Preserve exact hashes and do not migrate broader `e2c6a99c...` results to `c3b614b2...` until rerun. Do not infer product acceptance from technical gates. |
| P2/P3 production evidence gates | Functional conformance, signed protected security, action bag, stress, large-message, graph churn, host sanitizers, and performance thresholds are now repeated on the current source/production line. Complex broker subscription arenas are proven for the approved shapes. Current-source full-stack board ASAN/TSAN passes 16/16 per board with detector controls, current production 50-process service stress passes 10/10 rounds, rapid process restart is closed by process-scoped outgoing identities, and the two-hour sequential service soak passes 36,000/36,000 with zero timeout/error. rmw_mdds small-message performance remains materially behind Fast DDS. | incomplete / current technical sub-gates proven; product acceptance and final disposition still open | Complete the final evidence-matrix audit and obtain product acceptance for the performance gap before Task 5.5. |
| Delivery handoff | DSoftBus/MDDS branch `mdds-claude` contains remote-only `cfdaf1f1e6629cc91c260061aba2c5e39f378a3b`, transient/reconnect `2df8146dde97111f452d5527f12bed10034ac20b`, and gateway node-sync `6551d9cb7`. ROS 2 branch `jazzy-ubuntu-20.04` contains approved design `8a75658687029a7c59df16a98091719c7c1d36f3`, broker implementation `500f4fa2f64deb663111149e8b6f109e93230558`, current service/domain fix `92ff23a`, and gateway acceptance tooling `c06ecd5`. Earlier unrelated dirty-worktree changes remain unstaged. This is a scoped implementation handoff, not production/full-feature completion. | proven | Keep future commits scoped and do not absorb unrelated dirty-worktree changes. |

## Current Completion Decision

The current checkout has substantial scoped host and RK3588A evidence. The remote-only and shared service bridge
changes close the recorded ignore-local and small-service 50-way defects; the latest DSoftBus bridge additionally
closes the previously recorded in-process channel-recovery defects and focused 16MiB concurrent, long60, boundary,
and 2h concurrent large-message functional gates. The process-scoped outgoing identity now closes the isolated
fixed-identity cross-process restart defect with current-board evidence.
Artifact-specific historical soak, bag, gateway, security, and current bridge evidence must remain distinct.

The broad `rmw_mdds_cpp`/MDDS production or "perfect/all ROS 2 middleware features" goal is **not complete**.
The latest audit no longer treats allocator/serialized-size support skips as missing APIs. Clean direct loans,
fixed-scalar broker loans, and the approved broker dynamic String/sequence/nested shared arenas have host and
dual-board evidence. The dedicated control-channel gate closes the former ACK-tail blocker. Historical evidence
remains attached to its recorded hashes; the `4eb8.../d754.../e2c6...` historical production line has dynamic board
probes, conformance, topic/service/action regressions, 14/14 restart recovery per board, 50-process service stress
10/10, P0/P1, signed SROS2, action-bag, rosbag2, exact 16 MiB repeat3, graph/action churn, and full-stack
performance. Current-source full-stack ASAN/TSAN also passes 16/16 per board with working detector controls. The
full IPC broker binary is ASAN/TSAN-clean at 35/35. The current two-hour service soak passes 36,000/36,000 with
zero timeout/error. Performance-gap acceptance and explicit remaining skip/tooling disposition are incomplete.
Current bridge `c3b614b2...` has affected 171/171, per-board 14/14 restart, and bidirectional M2M evidence only;
the broader `e2c6a99c...` results have not all been repeated on it.
The dynamic-take payload leak and approved complex broker-loan scope are closed and are no longer blockers.
Do not report this state as
`production-ready`, `full-feature complete`, or full RMW compatibility certification.

### 2026-07-10 EDT / 2026-07-11 CST Plan A exact-artifact supplement

This supplement records only the newly affected DSoftBus surfaces. Both boards were hash-verified with
`libmdds_bridge_shared.z.so=454bb9929620111193414b8f1b4aae9258c65d6c7ab63c16d5fd903761e5f80b`; the
latency checks used `mdds_demo=2d9e1b7aca2c7d4aa0d4b9085775d0aa9a7d342e2bd55d8936ce9bef8b11d508`.

| Surface | Exact-artifact evidence | Classification |
| --- | --- | --- |
| Backend shutdown ordering | A focused blocked-callback test proved stop waits for in-flight dispatch, rejects late frames, and restarts; the full RK3588A backend suite passed 168/168. | proven for the affected DSoftBus lifecycle |
| Protected SROS2 shutdown | Each board completed 10/10 protected probe exits. Signed policy and authenticated/encrypted activation passed on both boards, authorized delivery was 60/60, unauthorized publish was denied, and the tampered/missing/identity/enforce/permissive matrix behaved as specified. | proven for this exact bridge and matrix; broader security certification remains open |
| Cross-device RELIABLE latency result integrity | The pre-fix runner was retained as RED evidence because partial 5/10 to 9/10 delivery returned PASS. Exact in-flight correlation then passed seven sizes from 128B through 4MiB at 10/10 each. Forced partial RELIABLE returned rc=1; forced partial BEST_EFFORT retained rc=0 observational behavior. | benchmark false-green closed |
| Runtime manager test gate | `MddsNodeManagerTest` still fails two initialization fixtures: both return `-7` before the expected rollback/filter point because the fixtures omit active DSoftBus mode registration. | failing independent gate |

This supplement does not change the completion decision. The shutdown crash and latency false-green are closed,
but the failing manager test plus independent conformance, sanitizer, performance, and remaining P2/P3 evidence
keep Task 5.5 and the production/full-feature goal open.

### 2026-07-11 lane-worker ownership follow-up

The Plan A 4MiB rerun exposed a real `LaneWorkerMain` UAF: data and control managers shared a global worker-context
array/count, and control-first shutdown freed a context still used by a data worker. The RED manager-capacity test
returned `-7` for the control manager's first worker after the data manager filled 12 slots. Context ownership is
now manager-local and indexed by lane slot; the regression also stops control first and proves the data lane still
accepts a push.

Final exact artifacts are bridge `db5b9d93...`, demo `7054cec3...`, lane test `ad573e46...`, and backend test
`3d5d656e...`. RK3588A results are lane 36/36, backend 168/168, seven RELIABLE payload sizes through 4MiB at
10/10 each, forced partial RELIABLE `160/1000` with rc 1, forced partial BEST_EFFORT `109/1000` with rc 0, and
signed protected SROS2 60/60 plus unauthorized denial. No new demo faultlog was created. NodeManager is now
30/30 and PubSub 198/198 on their unchanged exact binaries, superseding the historical manager-test row above.
Native `rmw_mdds_cpp` M2M on the same bridge also passed topic/service/action 3/3 with exact 40/40 topic delivery,
service sum 42, and Fibonacci `SUCCEEDED` with sequence `0,1,1,2,3,5`.

At that checkpoint Task 5.5 remained open because the worker UAF fix did not close full-stack RMW performance,
broader security/action-bag, then-unclosed broker-loan breadth, or every remaining independent P2/P3 gate. The
newer conformance/loan and full-stack sanitizer supplements at the top supersede the old skip/sanitizer
classifications.
