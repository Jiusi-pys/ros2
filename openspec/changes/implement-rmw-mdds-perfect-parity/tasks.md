## 1. OpenSpec Baseline

- [x] 1.1 Create and validate the proposal, design, and spec for the approved generator/typesupport, MDDS memory-model, signed SROS2, and authenticated transport scope.
- [x] 1.2 Re-run `openspec validate implement-rmw-mdds-perfect-parity --strict` and `openspec validate --changes --strict` for the current RED-contract task status.

## 2. RED Contracts For Dynamic Loaned Messages

- [x] 2.1 Add a failing host unit test for unbounded string loan publish that requires string bytes to be MDDS bridge-loan backed.
- [x] 2.2 Add a failing host unit test for dynamic sequence loan publish that requires sequence element storage to be MDDS bridge-loan backed.
- [x] 2.3 Add a failing host unit test for nested dynamic loan storage and lifetime.
- [x] 2.4 Add or update a script contract that runs the dynamic loan tests and emits explicit RED/GREEN markers for generalized loaned-message shapes.

RED evidence on 2026-07-03: `bash ohos/test_rmw_mdds_full_parity_red_contracts.sh` runs
`RmwMddsBridgeLoanedRmw.DISABLED_FullParityLoaned*` and emits
`RESULT|rmw_mdds_full_parity_loaned_shapes|RED|status=1`. The RED set now covers unbounded string,
dynamic sequence, and nested dynamic storage; all fail because the publisher does not advertise loan support
and `rmw_borrow_loaned_message()` returns `RMW_RET_UNSUPPORTED`.

## 3. RED Contracts For Signed And Protected SROS2

- [x] 3.1 Add a failing host test that accepts valid signed protected governance and permissions artifacts only after signature and identity validation.
- [x] 3.2 Add failing host tests that reject unsigned protected governance, tampered signed permissions, and identity/certificate mismatch before endpoint creation.
- [x] 3.3 Add a failing protected-transport contract that rejects signed protected policy when authenticated/encrypted MDDS/DSoftBus transport cannot be established.
- [x] 3.4 Add or update a board harness that emits signed-policy, authorized protected traffic, denied protected traffic, and authenticated/encrypted transport markers.

RED evidence on 2026-07-03: `bash ohos/test_rmw_mdds_full_parity_red_contracts.sh` runs
`RmwMddsPubSub.DISABLED_FullParitySros2*` and emits
`RESULT|rmw_mdds_full_parity_signed_security|RED|status=1`. The RED set now includes valid signed
protected-policy acceptance with authenticated/encrypted transport, unsigned protected governance rejection,
tampered signed permissions rejection, signed identity mismatch rejection, and missing authenticated transport
diagnostics. At that RED checkpoint, the implementation failed at the old protected-governance unsupported diagnostic before
signature, identity, or authenticated-transport semantics are implemented.

Host GREEN evidence on 2026-07-03: after adding OpenSSL-backed RSA/SHA-256 detached signature validation,
certificate trust-anchor checks, identity certificate binding, and an explicit authenticated/encrypted
protected-transport gate, `bash
ohos/test_rmw_mdds_full_parity_red_contracts.sh` emits
`RESULT|rmw_mdds_full_parity_signed_security|PASS`. This proves host signed-artifact semantics and
fail-closed protected-policy handling, but not RK3588/KaihongOS MDDS/DSoftBus protected transport activation.

Board harness evidence recorded on 2026-07-04: `ohos/test_rmw_mdds_delivery_contracts.sh` statically
requires `ohos/tools/run_cross_board_rmw_mdds_sros2_protected.sh`, and that board harness generates signed
governance/permissions/identity artifacts, deploys them to both boards, validates signed policy, reports
authenticated/encrypted protected transport, checks authorized protected pub/sub, and checks unauthorized
publish denial. After refreshing the OHOS overlay and restarting `softbus_server` on both boards,
`HDC_BIN=hdc ohos/tools/run_cross_board_rmw_mdds_sros2_protected.sh
<rk3588a-board-a> <rk3588a-board-b> 109` emitted
`RESULT|board_sros2_signed_policy|PASS` and
`RESULT|board_sros2_protected_transport|PASS|authenticated=1|encrypted=1` on both boards,
`RESULT|board_sros2_protected_authorized_pubsub|PASS|topic=/mdds_sros2_protected_allowed|received=20`,
`RESULT|board_sros2_protected_unauthorized_publish|PASS|topic=/mdds_sros2_protected_forbidden|denied`,
and `cross_board_rmw_mdds_sros2_protected_ok`.

Post-checkpoint correction on 2026-07-06: later DSoftBus/MDDS source now routes
`enhance/mdds/examples/ros2_mdds_demo/bridge_lib/src/mdds_bridge.c::MddsBridgeActivateProtectedTransport()` to
`MddsConnManagerActivateProtectedTransport()`, and the protected probe is deployed in the ROS2 board overlay.
The old 2026-07-04 board marker still is not accepted as current proof. A later domain-221 protected
harness/artifact re-read with bridge sha `707c8399dc1ea83f0f8200728781bf1bd0acd98348b082c251ed083520ec64aa`
passed signed policy and protected transport activation on both boards, but failed authorized protected pub/sub
with `received=0`; fresh hilog and source review showed `ClientGetChannelIdAndTypeBySocketId()` returned
business type through its `type` output while `GetEncryptByChannelId()` matches channel type. That failure is
now superseded by the channel-type RED/GREEN fix in DSoftBus and a current domain-290 board rerun with bridge
sha `8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc`: signed policy PASS on both boards,
protected transport activation PASS on both boards, authorized protected pub/sub PASS with `received=58`,
unauthorized publish denied, and `cross_board_rmw_mdds_sros2_protected_ok`.

## 4. Generator/Typesupport And MDDS Memory Model

- [x] 4.1 Identify the exact generated-message or type-support extension point that can construct ABI-safe loan-aware dynamic message storage.
- [x] 4.2 Add an MDDS bridge loan arena or segment ownership API for dynamic member storage and lifetime tracking.
- [x] 4.3 Implement the smallest unbounded string dynamic-loan GREEN slice without serialize/copy/default-heap fallback.
- [x] 4.4 Extend the loan-aware path to dynamic sequences and nested dynamic members only after their RED tests fail for the expected reason.
- [x] 4.5 Preserve fixed-size raw bridge loan behavior and zero-copy contracts while dynamic loan support is added.

Extension-point evidence on 2026-07-03: graph-guided inspection of `rosidl_generator_cpp/resource/msg__struct.hpp.em`
shows generated message templates already expose allocator-aware constructors at lines 204-245, but the public
default alias remains `<std::allocator<void>>` at lines 362-364. `rosidl_typesupport_introspection_cpp/resource/msg__type_support.cpp.em`
emits only the default `init_function` and `fini_function` into `MessageMembers`, and
`include/rosidl_typesupport_introspection_cpp/message_introspection.hpp` exposes no loan/allocator-aware
construction hook. The ABI-safe extension point is therefore a generated loan-construction/loan-view hook in
the generator or introspection type-support surface, not a cast inside `rmw_mdds_cpp`.

Arena GREEN evidence on 2026-07-03: `MddsLoanArena` was added in `src/loan_arena.hpp` and `src/loan_arena.cpp`
with aligned segment allocation, bounds checks, ownership reset, `Contains()`, byte-use, and segment-count
queries. `BridgePublisherLoanRecord` now carries an arena initialized from the borrowed bridge loan buffer.
The RED-to-GREEN cycle was `cmake --build build/rmw_mdds_cpp --target test_loan_arena -j4` failing first on
missing `loan_arena.hpp`, then passing after implementation. Focused verification also ran
`./build/rmw_mdds_cpp/test_loan_arena --gtest_color=no` and `./build/rmw_mdds_cpp/test_bridge_loaned_rmw
--gtest_color=no`; `test_loan_arena` passed 2/2 and `test_bridge_loaned_rmw` passed 9/9, preserving fixed-size
raw bridge loans.

Dynamic loan GREEN evidence on 2026-07-03: `MddsLoanArena` now has a guarded allocation hook for active
publisher loans, `MessageAdapter` recognizes dynamic C++ string/sequence/nested storage, and
`rmw_borrow_loaned_message()` places the ROS message object inside the bridge loan while arming the arena for
dynamic member allocations. `rmw_publish_loaned_message()` publishes the same bridge loan through
`BridgeBackend::PublishLoaned` only after `MessageAdapter::PrepareLoanedDynamicMddsPayload()` confirms the
dynamic storage is still backed by that bridge loan; the RMW loaned-publish body no longer calls
`EncodeMddsIntoBuffer` or borrows a second payload loan. Verification after the sequence/nested slice:
`cmake --build build/rmw_mdds_cpp --target test_bridge_loaned_rmw test_loan_arena -j4` passed,
`./build/rmw_mdds_cpp/test_loan_arena --gtest_color=no` passed 2/2,
`./build/rmw_mdds_cpp/test_bridge_loaned_rmw --gtest_filter=RmwMddsBridgeLoanedRmw.DISABLED_FullParityLoaned* --gtest_also_run_disabled_tests --gtest_color=no`
passed all three full-parity loaned-shape tests, the normal `test_bridge_loaned_rmw --gtest_color=no` passed
9/9, and `ohos/test_rmw_mdds_zero_copy_contracts.sh` emitted `rmw_mdds_zero_copy_contracts_ok`.

## 5. Signed SROS2 And Authenticated Transport Implementation

- [x] 5.1 Implement signed governance and permissions artifact loading, signature validation, certificate-chain validation, and identity binding.
- [x] 5.2 Preserve the existing unprotected local XML policy path for `NONE` protection kinds.
- [x] 5.3 Implement fail-closed protected-policy behavior for unsigned, tampered, mismatched, or unreadable artifacts.
- [x] 5.4 Activate authenticated/encrypted MDDS/DSoftBus transport for valid protected governance and fail closed if the protected lane cannot be established.

Implementation evidence on 2026-07-03: `LoadSecurityPolicy()` now accepts protected governance only after
`governance.xml.sig` and `permissions.xml.sig` verify against a trusted `permissions_ca.cert.pem`,
`identity.pem` validates against `identity_ca.cert.pem`, the identity certificate common name matches the
permissions `subject_name`, and the protected transport gate reports authenticated plus encrypted transport.
The `NONE` protection local XML policy path remains green through
`ohos/test_rmw_mdds_sros2_policy_contracts.sh`. Current board evidence closes the protected-lane proof for the
approved harness: after the channel-type fix, the latest domain 290 rerun passed signed policy, authenticated/encrypted
activation, authorized protected pub/sub, and unauthorized denial on two RK3588A boards.

Host activation evidence on 2026-07-03: `BridgeBackend` now binds optional
`MddsBridgeActivateProtectedTransport(uint32_t)`, and `LoadSecurityPolicy()` attempts bridge protected-lane
activation before accepting signed protected governance when the env-only host override is absent. TDD RED was
`./build/rmw_mdds_cpp/test_pubsub_inproc
--gtest_filter=RmwMddsPubSub.DISABLED_FullParitySros2ActivatesBridgeProtectedTransport
--gtest_also_run_disabled_tests --gtest_color=no` failing with the missing authenticated/encrypted transport
diagnostic and fake bridge activation count `0`; GREEN passes the same test and the full
`RmwMddsPubSub.DISABLED_FullParitySros2*` group passes 6/6. The host path proves bridge activation semantics.
The later DSoftBus source now routes `MddsBridgeActivateProtectedTransport()` to
`MddsConnManagerActivateProtectedTransport()` and has focused RK3588A unit plus full backend proof. The ROS2
protected probe and current bridge have now been rebuilt/deployed and rerun; RK3588/KaihongOS protected signed
SROS2 proof is closed for the current harness by the domain-290 PASS markers. Broader production security
certification remains separate from this task because permissive/enforce combinations, tampered/missing
credential board matrices, long soak, sanitizer, and performance evidence are still outside this proof.

DSoftBus bridge export evidence on 2026-07-03: in
`/home/kaihong/M-DDS/OpenHarmony_lyl/foundation/communication/dsoftbus`, the contract
`bash enhance/mdds/tests/scripts/test_bridge_protected_transport_contract.sh` first failed on the missing
authenticated/encrypted flags and missing `MddsBridgeActivateProtectedTransport` prototype, then passed with
`bridge_protected_transport_contract_ok` after adding the additive bridge C API. `./build.sh --product-name
khd_rk3588_a --build-target mdds_bridge_shared` built `libmdds_bridge_shared.z.so` successfully, and a
post-format direct rebuild with `prebuilts/build-tools/linux-x86/bin/ninja -w dupbuild=warn -C
out/arm64/targets mdds_bridge_shared` also passed. Export verification with
`prebuilts/clang/ohos/linux-x86_64/llvm/bin/llvm-nm --defined-only --dynamic
out/arm64/targets/communication/dsoftbus/libmdds_bridge_shared.z.so` shows
`MddsBridgeActivateProtectedTransport` exported alongside `MddsBridgeInit` and `MddsBridgeShutdown`. This
initial ABI/fail-closed activation hook and build proof was not the same as an enforcing protected transport.
It has since been extended on the DSoftBus side so the bridge calls the connection-manager enforcement gate.
The current bridge/probe is now built into the ROS2 board overlay, and the implementation item is closed for
the approved harness by the current domain-290 protected SROS2 PASS.

Current protected-transport recheck on 2026-07-06: the same DSoftBus contract was rerun and emitted
`bridge_protected_transport_contract_ok`, and `ohos/test_rmw_mdds_script_contracts.sh` emitted
`rmw_mdds_script_contracts_ok`. `./build.sh --product-name khd_rk3588_a --ccache
--no-prebuilt-sdk -T MddsDSoftBusBackendTest` succeeded, and the RK3588A focused run
`./MddsDSoftBusBackendTest --gtest_filter="MddsConnManagerTest.ProtectedTransport*"
--gtest_output=xml:/data/local/tmp/MddsDSoftBusBackendTest_protected.xml` passed 7/7 with `BOARD_RC=0`, covering
unencrypted incoming rejection, encrypted incoming acceptance, the channel-type lookup regression, existing
unencrypted rejection, encryption query failure, and short incoming/outgoing DSoftBus encryption-info visibility
delay. The full `./MddsDSoftBusBackendTest` also passed 140/140 with `BOARD_RC=0`. Both RK3588A boards now contain
`/data/local/tmp/ohos-colcon-rk3588a/lib/rmw_mdds_cpp/rmw_mdds_bridge_protected_transport_probe` sha
`6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`; the latest protected harness path uses bridge
sha `8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc`, softbus-client sha
`e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`, `librmw_mdds_cpp.so` sha
`ee2a285ca5484f1d90f4cf35e4272efa998ad0b32a8bed7153401d8a83e126a5`, and broker sha
`0ce6c5eb3bfe2e028df3103d3cb46499d6b2e909565fc600af34db4a4dc3427d`. Domain 290 protected
harness/artifact output returned signed-policy PASS and protected-transport activation PASS on both boards,
authorized protected pub/sub PASS with `received=58`, unauthorized publish denied, and final
`cross_board_rmw_mdds_sros2_protected_ok`. The prior domain-221 `received=0` failure remains recorded as the
root-cause artifact for the fixed businessType/channelType mismatch.

Late recorded-deployment note on 2026-07-06: subsequent broker graph/event work refreshed the then-current
RK3588A rmw/broker deployment to `librmw_mdds_cpp.so` sha
`ee2a285ca5484f1d90f4cf35e4272efa998ad0b32a8bed7153401d8a83e126a5` and broker sha
`0ce6c5eb3bfe2e028df3103d3cb46499d6b2e909565fc600af34db4a4dc3427d` on both boards. The deployed
rclpy graph-cleanup artifacts remain `type_description_service.py`
`344bd340293bf65bf998b0a2fed3b00cc344e2ad3865db7ee8f9be98afa4deda` and `_rclpy_pybind11`
`de2a4f7ca198aeda71dca0f9b32ee3d75706a8cf645be158ee8d75757ba1c6f8`. The protected
bridge/probe/softbus-client hashes are the values listed above. Domain 290 is the current protected signed
SROS2 PASS artifact after the latest rmw/broker redeploy.

## 6. Verification And Final Acceptance Sync

- [x] 6.1 Run focused host unit tests for dynamic loans and signed/protected SROS2 behavior.
- [x] 6.2 Run `ohos/test_rmw_mdds_delivery_contracts.sh` and affected full-parity contracts.
- [x] 6.3 Rebuild the OHOS overlay, deploy to RK3588/KaihongOS boards, and run affected native, gateway, and protected SROS2 board lanes against the current DSoftBus bridge.
- [x] 6.4 Update `complete-rmw-mdds-full-parity-acceptance/parity-matrix.md` and tasks with exact command evidence.
- [x] 6.5 Resolve the delivery endpoint with the user or execute the requested local/archive/push/PR/Gerrit path before marking the persistent goal complete.

2026-07-11 Task 6.3 evidence: DSoftBus bridge `c59c351a...` was rebuilt, deployed to both RK3588A boards at
both actual load paths, and verified by SHA-256 readback. The affected connection/large-message lanes passed the
final backend suite 158/158, three exact-wire 16MiB concurrent4x10 domains at aggregate 120/120, sequential
long60 at 60/60, and exact-cap/`+1` boundary cases 6/6. The final bridge then passed native M2M 3/3 (topic
40/40, service sum 42, action `SUCCEEDED`), gateway pub/sub 8/8, gateway service sum 42, gateway action exact
Fibonacci sequence with terminal `SUCCEEDED`, parameter value 4242, lifecycle `unconfigured -> inactive`, and
protected SROS2 signed-policy plus authenticated/encrypted activation on both boards, authorized delivery of 60
messages, and unauthorized publish denial. Task 6.3 is therefore complete for the approved affected board lanes.

Delivery endpoint resolution on 2026-07-04: the executed endpoint is local handoff because no push, PR,
Gerrit submission, or OpenSpec archive was requested. The paired local commits are DSoftBus/MDDS
`ba9a5f605` on `mdds-claude` and ROS 2/rmw_mdds implementation `86ab375` on `jazzy-ubuntu-20.04`; this
OpenSpec evidence update records the final local handoff state.

Post-checkpoint status sync on 2026-07-06: DSoftBus/MDDS HEAD is `30d53a2c2`, and retained board artifacts still
show the earlier 10-round 50-way stress PASS. The then-latest deployed overlay reached `CLIENT_CREATED=50`,
`CLIENT_SENT=50`, `SERVER_REQ=50`, and `CLIENT_OK=50`, but failed default `processes --clients 50 --timeout 75`
with `PROCESS_TIMEOUT=39`, `ELAPSED_MS=90847`, and `PASS=False`; cleanup-marker round 61 narrowed that old
blocker to response-afterward `node.destroy_node()`/process cleanup instead of request admission, server take, or
response delivery.

Live status re-read for this documentation sync on 2026-07-06: the lightweight contracts still pass
(`bridge_protected_transport_contract_ok`, `rmw_mdds_script_contracts_ok`, and `bash -n` for the updated board
runners). Direct board re-read confirmed the service-stress JSON files above, the historical sequential
service-soak request-12 timeout, corrected coverage2 4MiB topic/service-wire logs, and the currently retained graph
summaries for CLI node/topic/service 100/100 plus rclpy_action 100/100. The rclpy fast-mode 1000/1000 graph
result remains recorded evidence from the previous run, but it was not freshly re-read from the current
retained summary because later graph runs have overwritten that summary file.

Live status spot-check on 2026-07-07: re-ran `bridge_protected_transport_contract_ok`,
`rmw_mdds_script_contracts_ok`, and `/home/kaihong/ros2` `openspec validate --changes --strict` (`4 passed,
0 failed`). `hdc list targets` printed both RK3588A targets and then returned the known host exit 139. After
fixing client cleanup and pre-ACK delivery handling, the `ee2a285...` service-stress repair overlay was deployed to both boards.
Direct hash re-read matched `librmw_mdds_cpp.so=ee2a285...`, `rmw_mdds_broker=0ce6c5eb...`,
`libmdds_bridge_shared.z.so=8b918aa3...`, and `libsoftbus_client.z.so=e1771298...`. The ee2a service-stress JSON
shows default 50 independent-process stress is no longer the active P2 blocker for that overlay: smoke round 82 passed 50/50 with
`PROCESS_TIMEOUT=0`, and rounds 83-92 produced 10/10 PASS summary files with `SERVER_REQ=50`, `CLIENT_OK=50`,
and `PROCESS_TIMEOUT=0`. Retained graph summaries directly show node/topic/service 100/100 PASS and rclpy_action
100/100 PASS; retained service-soak logs directly show 10000/10000 client/server completion. Domain 286/288/289
remain prior command-record labels rather than fields present in the retained summary/client/server logs re-read
in this spot-check. The production/full-feature goal remains open for the other P2/P3 evidence gaps.

Sequential service-soak correction on 2026-07-06: after extending
`ohos/tools/run_cross_board_rmw_mdds_service.sh` with `RMW_MDDS_SERVICE_REQUESTS`, a fresh RK3588A 10-request
positive control on domain 281 passed with `client_sent=10`, `client_ok=10`, `server_req=10`, `timeout=0`,
and `error=0`. A fresh 20-request run on domain 282 failed with `TRIGGER_CLIENT_TIMEOUT index=12`,
`client_sent=12`, `client_ok=11`, `timeout=1`, and the server log reaching only `trigger_count=11` without
`TRIGGER_SERVER_DONE`. This proved a real one-client sequential-request blocker, not a documentation-only gap.
After adding a broker regression for service bridge internal history and expanding broker service/client bridge
transport QoS beyond the public service depth of 10, `build/rmw_mdds_cpp/test_ipc_broker` passed 14/14 and
RK3588A reruns passed domain 283 with 20/20 requests, domain 284 with 1000/1000 requests, and domain 285 with
10000/10000 requests, all with `timeout=0` and `error=0`. This closes the request-12 blocker for the current
broker-mode sequential gate; 2h stability and broader P2/P3 evidence remain open.

Fresh host status sync on 2026-07-06: after fixing broker service availability targeted graph refresh,
graph API bad-argument validation order, and host CLI probe robustness, `bash
ohos/test_rmw_mdds_delivery_contracts.sh` emitted `rmw_mdds_delivery_contracts_ok`. The run included package
CTest `23/23`, `test_rmw_implementation` rmw_mdds subset `16/16`, type-description PASS, and host CLI
pub/sub/service/action/params/lifecycle/graph/QoS/transient-local/message-info PASS markers. A separate full
`source install/setup.bash && ctest --test-dir build/test_rmw_implementation --output-on-failure` run passed `69/70`; the only failure was
host tooling, because the `cppcheck` executable is not installed.

Latest host and board status sync on 2026-07-06: after adding a defensive `DecodeEndpointList()` payload-size guard
and refreshing the broker graph before broker-mode matched-event current-count calculations, the focused
upstream event rerun
`source install/setup.bash && LD_LIBRARY_PATH=$PWD/build/rmw_mdds_cpp:$PWD/install/lib:$LD_LIBRARY_PATH
RMW_IMPLEMENTATION=rmw_mdds_cpp ctest --test-dir build/test_rmw_implementation -R 'test_event__rmw_mdds_cpp$'
--output-on-failure` passed 1/1. A follow-up `bash ohos/test_rmw_mdds_delivery_contracts.sh` emitted
`rmw_mdds_delivery_contracts_ok`, with package CTest `23/23`, `test_rmw_implementation` rmw_mdds subset
`16/16`, type-description PASS, and host CLI pub/sub/service/action/params/lifecycle/graph/QoS/transient-local/
message-info PASS markers. The overlay was rebuilt and redeployed to RK3588A; focused board reruns then passed
graph churn domain 286/287, sequential service soak domain 288/289, and protected SROS2 domain 290, while that
intermediate overlay still failed the default 50 independent-process service stress hard gate due `PROCESS_TIMEOUT`.
The later 2026-07-07 `ee2a285...` overlay supersedes that blocker with rounds 83-92 10/10 PASS.

P2 graph-churn progress on 2026-07-06: added `ohos/tools/run_rmw_mdds_board_graph_churn.sh`, extended it to
cover node/topic/service graph entries, and ran a 100-round RK3588A gate with `GRAPH_CHURN_PASS=100`,
`GRAPH_CHURN_FAIL=0`, topic/service created/destroyed counters all at `100`,
`RESULT|rmw_mdds_board_graph_churn|PASS|rounds=100|topics_created=100|services_created=100|topics_destroyed=100|services_destroyed=100`,
and `rmw_mdds_board_graph_churn_ok`. Treat this as the current 100-round node/topic/service graph-churn
checkpoint. A later rclpy fast-mode run closes the 1000-round graph-churn gate, and a later `rclpy_action`
run closes the current 100-round action-specific graph-churn gate.

P2 graph-churn late status on 2026-07-06: rclpy same-process mode initially remained RED after explicit broker
unregister and graph-update epoch ordering. Domains 231/236/237 and the domain-249 one-round recheck failed
with `found=1 gone=0`: custom topic/service entries disappeared, but destroyed rclpy node names and
`get_type_description` services remained visible. The root cause was later narrowed to rclpy's
type-description service wrapper missing the normal `rcl_service_fini()` deleter, so destruction did not call
`rmw_destroy_service()`/broker unregister for `/get_type_description`. The current fix in
`rclpy/src/rclpy/type_description_service.cpp` plus Python-side explicit service destruction is deployed to
both boards. Current RK3588A evidence: domain 252 debug one-round reaches endpoint count zero and passes
`RESULT|rmw_mdds_board_graph_churn_fast|PASS|rounds=1|topics_created=1|services_created=1|topics_destroyed=1|services_destroyed=1`;
domain 256 five-round fast-mode passes with `GRAPH_CHURN_PASS=5`, `GRAPH_CHURN_FAIL=0`, all created/destroyed
counters at `5`, and `rmw_mdds_board_graph_churn_fast_ok`. A later explicit-refresh cache fix handles same-broker stale frames as a successful refresh and limits generic graph snapshot settle to 100ms. After rebuilding and deploying `librmw_mdds_cpp.so=4459fbe764d98775c91c5505c20730560866e7cfacaf44f5d6ca904b372f9386`, domain 264 dropped from about 39s/round to 0.5-0.7s/round and passed 2/2, and domain 265 rclpy fast-mode passed 100/100 with all created/destroyed counters at `100`. After fixing broker inactive connection reaping, domain 267 rclpy fast-mode passed 1000/1000 with `GRAPH_CHURN_PASS=1000`, `GRAPH_CHURN_FAIL=0`, and all created/destroyed counters at `1000`. The runner now also supports `RMW_MDDS_GRAPH_CHURN_MODE=rclpy_action`; RK3588A domain 268 passed 5/5 as a smoke check, and domain 269 passed 100/100 action-specific graph churn with `GRAPH_CHURN_ACTIONS_CREATED=100`, `GRAPH_CHURN_ACTIONS_DESTROYED=100`, `RESULT|rmw_mdds_board_graph_churn_action|PASS|rounds=100|actions_created=100|actions_destroyed=100`, and `rmw_mdds_board_graph_churn_action_ok`. Long-soak graph evidence remains incomplete.

P2 coverage2 refresh on 2026-07-06: `ohos/tools/run_cross_board_rmw_mdds_coverage2.sh` now uses rclpy
publisher/subscriber helpers for large payload lanes, avoiding missing board `tr`, CLI argument-length
limits, and truncated `ros2 topic echo` output. The script supports
`RMW_MDDS_COVERAGE2_ONLY=large|transient|liveliness|bag`, configurable large-lane warmup/match timeout, and
publisher/subscriber completion markers. The transient-local lane uses strict one-shot publish plus keep-alive
for true late-joiner retained-history replay. Current isolated RK3588A evidence:
`RMW_MDDS_COVERAGE2_ONLY=liveliness ... 141` emitted
`RESULT|cov2_qos_liveliness|PASS|received=16`; `RMW_MDDS_COVERAGE2_ONLY=bag ... 138` emitted
`RESULT|cov2_rosbag2_record_play|PASS|recorded_files=2|recorded_messages=56|played_received=51`; and five
strict transient-local reruns on domains 150-154 emitted
`RESULT|cov2_transient_local_replay|PASS|late_joiner_got_retained=12`. The old domain-143 transient-local
failure is superseded by this 5/5 rerun. At this checkpoint, large payload appeared incomplete on the
cross-board path:
`RMW_MDDS_COVERAGE2_ONLY=large ... 160` passed 512KiB and 1MiB with `valid_len=1`, but failed 1.5MiB with
`received=0|valid_len=0`; standalone domain 161 also failed. Follow-up DSoftBus `maxSendSize` work produced a
real RED/GREEN unit correction:
the new RK3588A gtest failed before the fix with actual `4194240` versus expected `32768`, then passed after
`dsoftbus_backend.c` advertised `MDDS_DEFAULT_MAX_SEND_SIZE`. `deploy_rmw_mdds_delta.sh` installed bridge sha
`a8618d2c27e41beadc2d308d08e4dda58229708442ec10e417b5d575d0547eb8` on both boards, but domain 166 and
domain 173 large-only reruns still passed 512KiB/1MiB and failed 1.5MiB. A later RED/GREEN DSoftBus
defragmenter timeout fix made
`MddsDefragmenterTest.RecentFragmentKeepsEntryAlive_006b:MddsDefragmenterTest.SweepExpiredEntry_006`
pass `2/2` on RK3588A with `BOARD_EXIT=0`; after rebuilding `mdds_bridge_shared` and deploying bridge sha
`2975e13db300366f143ec3a32933d73c2bd3e769075f5d5e69978e5da7455c50` to both boards, the domain 184
large-only rerun still passed 512KiB/1MiB and failed 1.5MiB with
`RESULT|cov2_large1500k|FAIL|received=0|valid_len=0`.
Status-document recheck during this earlier sync re-read board logs and confirmed the then-current boundary:
`COV_BIGSUB_DONE size=524288 received=1 valid=1`, `COV_BIGSUB_DONE size=1048576 received=1 valid=1`, and
`COV_BIGSUB_DONE size=1572864 received=0 valid=0`. At that checkpoint both boards reported bridge sha
`2975e13db300366f143ec3a32933d73c2bd3e769075f5d5e69978e5da7455c50` and `librmw_mdds_cpp.so` sha
`3da10f0c8adc3973a1b4dae4752902b7c853fdd1422ae1569f0d02187e83d904`. `publisher.get_subscription_count()`
is not authoritative for the cross-board bridge path because successful 1MiB deliveries also printed
`subscriptions=0`. Persisted same-board 1.5MiB logs show `received=1 valid=1`, while persisted cross-board
single-sample 1.5MiB logs show `received=0 valid=0`; the blocker was therefore documented at that time as
cross-board route, fragment delivery, admission, queueing, or wait/take behavior, not a proven local
payload-size hard limit. This is superseded for the current coverage2 topic large gate by the later harness
correction and domain-260 PASS evidence below.

Late 2026-07-06 refresh: a fresh large-only RK3588A rerun on domain 193 again passed 512KiB and 1MiB but
failed 1.5MiB: `RESULT|cov2_large512k|PASS|received=1|valid_len=1`,
`RESULT|cov2_large1m|PASS|received=1|valid_len=1`,
`RESULT|cov2_large1500k|FAIL|received=0|valid_len=0`, and `COVERAGE2_SUMMARY|pass=2|fail=1`. Current board
logs for that run show `COV_BIGSUB_DONE size=524288 received=1 valid=1`,
`COV_BIGSUB_DONE size=1048576 received=1 valid=1`, and
`COV_BIGSUB_DONE size=1572864 received=0 valid=0`. A new DSoftBus pending-queue RED test,
`MddsConnManagerTest.PendingQueueAcceptsLargeStartupBurst_040kc`, builds in `MddsDSoftBusBackendTest` but
fails on RK3588A with `actual: 2048 vs 64` and `BOARD_EXIT=1`, so that checkpoint recorded the
pending-capacity/admission path as an unfixed blocker candidate.

Final 2026-07-06 pending-queue refresh: the pending-capacity/admission unit blocker above is now GREEN at the
DSoftBus unit level. `MDDS_CONN_MAX_PENDING` now scales as `MDDS_MAX_FRAGMENTS_PER_MSG * 8`, large pending
drain buffers in `SweepRetryConnections` and `MddsConnGetOrCreate` are heap allocated, and the mock socket
send-capture capacity follows `MDDS_CONN_MAX_PENDING`. The RK3588A focused `MddsDSoftBusBackendTest` group
`MddsConnManagerTest.PendingQueueAcceptsLargeStartupBurst_040kc:
MddsConnManagerTest.PendingQueueFullAndFlushOnBind_040k:
MddsConnManagerTest.PendingQueueFullReturnsError_058:
MddsConnManagerTest.ConnManagerMemoryEstimate*` passed `6/6` with `BOARD_EXIT=0`.
After rebuilding `mdds_bridge_shared`, `deploy_rmw_mdds_delta.sh` installed bridge sha
`f1d24c5431b4e01472603b298e1dbcff09302da3a3d42a876dcd0b8a8e257127` on both RK3588A boards. The follow-up
cross-board large-only run on domain 194 still passed 512KiB and 1MiB but failed 1.5MiB:
`RESULT|cov2_large512k|PASS|received=1|valid_len=1`,
`RESULT|cov2_large1m|PASS|received=1|valid_len=1`,
`RESULT|cov2_large1500k|FAIL|received=0|valid_len=0`, and `COVERAGE2_SUMMARY|pass=2|fail=1`.
This closes the pending-queue unit blocker. The domain-194 large result is retained as a historical checkpoint
because the coverage2 harness was later found to be under-validating large subscriber completion and using a
host HDC cap that could be shorter than the board-side subscriber timeout.

Final 2026-07-06 coverage2 large refresh: `ohos/tools/run_cross_board_rmw_mdds_coverage2.sh` now starts the
large subscriber with `min_valid=times`, requires both `received >= times` and `valid_len >= times`, and uses
configurable `RMW_MDDS_COVERAGE2_HDC_TIMEOUT`. The current same-domain RK3588A run
`RMW_MDDS_COVERAGE2_ONLY=large RMW_MDDS_COVERAGE2_LARGE_DOMAIN_STRIDE=0
RMW_MDDS_COVERAGE2_BIG_SUB_TIMEOUT=240 RMW_MDDS_COVERAGE2_HDC_TIMEOUT=300
ohos/tools/run_cross_board_rmw_mdds_coverage2.sh <board-a> <board-b> 260` emitted
`RESULT|cov2_large512k|PASS|received=12|valid_len=12`,
`RESULT|cov2_large1m|PASS|received=10|valid_len=10`,
`RESULT|cov2_large1500k|PASS|received=8|valid_len=8`, and `COVERAGE2_SUMMARY|pass=3|fail=0`. Latest
board-side topic logs under `/data/local/tmp/coverage2` re-read as `COV_BIGSUB_DONE size=524288 received=12 valid=12`,
`COV_BIGSUB_DONE size=1048576 received=10 valid=10`, and
`COV_BIGSUB_DONE size=1572864 received=8 valid=8`. This closes the current topic large-payload coverage2 gate
through 1.5MiB.

Service large-payload follow-up on 2026-07-06: `ohos/tools/run_cross_board_rmw_mdds_coverage2.sh` now supports
`RMW_MDDS_COVERAGE2_ONLY=service_large`, using `rcl_interfaces/srv/SetParameters` to carry a large request
string and a large response string. The RK3588A domain-270 run emitted
`RESULT|cov2_service_large512k|PASS|sent=3|server_req=3|server_valid=3|client_ok=3|response_valid=3`,
`RESULT|cov2_service_large1m|PASS|sent=2|server_req=2|server_valid=2|client_ok=2|response_valid=2`,
`RESULT|cov2_service_large1500k|PASS|sent=1|server_req=1|server_valid=1|client_ok=1|response_valid=1`, and
`COVERAGE2_SUMMARY|pass=3|fail=0`. Re-read board logs show matching client and server completion counters.
This closes the current service large-payload coverage2 gate through 1.5MiB, but not the remaining
production/full-feature gates such as long soak, larger payload boundaries, rosbag2
service/action, broader board security matrices, ASAN/TSAN, and performance baseline.

Latest 4MiB boundary refresh on 2026-07-06: the coverage2 harness supports extra large-payload cases via
`RMW_MDDS_COVERAGE2_LARGE_EXTRA_CASES` and `RMW_MDDS_COVERAGE2_SERVICE_EXTRA_CASES` plus skip-default switches.
Extra-case sizes are generated body lengths; the helper adds validation prefix/suffix bytes. After updating the
DSoftBus large-payload implementation, the focused OHOS build for
`MddsDSoftBusBackendTest`, `MddsDefragmenterTest`, `MddsPubSubTest`, and `MddsMessageFrameTest` succeeded, and
the RK3588A focused runs passed `ServiceSendFragmentsLargePayload_012e`,
`WirePayloadAllowsUserPayloadPlusTransportOverhead_013`, `ReliablePayloadAcceptsFullUserPayload_206`, and
`DecodeRejectsOversizedPayload_001`, each with `BOARD_EXIT=0`. The refreshed bridge was deployed to both boards
with sha `8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc`.

The isolated RK3588A exact-total 4MiB topic run for `large4m_exact:4194254:1` on domain 275 now passes with
`RESULT|cov2_large4m_exact|PASS|received=1|valid_len=1`. The previous service body probe
`service_large4m_exact:4194250:1` on domain 276 failed with
`RESULT|cov2_service_large4m_exact|FAIL|sent=1|server_req=0|server_valid=0|client_ok=0|response_valid=0`, but
board-side serialization proved that body size produces request wire `4194416`, which is above the 4MiB
rmw_mdds payload cap used by that checkpoint. The corrected service probe `service_large4m_wire:4194139:1`
produces request wire `4194304` and response wire `4194250`; the RK3588A cross-board run on domain 277 passes with
`RESULT|cov2_service_large4m_wire|PASS|sent=1|server_req=1|server_valid=1|client_ok=1|response_valid=1` and
`COVERAGE2_SUMMARY|pass=1|fail=0`. Therefore topic 4MiB and corrected service 4MiB wire are closed for this
focused cross-board gate. The early 2026-07-07 16MiB topic probes failed before the MDDS bridge/fragment refresh:
`large16m:16777216:1` and `large16m_total:16777164:1` hit broker encode-frame failure, while
`large16m_ipc_budget:16777143:1` let the publisher print `COV_BIGPUB_DONE` but left the subscriber at
`received=0 valid=0`. Those failures are retained as historical root-cause evidence for broker IPC envelope and
downstream MDDS/DSoftBus delivery gaps. The later MDDS refresh raised the public payload budget to 16MiB, kept a
512-byte frame-payload allowance, expanded the defragmenter receive mask, rebuilt/deployed bridge
`037553404f7a9d9a37ad69343f0f8a013ec66230d0da53af1c177a3ad55c18ef`, and made
`large16m_total_after_mddsfix:16777164:1` pass cross-board with `received=1 valid_len=1`; that generated
`String.data` is exactly 16MiB. The `large16m_full_user_after_mddsfix:16777216:1` helper case still fails because
it adds prefix/suffix bytes on top of a 16MiB body and therefore exceeds the exact 16MiB topic string boundary.

Latest 4MiB source review on 2026-07-06: current DSoftBus/MDDS source now has
`MDDS_RELIABLE_MAX_PAYLOAD_SIZE=MDDS_MAX_PAYLOAD_SIZE`, `DSoftBusSendService()` fragments payloads larger than
`MDDS_DEFAULT_FRAGMENT_SIZE`, and the frame/defrag path uses `MDDS_MAX_FRAME_PAYLOAD_SIZE` while preserving the
final user-payload cap. The corrected service exact-total 4MiB wire case is now green; the old `4194250` body
case should be retained only as an oversized negative probe. A later MDDS 16MiB bridge/fragment refresh adds one
cross-board PASS for exact-total 16MiB `String.data`, but the broad parity goal remains open because service 16MiB,
repeated/concurrent large-message gates, 2h service/graph soak, dedicated action bag/action introspection CLI
support, sanitizer, performance, and oversized/error-propagation gates are still missing. The 2026-07-07 RK3588A rosbag2 refresh covers
`/add_two_ints` service event record/info, service event playback, service request playback, and Fibonacci
action feedback/status hidden-topic record/playback; it does not prove a dedicated `--action`/`ros2 action echo`
CLI surface because this board image does not expose those commands.

Additional DSoftBus unit verification after the defragmenter fix: the full RK3588A `MddsDefragmenterTest`
suite passed `13/13` with `BOARD_EXIT=0`; the
`MddsDSoftBusBackendTest.MaxSendSizeForcesFragmentationBelowDSoftBusLimit_001b` focused rerun also passed
with `BOARD_EXIT=0`.

Current host verification evidence on 2026-07-03: with
`PYTHONPATH=/home/kaihong/ros2/install/lib/python3.12/site-packages` set for ament's Python wrappers and
`LD_LIBRARY_PATH=/home/kaihong/ros2/build/rmw_mdds_cpp:/home/kaihong/ros2/install/lib`, the full
`rmw_mdds_cpp` build succeeds, `ctest --test-dir build/rmw_mdds_cpp --output-on-failure` passes 23/23 after
adding `test_loan_arena`, and
`ohos/test_rmw_mdds_delivery_contracts.sh` emits `rmw_mdds_delivery_contracts_ok`. The affected full-parity
contract still exits nonzero by design with `RESULT|rmw_mdds_full_parity_loaned_shapes|RED|status=1`, while
`RESULT|rmw_mdds_full_parity_signed_security|PASS` and
`RESULT|rmw_mdds_full_parity_broker_network_flow|PASS` are green. `cmake --install build/rmw_mdds_cpp`
refreshed `install/lib/librmw_mdds_cpp.so`, and `ohos/test_rmw_mdds_artifact_contracts.sh` emits
`rmw_mdds_artifact_contracts_ok`.

This checkpoint was superseded later on 2026-07-03 by the sequence/nested dynamic-loan evidence below, where
`ohos/test_rmw_mdds_full_parity_red_contracts.sh` exits 0 and emits
`RESULT|rmw_mdds_full_parity_loaned_shapes|PASS`.

Additional host verification on 2026-07-03 after bridge protected-lane activation: full
`cmake --build build/rmw_mdds_cpp -j4` passes, `ctest --test-dir build/rmw_mdds_cpp --output-on-failure`
passes 23/23 when run with the required `PYTHONPATH` and `LD_LIBRARY_PATH`, and
`ohos/test_rmw_mdds_delivery_contracts.sh` ends with `rmw_mdds_delivery_contracts_ok`. The affected full-parity
contract remains intentionally nonzero: dynamic loaned shapes emit
`RESULT|rmw_mdds_full_parity_loaned_shapes|RED|status=1`, while signed security now passes all six
`DISABLED_FullParitySros2*` tests including bridge activation and emits
`RESULT|rmw_mdds_full_parity_signed_security|PASS`.

This checkpoint was superseded later on 2026-07-03 by the sequence/nested dynamic-loan evidence below, where
the same full-parity host contract exits 0.

Additional host verification on 2026-07-03 after unbounded-string dynamic loan support: full
`cmake --build build/rmw_mdds_cpp -j4` passes, `ctest --test-dir build/rmw_mdds_cpp --output-on-failure`
passes 23/23 when run with the required `PYTHONPATH` and `LD_LIBRARY_PATH`, and
`ohos/test_rmw_mdds_delivery_contracts.sh` ends with `rmw_mdds_delivery_contracts_ok`. `cmake --install
build/rmw_mdds_cpp` refreshed the install tree and `ohos/test_rmw_mdds_artifact_contracts.sh` emits
`rmw_mdds_artifact_contracts_ok`; installed `librmw_mdds_cpp.so` sha is
`11121661e648048d51c387db2bebe9ef7b0af96c7d16a74df16565e341fe100e`. The affected full-parity contract still
exits nonzero by design, but now only sequence and nested dynamic loans remain in the loaned-shapes RED group:
the unbounded-string full-parity test passes, `RESULT|rmw_mdds_full_parity_signed_security|PASS`, and
`RESULT|rmw_mdds_full_parity_broker_network_flow|PASS`.

This checkpoint was superseded later on 2026-07-03 by the sequence/nested dynamic-loan evidence below, where
the full loaned-shapes group passes.

Host verification on 2026-07-03 after sequence/nested dynamic loan support: `cmake --build
build/rmw_mdds_cpp -j4` passed, `ctest --test-dir build/rmw_mdds_cpp --output-on-failure` passed 23/23 with
`PYTHONPATH=/home/kaihong/ros2/install/lib/python3.12/site-packages` and
`LD_LIBRARY_PATH=/home/kaihong/ros2/build/rmw_mdds_cpp:/home/kaihong/ros2/install/lib`, and
`ohos/test_rmw_mdds_full_parity_red_contracts.sh` emitted
`RESULT|rmw_mdds_full_parity_loaned_shapes|PASS`,
`RESULT|rmw_mdds_full_parity_signed_security|PASS`,
`RESULT|rmw_mdds_full_parity_broker_network_flow|PASS`, and
`rmw_mdds_full_parity_red_contracts_ok`. After `cmake --install build/rmw_mdds_cpp`,
`ohos/test_rmw_mdds_delivery_contracts.sh` emitted `rmw_mdds_delivery_contracts_ok` and
`ohos/test_rmw_mdds_artifact_contracts.sh` emitted `rmw_mdds_artifact_contracts_ok`. Host library hashes:
installed `librmw_mdds_cpp.so` =
`c5301736ab3e1aca211db77dfabb2eca01c82a1ec512d8cbc2173fe663899382`; build-tree
`librmw_mdds_cpp.so` = `2ad6268ab61a24df51a5e5c136e30abd25cd46440889aa9116381cd660a91750`.
Final board verification on 2026-07-04: `./ohos/colcon_rk3588a.sh rmw_mdds_cpp` rebuilt the OHOS overlay
with target OpenSSL enabled; `llvm-readelf -d install/ohos-colcon-rk3588a/lib/librmw_mdds_cpp.so` shows
`NEEDED libcrypto_openssl.z.so`. `HDC_BIN=hdc ohos/tools/deploy_rmw_mdds_delta.sh
<rk3588a-board-a> <rk3588a-board-b>` emitted deploy PASS markers on both
boards with `librmw_mdds_cpp.so` sha `b08e129c4086b3593cf1271da6a2e2acc853a7c6b98e2f6f265995d081dbf1b1`,
broker sha `83b93da71c72133d89017c52cd95e5dc06c2d661626123bb94553eaa3d8be970`, and bridge sha
`771cec930859b407dc7c48aa94a0fb4b342478bb6403abaf72974fbb2d86e9c1`.

Board lane evidence on 2026-07-04: after restarting `softbus_server` on both boards to refresh LNN binding,
`HDC_BIN=hdc ohos/tools/run_cross_board_rmw_mdds_m2m.sh
<rk3588a-board-a> <rk3588a-board-b> 102` emitted
`RESULT|m2m_pubsub_std_msgs_string|PASS|received=40/40 exact(RELIABLE zero-loss)`,
`RESULT|m2m_service_add_two_ints|PASS|sum=42`,
`RESULT|m2m_action_fibonacci|PASS|status=SUCCEEDED;sequence=0,1,1,2,3,5;feedback_msgs=4`, and
`cross_board_rmw_mdds_m2m_ok`. Gateway verification passed `run_cross_board_rmw_mdds_matrix.sh ... 103` with
`MATRIX_SUMMARY pass=8 fail=0`, `run_cross_board_rmw_mdds_service_gw.sh ... 104` with
`RESULT|service_addints_mdds_client_to_fastrtps_server|PASS|sum=42`, `run_cross_board_rmw_mdds_action_gw.sh
... 105` with `RESULT|action_fibonacci_mdds_client_to_fastrtps_server|PASS|sequence=0,1,1,2,3,5,8,13`,
`run_cross_board_rmw_mdds_params_gw.sh ... 106` with
`RESULT|params_cli_mdds_client_to_fastrtps_node|PASS|4242`, and
`run_cross_board_rmw_mdds_lifecycle_gw.sh ... 107` with
`RESULT|lifecycle_mdds_client_to_fastrtps_node|PASS`. The protected SROS2 board harness markers recorded
under task 3.4 are retained as historical output only; after the 2026-07-06 bridge-source correction they are
not accepted as proof of current authenticated/encrypted MDDS/DSoftBus transport.

Final host refresh on 2026-07-04: with
`PYTHONPATH=/home/kaihong/ros2/install/lib/python3.12/site-packages` and
`LD_LIBRARY_PATH=/home/kaihong/ros2/build/rmw_mdds_cpp:/home/kaihong/ros2/install/lib`,
`cmake --build build/rmw_mdds_cpp -j4` passed, `ctest --test-dir build/rmw_mdds_cpp --output-on-failure`
passed 23/23, `ohos/test_rmw_mdds_full_parity_red_contracts.sh` emitted PASS for loaned shapes, signed
security, and broker network-flow, `ohos/test_rmw_mdds_delivery_contracts.sh` emitted
`rmw_mdds_delivery_contracts_ok`, and `ohos/test_rmw_mdds_artifact_contracts.sh` emitted
`rmw_mdds_artifact_contracts_ok`.

Late status sync on 2026-07-07: later service-stress, protected SROS2, graph-churn, rosbag2, and large-payload
evidence is tracked in `complete-rmw-mdds-full-parity-acceptance`. The IPC-focused refresh added a max
user-payload plus `SampleMessage` envelope regression, passed the focused IPC/broker CTest set 5/5, rebuilt and
deployed `librmw_mdds_cpp.so=e347fe583811c9e3082476b804bae5a638f553756b1e27ef5d2e9e75225413b6` and
`rmw_mdds_broker=fb70c341e896fcf45a7341b9732edc1bdb4f7ffc6cc1a8051fd1c16c5cc6abab` to both RK3588A boards,
and initially left bridge/probe/softbus hashes unchanged. At that intermediate checkpoint, after-fix 16MiB probes
let the publisher complete, but the subscriber still reported `received=0 valid=0`.

Current status-doc sync on 2026-07-07: re-ran the lightweight validation commands before this edit
(`bridge_protected_transport_contract_ok`, `rmw_mdds_script_contracts_ok`, and OpenSpec `4 passed, 0 failed`).
Both RK3588A targets are still visible through `hdc list targets`, with the known host-side 139 after valid
output. A later MDDS bridge/fragment refresh rebuilt and deployed bridge
`037553404f7a9d9a37ad69343f0f8a013ec66230d0da53af1c177a3ad55c18ef` on top of the current
`e347fe.../fb70c341...` rmw/broker deployment. Focused OHOS build and RK3588A unit gates passed, and the
cross-board exact-total 16MiB topic probe `large16m_total_after_mddsfix:16777164:1` passed with
`RESULT|cov2_large16m_total_after_mddsfix|PASS|received=1|valid_len=1`. This closes only the single-sample
exact-total 16MiB topic evidence point. The broad perfect/full-parity goal remains open for service 16MiB,
repeated/concurrent large messages, current-overlay service-stress reruns if required, 2h soak, broader security
matrix, sanitizer, oversized/error-propagation, and performance gates.
