## 1. Full Parity Audit

- [x] 1.1 Build a current `rmw_mdds_cpp` parity matrix covering init, context, nodes, wait sets, guard conditions, graph, pub/sub, services, clients, actions, serialized APIs, dynamic APIs, loaned messages, QoS compatibility, QoS events, content filters, type identity, security, network-flow metadata, logging, host delivery, board delivery, and upstream handoff.
- [x] 1.2 For every matrix row, record the current evidence source: code path, unit test, contract script, upstream conformance test, board harness, OpenSpec task, or missing evidence.
- [x] 1.3 Classify every row as proven, accepted unsupported, or incomplete; rows cannot remain ambiguous.
- [x] 1.4 Identify the minimum RED test or executable contract needed for every incomplete row.

Audit artifact: `parity-matrix.md`.

## 2. RED Gates For Intended Support

- [x] 2.1 Add failing contracts for generalized loaned-message shapes that are intended to become supported beyond the current fixed-size raw loan path.
- [x] 2.2 Add failing contracts for any security behavior intended beyond current local SROS2 policy enforcement, including signed artifacts or transport protection if required.
- [x] 2.3 Add failing contracts for any remaining ROS 2 RMW API surface that the parity matrix marks incomplete but intended to become supported.
- [x] 2.4 Preserve explicit unsupported tests for rows that are accepted as out of scope.

Initial RED contract evidence: `ohos/test_rmw_mdds_full_parity_red_contracts.sh` exited nonzero by design on 2026-07-03 and emitted:
`RESULT|rmw_mdds_full_parity_loaned_shapes|RED|status=1`,
`RESULT|rmw_mdds_full_parity_signed_security|RED|status=1`, and
`RESULT|rmw_mdds_full_parity_broker_network_flow|RED|status=1`.

Unsupported-boundary evidence on 2026-07-03: no unsupported row is accepted as out of scope for final
completion yet. Unsupported behavior remains explicit where required:
`test_bridge_loaned_rmw` passes
`RmwMddsBridgeLoanedRmw.PublisherDoesNotAdvertiseLoaningWithoutBridge`, while the generalized host dynamic
loaned-shape row has since moved from RED to PASS through task 3.1.

## 3. Implementation For Missing Supported Rows

- [x] 3.1 Implement the smallest behavior change needed to turn the generalized loaned-message RED gates green.
- [x] 3.2 Implement the smallest behavior change needed to turn the security-parity RED gates green, or document the external MDDS/DSoftBus dependency blocking implementation.
- [x] 3.3 Implement any remaining supported RMW API rows identified by the parity matrix.
- [x] 3.4 Re-run focused unit tests after each behavior change and keep unsupported rows explicit.

Security GREEN evidence: after the local XML/protected-governance split and the later signed protected
policy implementation, `test_pubsub_inproc` passes the full `RmwMddsPubSub.DISABLED_FullParitySros2*`
set. `ohos/test_rmw_mdds_sros2_policy_contracts.sh` still emits
`RESULT|rmw_mdds_sros2_policy_contracts|PASS`, and `ohos/test_rmw_mdds_full_parity_red_contracts.sh` now
emits `RESULT|rmw_mdds_full_parity_signed_security|PASS`. This is host signed-artifact and fail-closed
protected-policy evidence. Board-visible protected signed SROS2 traffic is now current for the approved harness:
after the DSoftBus channel-type RED/GREEN fix, the latest domain-315 board run on the current
`e347fe.../fb70c341...` rmw/broker plus `d314e6cd...` bridge passed signed policy on both boards,
protected transport activation on both boards, authorized protected pub/sub with `received=60`, unauthorized
publish denial, and final `cross_board_rmw_mdds_sros2_protected_ok`.

Broker network-flow GREEN evidence: `test_broker_mode` passes
`RmwMddsBrokerMode.DISABLED_FullParityBrokerModeNetworkFlowEndpointsReportMddsTransport`, and
`ohos/test_rmw_mdds_full_parity_red_contracts.sh` now emits
`RESULT|rmw_mdds_full_parity_broker_network_flow|PASS`.

Loaned-shape GREEN evidence on 2026-07-03: `MddsLoanArena` provides aligned bridge-loan segments and a
guarded active-loan allocation hook, `rmw_borrow_loaned_message()` now advertises/returns bridge-backed
dynamic publisher loans for the covered unbounded string, dynamic sequence, and nested dynamic C++ message
shapes, and `rmw_publish_loaned_message()` publishes through the same bridge loan after
`MessageAdapter::PrepareLoanedDynamicMddsPayload()` verifies dynamic storage remains inside the loan. Focused
verification passed `./build/rmw_mdds_cpp/test_loan_arena --gtest_color=no` 2/2, normal
`./build/rmw_mdds_cpp/test_bridge_loaned_rmw --gtest_color=no` 9/9, and
`./build/rmw_mdds_cpp/test_bridge_loaned_rmw --gtest_filter=RmwMddsBridgeLoanedRmw.DISABLED_FullParityLoaned* --gtest_also_run_disabled_tests --gtest_color=no`
3/3. `ohos/test_rmw_mdds_zero_copy_contracts.sh` emitted `rmw_mdds_zero_copy_contracts_ok`.

Focused verification evidence on 2026-07-03: after dynamic sequence/nested support, `ohos/test_rmw_mdds_full_parity_red_contracts.sh`
exits 0 and emits `RESULT|rmw_mdds_full_parity_loaned_shapes|PASS`,
`RESULT|rmw_mdds_full_parity_signed_security|PASS`,
`RESULT|rmw_mdds_full_parity_broker_network_flow|PASS`, and
`rmw_mdds_full_parity_red_contracts_ok`. `ohos/test_rmw_mdds_sros2_policy_contracts.sh` also emits
`RESULT|rmw_mdds_sros2_policy_contracts|PASS`. Future loaned-message expansion beyond the covered host
shapes should add another RED contract before production behavior changes.

## 4. Runtime And Delivery Evidence

- [x] 4.1 Run `ohos/test_rmw_mdds_delivery_contracts.sh` and confirm markers cover all affected host surfaces.
- [x] 4.2 Rebuild the OHOS `rmw_mdds_cpp` overlay after parity changes.
- [x] 4.3 Deploy the refreshed runtime delta to both RK3588/KaihongOS boards.
- [ ] 4.4 Run affected native MDDS, cross-RMW gateway, zero-copy, and protected-security board lanes with explicit PASS markers that prove current bridge activation.
- [x] 4.5 Decide and execute the delivery endpoint: local handoff only, OpenSpec archive, push, PR, or Gerrit submission.

Fresh host delivery evidence on 2026-07-03: after the security, broker network-flow, dynamic-loaned-message,
and host CLI harness robustness changes, `ohos/test_rmw_mdds_delivery_contracts.sh` emits
`rmw_mdds_delivery_contracts_ok`, including package CTest `23/23`, upstream `test_rmw_implementation`
`16/16`, SROS2 local policy, zero-copy, artifact, type-description, and host CLI
pub/sub/service/action/params/lifecycle/graph/QoS/transient-local/message-info PASS markers. During an earlier
rerun, the params/lifecycle probes exposed daemon/discovery timing misses; the harness now uses daemon-free
discovery and state polling for those probes, and the final full delivery rerun passed.

Fresh host delivery evidence on 2026-07-06: after fixing broker service availability to use a targeted graph
refresh, restoring graph API bad-argument fail-fast behavior, and hardening host CLI probes with per-run broker
sockets plus publisher match waits, `bash ohos/test_rmw_mdds_delivery_contracts.sh` emitted
`rmw_mdds_delivery_contracts_ok`. The run included package CTest `23/23`, `test_rmw_implementation`
rmw_mdds subset `16/16`, type-description PASS, and host CLI pub/sub/service/action/params/lifecycle/graph/QoS/
transient-local/message-info PASS markers. A separate full
`source install/setup.bash && ctest --test-dir build/test_rmw_implementation --output-on-failure` run passed `69/70`; the only failure was
the missing host `cppcheck` executable.

Latest host-only delivery refresh on 2026-07-06: after adding a malformed graph endpoint-list count guard in
`ipc_protocol` and refreshing the broker graph before broker-mode matched-event current-count calculations,
`source install/setup.bash && LD_LIBRARY_PATH=$PWD/build/rmw_mdds_cpp:$PWD/install/lib:$LD_LIBRARY_PATH
RMW_IMPLEMENTATION=rmw_mdds_cpp ctest --test-dir build/test_rmw_implementation -R 'test_event__rmw_mdds_cpp$'
--output-on-failure` passed 1/1. A follow-up `bash ohos/test_rmw_mdds_delivery_contracts.sh` emitted
`rmw_mdds_delivery_contracts_ok`, with package CTest `23/23`, `test_rmw_implementation` rmw_mdds subset
`16/16`, type-description PASS, and host CLI pub/sub/service/action/params/lifecycle/graph/QoS/transient-local/
message-info PASS markers. This is host build-tree evidence only; no RK3588A redeploy/rerun was performed for
these two latest host fixes in this status-sync pass.

Protected-security board refresh on 2026-07-06: the DSoftBus bridge was rebuilt and deployed with
`libmdds_bridge_shared.z.so` sha `330a61f59bf3b6e1dbe4198562b9081b0474701b61cab08b33d1631cb5f99698`,
`libsoftbus_client.z.so` sha `e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`,
`librmw_mdds_cpp.so` sha `f7209cf4ec01e3a8eda6a882db4faea8e9f9cc820b01660adb37d2f2f25ffcd7`,
and broker sha `65364cee43c4b3078803c14da8b320eb6176f58876fbbb7d5459dcaf1ee4fb4d` on both RK3588A boards.
`HDC_BIN=hdc RMW_MDDS_HDC_TIMEOUT_SECONDS=180 RMW_MDDS_SROS2_AUTH_WARMUP_SECONDS=30
RMW_MDDS_SROS2_AUTH_WAIT_SECONDS=30 ohos/tools/run_cross_board_rmw_mdds_sros2_protected.sh
<rk3588a-subscriber-board> <rk3588a-publisher-board> 222` emitted signed-policy PASS on both boards,
protected-transport activation PASS on both boards, `RESULT|board_sros2_protected_authorized_pubsub|PASS|...|received=58`,
`RESULT|board_sros2_protected_unauthorized_publish|PASS|...|denied`, and
`cross_board_rmw_mdds_sros2_protected_ok`. Task 4.4 remains open only because it also names native MDDS,
cross-RMW gateway, and zero-copy board lanes as a broader bundle; this refresh closes the protected-security lane.

Signed-security host refresh on 2026-07-03: after OpenSSL-backed signed protected SROS2 artifact validation
landed, the full `rmw_mdds_cpp` build succeeded with the workspace Python path exported,
`ctest --test-dir build/rmw_mdds_cpp --output-on-failure` passed 22/22 with
`LD_LIBRARY_PATH=${ROS2_WS}/build/rmw_mdds_cpp:${ROS2_WS}/install/lib`, and
`ohos/test_rmw_mdds_delivery_contracts.sh` again emitted `rmw_mdds_delivery_contracts_ok`. The refreshed
full-parity host contract emits `RESULT|rmw_mdds_full_parity_signed_security|PASS` and remains nonzero only
because `RESULT|rmw_mdds_full_parity_loaned_shapes|RED|status=1` is still open. This was superseded later on
2026-07-03 by the dynamic sequence/nested loan support evidence above, where the full-parity host contract
exits 0 and emits `RESULT|rmw_mdds_full_parity_loaned_shapes|PASS`.

OHOS overlay rebuild evidence on 2026-07-03: `./ohos/colcon_rk3588a.sh rmw_mdds_cpp` rebuilt and installed
the target package into `install/ohos-colcon-rk3588a`, and `ohos/test_rmw_mdds_artifact_contracts.sh`
emitted `rmw_mdds_artifact_contracts_ok`. Rebuilt artifact checksums:
`librmw_mdds_cpp.so` sha `45b901717a47ce9b7d6124131df3f6a10f952b86581160a5f5e7375de1b9bdbb`;
`rmw_mdds_broker` sha `85f5c7afc3c51f4bd98cad1c2e8f1edd3df968e6124b3c6e2435feac3680670f`.

Host install refresh on 2026-07-03: `cmake --install build/rmw_mdds_cpp` refreshed
`install/lib/librmw_mdds_cpp.so`, `ohos/test_rmw_mdds_artifact_contracts.sh` emitted
`rmw_mdds_artifact_contracts_ok`, and the installed host `librmw_mdds_cpp.so` sha is
`f2c8f6708594bed141aa6d185f4f07c7723d2d4093e1cca5e5af426acadae287`.

Host install refresh after dynamic sequence/nested loan support on 2026-07-03: `cmake --install
build/rmw_mdds_cpp` refreshed `install/lib/librmw_mdds_cpp.so`, `ohos/test_rmw_mdds_artifact_contracts.sh`
emitted `rmw_mdds_artifact_contracts_ok`, installed host `librmw_mdds_cpp.so` sha is
`c5301736ab3e1aca211db77dfabb2eca01c82a1ec512d8cbc2173fe663899382`, and the build-tree
`librmw_mdds_cpp.so` sha is `2ad6268ab61a24df51a5e5c136e30abd25cd46440889aa9116381cd660a91750`.

Board deploy evidence on 2026-07-03: `ohos/tools/deploy_rmw_mdds_delta.sh
<rk3588a-board-a> <rk3588a-board-b>` emitted
`RESULT|rmw_mdds_deploy|PASS` for both boards with `librmw_mdds_cpp.so` sha
`45b901717a47ce9b7d6124131df3f6a10f952b86581160a5f5e7375de1b9bdbb` and broker sha
`85f5c7afc3c51f4bd98cad1c2e8f1edd3df968e6124b3c6e2435feac3680670f`; it also emitted
`RESULT|mdds_bridge_deploy|PASS` for both boards with bridge sha
`82e49a4c8e01df25a3980376ff5c24290bf4cded1c08a59c36d047c547e9e452`.

Board runtime evidence on 2026-07-03: `ohos/tools/run_cross_board_rmw_mdds_m2m.sh
<rk3588a-board-a> <rk3588a-board-b>` emitted
`M2M_SUMMARY pass=3 fail=0` and `cross_board_rmw_mdds_m2m_ok` for native MDDS pub/sub,
service, and Fibonacci action. `ohos/tools/run_cross_board_rmw_mdds_matrix.sh
<rk3588a-board-a> <rk3588a-board-b>` emitted
`MATRIX_SUMMARY pass=8 fail=0` and `cross_board_rmw_mdds_matrix_ok` for cross-RMW gateway
String, PoseStamped, best-effort QoS, and TFMessage lanes in both directions. The zero-copy
gate remains the host/static `RESULT|rmw_mdds_zero_copy_contracts|PASS` from the delivery
contract; board runtime preservation after the zero-copy/security changes is covered by the
native and gateway board lanes above.

Board SROS2 policy evidence on 2026-07-03: the board harness was aligned with the supported local
XML topic-policy scope (`NONE` protection kinds). It first failed against the old protected
`ENCRYPT` governance, which is now intentionally rejected by `rmw_mdds_cpp`; after the harness fix,
`ohos/tools/run_cross_board_rmw_mdds_sros2_policy.sh
<rk3588a-board-b> <rk3588a-board-a>` emitted
`RESULT|board_sros2_authorized_pubsub|PASS|topic=/mdds_sros2_allowed|received=20`,
`RESULT|board_sros2_unauthorized_publish|PASS|topic=/mdds_sros2_forbidden|denied`, and
`cross_board_rmw_mdds_sros2_policy_ok`. Host signed/protected SROS2 policy behavior is now covered by the
full-parity host contract.

Final board runtime evidence on 2026-07-04 before the 2026-07-06 correction: `./ohos/colcon_rk3588a.sh rmw_mdds_cpp` rebuilt the OHOS
overlay with target OpenSSL enabled, `llvm-readelf` showed `NEEDED libcrypto_openssl.z.so`, and
`ohos/tools/deploy_rmw_mdds_delta.sh <rk3588a-board-a>
<rk3588a-board-b>` deployed `librmw_mdds_cpp.so` sha
`b08e129c4086b3593cf1271da6a2e2acc853a7c6b98e2f6f265995d081dbf1b1`, broker sha
`83b93da71c72133d89017c52cd95e5dc06c2d661626123bb94553eaa3d8be970`, and bridge sha
`771cec930859b407dc7c48aa94a0fb4b342478bb6403abaf72974fbb2d86e9c1` to both boards. After restarting
`softbus_server` to refresh LNN binding, native M2M emitted `M2M_SUMMARY pass=3 fail=0` and
`cross_board_rmw_mdds_m2m_ok`; the gateway matrix emitted `MATRIX_SUMMARY pass=8 fail=0` and
`cross_board_rmw_mdds_matrix_ok`; gateway service/action/params/lifecycle emitted PASS markers for
`sum=42`, Fibonacci `sequence=0,1,1,2,3,5,8,13`, parameter value `4242`, and lifecycle transition to
inactive. The protected SROS2 board harness reported signed-policy PASS and
`RESULT|board_sros2_protected_transport|PASS|authenticated=1|encrypted=1` on both boards, authorized
protected pub/sub PASS with `received=20`, unauthorized protected publish PASS with `denied`, and
`cross_board_rmw_mdds_sros2_protected_ok`.

Post-checkpoint correction on 2026-07-06: the current DSoftBus/MDDS HEAD is
`30d53a2c2 <test><29168><补充rmw_mdds service压力门禁><source:int;none>`; the worktree additionally contains
DSoftBus `maxSendSize`, defragmenter, pending-queue, and protected-transport gate unit-test/fix work.
The 2026-07-04 protected transport board marker must not be treated as current proof of an enforcing
authenticated/encrypted MDDS/DSoftBus lane because it predates the current bridge/probe correction.
The current truthful security state is: signed-artifact validation and fail-closed policy behavior have host
evidence; the DSoftBus bridge now calls `MddsConnManagerActivateProtectedTransport()` and has source/contract
plus focused RK3588A unit and full backend evidence; the current bridge/probe are rebuilt and deployed, and the
domain-315 protected board harness now verifies signed policy, bridge activation, authorized protected pub/sub
with `received=60`, and unauthorized publish denial on both boards. Broader security certification remains
incomplete only because the wider board matrix, soak, sanitizer, and performance evidence are still missing.

Current protected-transport recheck on 2026-07-06: the DSoftBus contract
`bash enhance/mdds/tests/scripts/test_bridge_protected_transport_contract.sh` emitted
`bridge_protected_transport_contract_ok`, and `bash ohos/test_rmw_mdds_script_contracts.sh` emitted
`rmw_mdds_script_contracts_ok`. `./build.sh --product-name khd_rk3588_a --ccache
--no-prebuilt-sdk -T MddsDSoftBusBackendTest` succeeded, and the RK3588A focused run
`./MddsDSoftBusBackendTest --gtest_filter="MddsConnManagerTest.ProtectedTransport*"
--gtest_output=xml:/data/local/tmp/MddsDSoftBusBackendTest_protected.xml` passed 7/7 with `BOARD_RC=0`,
including `ProtectedTransportUsesChannelTypeForEncryptionLookup`. The full `./MddsDSoftBusBackendTest` also
passed 140/140 with `BOARD_RC=0`. The protected probe is now deployed on both RK3588A boards with sha
`6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`; the latest protected harness path uses
bridge sha `8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc`, `libsoftbus_client.z.so` sha
`e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`, `librmw_mdds_cpp.so` sha
`ee2a285ca5484f1d90f4cf35e4272efa998ad0b32a8bed7153401d8a83e126a5`, and broker sha
`0ce6c5eb3bfe2e028df3103d3cb46499d6b2e909565fc600af34db4a4dc3427d` on both boards. Domain 221 first proved
the real bug: signed policy and activation passed, but authorized protected pub/sub returned `received=0`;
hilog and source review showed `ClientGetChannelIdAndTypeBySocketId()` returned `businessType` while
`GetEncryptByChannelId()` matches `channelType`. After switching `CheckSocketEncrypted()` to
`ClientGetChannelBySessionId()`, domain 290 protected harness output passed signed policy and protected
transport activation on both boards, authorized protected pub/sub with `received=58`, unauthorized publish
denial, and final `cross_board_rmw_mdds_sros2_protected_ok`. Task 4.4 remains open only because it also bundles
native MDDS, cross-RMW gateway, and zero-copy board lanes, not because the protected-security lane is still
blocked.

Current d314 protected-security rerun on 2026-07-07: after the late IPC and recv-queue refreshes, both RK3588A
boards were rechecked with `librmw_mdds_cpp.so=e347fe583811c9e3082476b804bae5a638f553756b1e27ef5d2e9e75225413b6`,
`rmw_mdds_broker=fb70c341e896fcf45a7341b9732edc1bdb4f7ffc6cc1a8051fd1c16c5cc6abab`,
`libmdds_bridge_shared.z.so=d314e6cd6d3937f37d9d5ea853225e9c581eafc3222145122860fbfc81c04a99`, and
protected probe sha `6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`. The command
`HDC_BIN=hdc RMW_MDDS_HDC_TIMEOUT_SECONDS=180 RMW_MDDS_HDC_RETRY_ATTEMPTS=5
RMW_MDDS_SROS2_AUTH_WARMUP_SECONDS=45 RMW_MDDS_SROS2_AUTH_WAIT_SECONDS=45
RMW_MDDS_SROS2_AUTH_PUB_TIMES=60 ohos/tools/run_cross_board_rmw_mdds_sros2_protected.sh
<rk3588a-subscriber-board> <rk3588a-publisher-board> 315` emitted signed-policy PASS on both boards,
protected-transport activation PASS on both boards, `RESULT|board_sros2_protected_authorized_pubsub|PASS|...|received=60`,
`RESULT|board_sros2_protected_unauthorized_publish|PASS|...|denied`, and
`cross_board_rmw_mdds_sros2_protected_ok`. This supersedes domain 290 for current-bridge approved-harness evidence.

Historical rmw_mdds graph-unregister refresh on 2026-07-06: after adding explicit broker unregister for destroyed
broker-mode endpoints, the host broker regression
`source install/setup.bash && LD_LIBRARY_PATH=$PWD/build/rmw_mdds_cpp:$LD_LIBRARY_PATH ctest --test-dir
build/rmw_mdds_cpp -R test_broker_mode --output-on-failure` passed `1/1`, covering
`RmwMddsBrokerMode.DestroyedBrokerEndpointLeavesNoNodeGraphEntry`. The refreshed OHOS overlay was deployed to
both RK3588A boards at that checkpoint; deployed hashes were `librmw_mdds_cpp.so`
`4459fbe764d98775c91c5505c20730560866e7cfacaf44f5d6ca904b372f9386` and broker
`7af506a2775937544b95bfee3d63d5a0e8f7aff2949aa5870c2497ac422f34db`. The deployed rclpy
graph-cleanup artifacts are `type_description_service.py`
`344bd340293bf65bf998b0a2fed3b00cc344e2ad3865db7ee8f9be98afa4deda` and `_rclpy_pybind11`
`de2a4f7ca198aeda71dca0f9b32ee3d75706a8cf645be158ee8d75757ba1c6f8`. This checkpoint is superseded by the
latest e46ec/0ce6c deployment and domain-290 protected SROS2 rerun.

Service stress correction on 2026-07-06: the DSoftBus/MDDS board-side artifacts under
`/data/local/tmp/rmw_mdds_service_stress_gate50` on the RK3588A board currently retaining that directory show
`GATE50_SUMMARY_FILES=10 PASS=10 FAIL=0`; every
`summary.json` has `CLIENT_CREATED=50`, `CLIENT_SENT=50`, `SERVER_REQ=50`, `CLIENT_OK=50`, and zero
timeout/error counters. The original CLI 50-way artifact under `/data/local/tmp/rmw_mdds_cli_service50`
shows `CLIENT_SENT=50`, `CLIENT_OK=50`, `SERVER_REQ=50`, `WAITING_LINES=0`, and `ERROR_LINES=0`.
Status-document recheck during this sync re-read all 10 `service_stress_gate50` `summary.json` files from
the board and confirmed every file still reports `PASS=true` with the same 50/50 created/sent/server/ok
counters and zero timeout/error counters. The paired RK3588A board did not retain the same structured stress
directory during this re-read. The CLI 50-way directory is still present; spot re-read of
`call_1.log` and `call_50.log` showed valid AddTwoInts responses. The structured gate50 summaries remain the
authoritative stress evidence because they include per-round counters.

Live status re-read for this documentation sync on 2026-07-06: the lightweight contracts still pass
(`bridge_protected_transport_contract_ok` and `rmw_mdds_script_contracts_ok`), and `bash -n` passed for the
updated cross-board service, coverage2, protected-SROS2, and graph-churn runners. The board-side service stress
JSON files above were read directly again; the old service-soak client/server logs still show request 12 timeout
and only 11 server callbacks as historical root-cause evidence, while current reruns after the broker
service-bridge history fix pass 20/1000/10000 sequential requests; coverage2 retained logs still show corrected 4MiB topic and service-wire PASS
plus the old oversized service body failure; and the current graph-churn summaries directly show CLI
node/topic/service 100/100 PASS plus rclpy_action 100/100 PASS. The rclpy fast-mode 1000/1000 result remains
recorded evidence from the prior run, but it was not freshly re-read from the current retained summary file
because later graph runs have overwritten that summary.

Sequential service-soak correction on 2026-07-06: the cross-board service runner now has
`RMW_MDDS_SERVICE_REQUESTS` and `RMW_MDDS_SERVICE_PROGRESS_INTERVAL` so it can act as a sequential service-soak
gate instead of a one-call smoke test. A fresh RK3588A 10-request run on domain 281 passed with
`RESULT|mdds_service_soak|PASS|domain=281|service=/rclpy_mdds_trigger|requests=10|client_sent=10|client_ok=10|server_req=10|timeout=0|error=0`.
A fresh 20-request run on domain 282 failed with
`RESULT|mdds_service_soak|FAIL|domain=282|service=/rclpy_mdds_trigger|requests=20|client_sent=12|client_ok=11|server_req=0|timeout=1|error=0`;
the client printed `TRIGGER_CLIENT_TIMEOUT index=12`, and the server printed `trigger_count=1` through
`trigger_count=11` but no `TRIGGER_SERVER_DONE`. The `server_req=0` field means the runner did not observe the
server completion marker; the server log still proves 11 callbacks and a stop before request 12. This is a
real historical service long-soak blocker and must remain separate from the 50-way process stress PASS. The
current fix adds host coverage in `RmwMddsIpcBroker.ServiceBridgeTransportUsesExpandedInternalHistory`: the
test failed while service bridge publisher/subscriber history depth stayed at the public service depth 10, then
passed after broker service/client bridge transport QoS was expanded. Full `test_ipc_broker` now passes 14/14.
The refreshed RK3588A deployment then passed `RESULT|mdds_service_soak|PASS` on domain 283 with 20/20 requests,
domain 284 with 1000/1000 requests, and domain 285 with 10000/10000 requests, all with `timeout=0` and
`error=0`. This closes the request-12 blocker for the current broker-mode sequential gate, while 2h stability
and broader P2/P3 evidence remain open.

Delivery endpoint decision as of 2026-07-06: no push, PR, Gerrit submission, or OpenSpec archive was requested,
so the executed endpoint remains a local two-repo handoff. The handoff is a status/evidence checkpoint, not a
production/full-feature completion claim.

P2 graph-churn progress on 2026-07-06: added `ohos/tools/run_rmw_mdds_board_graph_churn.sh`, extended it to
cover node/topic/service graph entries, and ran `RMW_MDDS_GRAPH_CHURN_CLI_TIMEOUT=4
ohos/tools/run_rmw_mdds_board_graph_churn.sh <device> 93 100` on RK3588A. The board-side summary reported
`GRAPH_CHURN_ROUNDS=100`, `GRAPH_CHURN_PASS=100`, `GRAPH_CHURN_FAIL=0`, topic/service created/destroyed
counters all at `100`, `RESULT|rmw_mdds_board_graph_churn|PASS|rounds=100|topics_created=100|services_created=100|topics_destroyed=100|services_destroyed=100`, and `rmw_mdds_board_graph_churn_ok`. This closes the current 100-round node/topic/service graph-churn checkpoint. A later rclpy fast-mode run closes the 1000-round graph-churn gate, and a later `rclpy_action` run closes the current 100-round action-specific graph-churn gate.

P2 graph-churn follow-up on 2026-07-06: the runner now also has `RMW_MDDS_GRAPH_CHURN_MODE=rclpy` for faster
same-process RMW graph churn diagnosis. The RK3588A domain-229 fast-mode run failed on the first round:
`GRAPH_CHURN_MODE=rclpy`, `GRAPH_CHURN_PASS=0`, `GRAPH_CHURN_FAIL=1`,
`RESULT|rmw_mdds_board_graph_churn_fast|FAIL|rounds=10|pass=0|fail=1|topics_created=1|services_created=1|topics_destroyed=0|services_destroyed=0`,
and `FIRST_FAILURE=round=1 found=1 gone=0`. The fresh observer snapshot showed that the destroyed custom topic
and custom service were gone, but the destroyed rclpy node and some parameter/type-description graph services
remained visible. A domain-230 default `cli` mode one-round regression passed after the script update. Therefore
the original CLI/cross-process graph churn evidence remains valid, but the fast same-process graph churn path is
a RED blocker and is not 1000-round PASS evidence.

P2 graph-churn latest status on 2026-07-06: after the broker explicit-unregister refresh and rmw graph-update
epoch ordering fix, rclpy fast-mode had failed on domains 231/236/237 and the later domain-249 one-round
recheck with `FIRST_FAILURE=round=1 found=1 gone=0`. The custom topic and custom service were absent from the
after snapshot, but destroyed rclpy node names and `get_type_description` services remained visible. Follow-up
debugging found that rclpy's type-description service wrapper created an `rcl_service_t` without the normal
custom deleter, so destruction deleted the wrapper without calling `rcl_service_fini()`, `rmw_destroy_service()`,
or broker unregister for `/get_type_description`. The current deployed fix adds the `rcl_service_fini()` deleter
in `rclpy/src/rclpy/type_description_service.cpp` and explicitly destroys/removes the Python-side wrapped
service. Current RK3588A evidence: domain 252 debug one-round reached endpoint count zero and passed
`RESULT|rmw_mdds_board_graph_churn_fast|PASS|rounds=1|topics_created=1|services_created=1|topics_destroyed=1|services_destroyed=1`;
domain 256 five-round fast-mode passed with `GRAPH_CHURN_PASS=5`, `GRAPH_CHURN_FAIL=0`, all created/destroyed
counters at `5`, and `rmw_mdds_board_graph_churn_fast_ok`. This closes the stale type-description graph
cleanup blocker. A later explicit-refresh cache fix handles same-broker stale frames as a successful refresh
and limits generic graph snapshot settle to 100ms; host `test_broker_mode` passes, including
`EmptyBrokerGraphRefreshIsCached`. After rebuilding and deploying `librmw_mdds_cpp.so`
`4459fbe764d98775c91c5505c20730560866e7cfacaf44f5d6ca904b372f9386`, domain 264 dropped from about
39s/round to 0.5-0.7s/round and passed 2/2, and domain 265 rclpy fast-mode passed 100/100 with all
created/destroyed counters at `100`. After fixing broker inactive connection reaping, domain 267 rclpy
fast-mode passed 1000/1000 with `GRAPH_CHURN_PASS=1000`, `GRAPH_CHURN_FAIL=0`, and all created/destroyed
counters at `1000`. The runner now also supports `RMW_MDDS_GRAPH_CHURN_MODE=rclpy_action`; RK3588A domain
268 passed 5/5 as a smoke check, and domain 269 passed 100/100 action-specific graph churn with
`GRAPH_CHURN_ACTIONS_CREATED=100`, `GRAPH_CHURN_ACTIONS_DESTROYED=100`,
`RESULT|rmw_mdds_board_graph_churn_action|PASS|rounds=100|actions_created=100|actions_destroyed=100`,
and `rmw_mdds_board_graph_churn_action_ok`. Long-soak graph evidence remains incomplete.

P2 coverage2 refresh on 2026-07-06: fixed `ohos/tools/run_cross_board_rmw_mdds_coverage2.sh` so large
payload lanes no longer depend on missing board `tr`, overlong `ros2 topic pub` YAML arguments, or truncated
`ros2 topic echo` output. The large payload publisher/subscriber now use rclpy and validate payload length in
the subscriber callback. The script also supports `RMW_MDDS_COVERAGE2_ONLY=large|transient|liveliness|bag`,
configurable large-lane warmup/match timeout, and publisher/subscriber completion markers. The transient-local
lane uses strict one-shot publish plus keep-alive for a true late-joiner retained-history check. Current isolated
RK3588A evidence:
`RMW_MDDS_COVERAGE2_ONLY=liveliness ... 141` emitted
`RESULT|cov2_qos_liveliness|PASS|received=16`; `RMW_MDDS_COVERAGE2_ONLY=bag ... 138` emitted
`RESULT|cov2_rosbag2_record_play|PASS|recorded_files=2|recorded_messages=56|played_received=51`; and
five strict transient-local reruns on domains 150-154 emitted
`RESULT|cov2_transient_local_replay|PASS|late_joiner_got_retained=12` with
`COVERAGE2_SUMMARY|pass=1|fail=0`. The old domain-143 transient-local failure is superseded by this 5/5
rerun. At this checkpoint, large payload appeared incomplete on the cross-board path:
`RMW_MDDS_COVERAGE2_ONLY=large ... 160`
passed 512KiB and 1MiB with `valid_len=1`, but failed 1.5MiB with `received=0|valid_len=0`; a standalone
1.5MiB retry on domain 161 also failed. Follow-up DSoftBus `maxSendSize` work produced a real RED/GREEN unit correction:
`MddsDSoftBusBackendTest.MaxSendSizeForcesFragmentationBelowDSoftBusLimit_001b` failed before the fix with
actual `4194240` versus expected `32768`, then passed on RK3588A with `BOARD_EXIT=0` after `dsoftbus_backend.c`
advertised `MDDS_DEFAULT_MAX_SEND_SIZE`. The updated bridge was deployed to both boards with sha
`a8618d2c27e41beadc2d308d08e4dda58229708442ec10e417b5d575d0547eb8`, but
`RMW_MDDS_COVERAGE2_ONLY=large ... 166` and domain 173 reruns still passed 512KiB/1MiB and failed 1.5MiB.
A later RED/GREEN DSoftBus defragmenter timeout fix made
`MddsDefragmenterTest.RecentFragmentKeepsEntryAlive_006b:MddsDefragmenterTest.SweepExpiredEntry_006`
pass `2/2` on RK3588A with `BOARD_EXIT=0`; after rebuilding `mdds_bridge_shared` and deploying bridge sha
`2975e13db300366f143ec3a32933d73c2bd3e769075f5d5e69978e5da7455c50` to both boards, the domain 184
large-only rerun still passed 512KiB/1MiB and failed 1.5MiB:
`RESULT|cov2_large1500k|FAIL|received=0|valid_len=0`. `publisher.get_subscription_count()` is not
authoritative for the cross-board bridge path because successful 1MiB deliveries also printed
`subscriptions=0`.
Status-document recheck during this earlier sync re-read board logs and confirmed the then-current large boundary:
`large512k_e.log` has `COV_BIGSUB_DONE size=524288 received=1 valid=1`, `large1m_e.log` has
`COV_BIGSUB_DONE size=1048576 received=1 valid=1`, and `large1500k_e.log` has
`COV_BIGSUB_DONE size=1572864 received=0 valid=0`. At that checkpoint both boards reported bridge sha
`2975e13db300366f143ec3a32933d73c2bd3e769075f5d5e69978e5da7455c50` and `librmw_mdds_cpp.so` sha
`3da10f0c8adc3973a1b4dae4752902b7c853fdd1422ae1569f0d02187e83d904`. Persisted same-board 1.5MiB logs
show `received=1 valid=1`, while persisted cross-board single-sample 1.5MiB logs show `received=0 valid=0`;
the blocker was then classified as cross-board route, fragment delivery, admission, queueing, or wait/take
behavior, not a proven local payload-size hard limit. This classification is superseded for the current
coverage2 topic large gate by the later harness correction and domain-260 PASS evidence below.

Additional DSoftBus unit verification after the defragmenter fix: the full RK3588A `MddsDefragmenterTest`
suite passed `13/13` with `BOARD_EXIT=0`; the
`MddsDSoftBusBackendTest.MaxSendSizeForcesFragmentationBelowDSoftBusLimit_001b` focused rerun also passed
with `BOARD_EXIT=0`.

Late 2026-07-06 status refresh: a fresh large-only RK3588A run on domain 193 with
`RMW_MDDS_COVERAGE2_ONLY=large RMW_MDDS_COVERAGE2_BIG_SUB_TIMEOUT=90
RMW_MDDS_COVERAGE2_STOP_WAIT=4` still emitted `RESULT|cov2_large512k|PASS|received=1|valid_len=1`,
`RESULT|cov2_large1m|PASS|received=1|valid_len=1`, `RESULT|cov2_large1500k|FAIL|received=0|valid_len=0`,
and `COVERAGE2_SUMMARY|pass=2|fail=1`. Current board logs for that run show the same
`COV_BIGSUB_DONE` boundary: 512KiB and 1MiB received/valid, 1.5MiB received=0 valid=0. A new
DSoftBus focused RED test, `MddsConnManagerTest.PendingQueueAcceptsLargeStartupBurst_040kc`, builds inside
`MddsDSoftBusBackendTest` but fails on RK3588A with `actual: 2048 vs 64` and `BOARD_EXIT=1`. This records
the pending queue/admission capacity as an unfixed blocker candidate at that checkpoint, not as a completed fix.

Final 2026-07-06 status refresh for the pending-queue investigation: the pending queue RED test above is now
GREEN at the DSoftBus unit level. `MDDS_CONN_MAX_PENDING` now scales as `MDDS_MAX_FRAGMENTS_PER_MSG * 8`,
large pending drain arrays in `SweepRetryConnections` and `MddsConnGetOrCreate` are heap allocated, and the
mock socket send-capture capacity follows `MDDS_CONN_MAX_PENDING`. After rebuilding
`MddsDSoftBusBackendTest`, the RK3588A focused group
`MddsConnManagerTest.PendingQueueAcceptsLargeStartupBurst_040kc:
MddsConnManagerTest.PendingQueueFullAndFlushOnBind_040k:
MddsConnManagerTest.PendingQueueFullReturnsError_058:
MddsConnManagerTest.ConnManagerMemoryEstimate*` passed `6/6` with `BOARD_EXIT=0`.
`mdds_bridge_shared` was then rebuilt and deployed to both boards with bridge sha
`f1d24c5431b4e01472603b298e1dbcff09302da3a3d42a876dcd0b8a8e257127`. The follow-up cross-board large-only
run on domain 194 still passed 512KiB and 1MiB but failed 1.5MiB:
`RESULT|cov2_large512k|PASS|received=1|valid_len=1`,
`RESULT|cov2_large1m|PASS|received=1|valid_len=1`,
`RESULT|cov2_large1500k|FAIL|received=0|valid_len=0`, and `COVERAGE2_SUMMARY|pass=2|fail=1`.
That domain-194 result is retained as a historical checkpoint. A later coverage2 harness audit found two
test-side problems: the subscriber was launched with `min_valid=1` while the publisher sent 12/10/8 samples,
and the host-side HDC cap could expire before the board-side subscriber timeout. The harness now passes
`min_valid=times`, requires `received >= times` and `valid_len >= times`, and exposes
`RMW_MDDS_COVERAGE2_HDC_TIMEOUT`.

Final 2026-07-06 coverage2 large refresh after the harness correction: the same-domain RK3588A run
`RMW_MDDS_COVERAGE2_ONLY=large RMW_MDDS_COVERAGE2_LARGE_DOMAIN_STRIDE=0
RMW_MDDS_COVERAGE2_BIG_SUB_TIMEOUT=240 RMW_MDDS_COVERAGE2_HDC_TIMEOUT=300
ohos/tools/run_cross_board_rmw_mdds_coverage2.sh <board-a> <board-b> 260` emitted
`RESULT|cov2_large512k|PASS|received=12|valid_len=12`,
`RESULT|cov2_large1m|PASS|received=10|valid_len=10`,
`RESULT|cov2_large1500k|PASS|received=8|valid_len=8`, and `COVERAGE2_SUMMARY|pass=3|fail=0`.
Latest board-side topic logs re-read from `/data/local/tmp/coverage2` contain
`COV_BIGSUB_DONE size=524288 received=12 valid=12`,
`COV_BIGSUB_DONE size=1048576 received=10 valid=10`, and
`COV_BIGSUB_DONE size=1572864 received=8 valid=8`. Therefore the current topic large-payload coverage2
gate is closed through 1.5MiB.

Service large-payload follow-up on 2026-07-06: `ohos/tools/run_cross_board_rmw_mdds_coverage2.sh` now also
supports `RMW_MDDS_COVERAGE2_ONLY=service_large`, using `rcl_interfaces/srv/SetParameters` to validate both a
large request string and a large response string. The RK3588A run
`RMW_MDDS_COVERAGE2_ONLY=service_large RMW_MDDS_COVERAGE2_HDC_TIMEOUT=360
ohos/tools/run_cross_board_rmw_mdds_coverage2.sh <board-a> <board-b> 270` emitted
`RESULT|cov2_service_large512k|PASS|sent=3|server_req=3|server_valid=3|client_ok=3|response_valid=3`,
`RESULT|cov2_service_large1m|PASS|sent=2|server_req=2|server_valid=2|client_ok=2|response_valid=2`,
`RESULT|cov2_service_large1500k|PASS|sent=1|server_req=1|server_valid=1|client_ok=1|response_valid=1`, and
`COVERAGE2_SUMMARY|pass=3|fail=0`. Re-read board logs under `/data/local/tmp/coverage2` contain matching
client done lines (`sent/ok/response_valid` at `3/3/3`, `2/2/2`, and `1/1/1`) and server done lines
(`requests/valid` at `3/3`, `2/2`, and `1/1`). Therefore the current service large-payload gate is closed
through 1.5MiB; the corrected 4MiB service-wire boundary is recorded below, while 16MiB support/fix, long-soak, and
performance gates remain open.

Latest 4MiB boundary refresh on 2026-07-06: after updating DSoftBus large-payload handling, the focused OHOS
build `./build.sh --product-name khd_rk3588_a --ccache --no-prebuilt-sdk -T MddsDSoftBusBackendTest -T
MddsDefragmenterTest -T MddsPubSubTest -T MddsMessageFrameTest` succeeded. RK3588A focused runs then passed
`MddsDSoftBusBackendTest.ServiceSendFragmentsLargePayload_012e`,
`MddsDefragmenterTest.WirePayloadAllowsUserPayloadPlusTransportOverhead_013`,
`MddsPubSubTest.ReliablePayloadAcceptsFullUserPayload_206`, and
`MddsMessageFrameTest.DecodeRejectsOversizedPayload_001`, each with `BOARD_EXIT=0`. `mdds_bridge_shared` was
rebuilt and deployed to both boards with bridge sha
`8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc`; rmw and broker remained at sha
`4459fbe764d98775c91c5505c20730560866e7cfacaf44f5d6ca904b372f9386` and
`7af506a2775937544b95bfee3d63d5a0e8f7aff2949aa5870c2497ac422f34db`.

The latest isolated RK3588A exact-total 4MiB topic run
`RMW_MDDS_COVERAGE2_ONLY=large RMW_MDDS_COVERAGE2_SKIP_DEFAULT_LARGE=1
RMW_MDDS_COVERAGE2_LARGE_EXTRA_CASES=large4m_exact:4194254:1 ... <board-a> <board-b> 275` emitted
`RESULT|cov2_large4m_exact|PASS|received=1|valid_len=1` and `COVERAGE2_SUMMARY|pass=1|fail=0`. The latest
isolated exact-total 4MiB service run
`RMW_MDDS_COVERAGE2_ONLY=service_large RMW_MDDS_COVERAGE2_SKIP_DEFAULT_SERVICE_LARGE=1
RMW_MDDS_COVERAGE2_SERVICE_EXTRA_CASES=service_large4m_exact:4194250:1 ... <board-a> <board-b> 276` emitted
`RESULT|cov2_service_large4m_exact|FAIL|sent=1|server_req=0|server_valid=0|client_ok=0|response_valid=0`, with
client log `COV_BIGSVC_CLIENT_DONE size=4194250 sent=1 ok=0 response_valid=0 service_available=1` and server log
`COV_BIGSVC_SERVER_DONE size=4194250 requests=0 valid=0`; board-side serialization later proved this body size
produces request wire `4194416`, above the 4MiB rmw_mdds payload cap, so it is now classified as an oversized
negative case. The corrected exact 4MiB service-wire run
`RMW_MDDS_COVERAGE2_ONLY=service_large RMW_MDDS_COVERAGE2_SKIP_DEFAULT_SERVICE_LARGE=1
RMW_MDDS_COVERAGE2_SERVICE_EXTRA_CASES=service_large4m_wire:4194139:1 ... <board-a> <board-b> 277` emitted
`RESULT|cov2_service_large4m_wire|PASS|sent=1|server_req=1|server_valid=1|client_ok=1|response_valid=1` and
`COVERAGE2_SUMMARY|pass=1|fail=0`. This closes the focused topic 4MiB and corrected service 4MiB wire gates;
16MiB is covered by the follow-up negative evidence below.

16MiB boundary follow-up on 2026-07-07: the coverage2 extra topic case
`RMW_MDDS_COVERAGE2_ONLY=large RMW_MDDS_COVERAGE2_SKIP_DEFAULT_LARGE=1
RMW_MDDS_COVERAGE2_LARGE_EXTRA_CASES=large16m:16777216:1 ... <board-a> <board-b> 308` failed on RK3588A with
`RESULT|cov2_large16m|FAIL|received=0|valid_len=0` and `COVERAGE2_SUMMARY|pass=0|fail=1`. The publisher log
shows `rclpy._rclpy_pybind11.RCLError: Failed to publish: MDDS broker publish failed: failed to encode frame`;
the subscriber log shows `COV_BIGSUB_DONE size=16777216 received=0 valid=0`. 16MiB is therefore a current
large-payload blocker, not just a missing evidence row. Source review confirms the current broker IPC protocol
caps frame payloads at 16MiB (`kMaxFramePayloadSize`), while the broker publish path writes an
`EncodeSampleMessage()` envelope around the user payload before `EncodeFrame()`. A 16MiB user payload therefore
exceeds the actual broker IPC frame-payload budget unless the implementation adds chunking or declares a lower
maximum user-payload contract.

Additional 16MiB boundary split on 2026-07-07: `large16m_total:16777164:1 ... 309` makes the generated topic
string exactly 16MiB and still fails with the same publisher encode-frame error; the subscriber log is
`COV_BIGSUB_DONE size=16777164 received=0 valid=0`. `large16m_ipc_budget:16777143:1 ... 310` fits the broker
IPC frame-payload budget after the 21-byte `SampleMessage` envelope; the publisher log reaches
`COV_BIGPUB_DONE size=16777143 times=1 subscriptions=1`, but the subscriber log is still
`COV_BIGSUB_DONE size=16777143 received=0 valid=0`. This separates the blocker into two current gaps:
exact 16MiB topic/user payloads exceed the broker IPC envelope budget, and a near-16MiB payload that fits that
budget is still blocked downstream. The downstream source caps remain `MDDS_MAX_PAYLOAD_SIZE=4MiB` and
`MDDS_MAX_FRAME_PAYLOAD_SIZE=4MiB+512`, so the production gate needs either real MDDS/DSoftBus expansion plus
error propagation or an accepted lower maximum-payload contract.

Late IPC fix recheck on 2026-07-07: added a host regression for a max user-payload sample plus
`SampleMessage` envelope and increased the broker IPC frame-payload budget so the exact 16MiB user payload
does not fail at the first IPC encode layer. Focused host regression
`ctest --test-dir build/rmw_mdds_cpp -R '^(test_ipc_protocol|test_ipc_transport|test_ipc_broker|test_broker_mode|test_broker_process)$' --output-on-failure`
passed 5/5 with `LD_LIBRARY_PATH=$PWD/build/rmw_mdds_cpp:$PWD/install/lib:$LD_LIBRARY_PATH`. The OHOS overlay
was rebuilt and deployed to both RK3588A boards; board-side hashes are
`librmw_mdds_cpp.so=e347fe583811c9e3082476b804bae5a638f553756b1e27ef5d2e9e75225413b6`,
`rmw_mdds_broker=fb70c341e896fcf45a7341b9732edc1bdb4f7ffc6cc1a8051fd1c16c5cc6abab`,
bridge `8b918aa3592249fb6c922c490c92566f32d77311d18104d64496059677e578bc`, softbus client
`e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`, and protected probe
`6aa06d13089af31f93e5ed1c7c694fc86fa353077266d8b2003be9481cb063c0`. The rerun
`large16m_total_after_ipcfix:16777164:1 ... 311` reached
`COV_BIGPUB_DONE size=16777164 times=1 subscriptions=1`, but the subscriber remained
`COV_BIGSUB_DONE size=16777164 received=0 valid=0`; `large16m_ipc_budget_after_ipcfix:16777143:1 ... 312`
also reached publisher done while the subscriber stayed `received=0 valid=0`. This moves the active blocker
from first-layer broker IPC encode failure to downstream MDDS/DSoftBus payload/fragment/queue delivery or
error propagation. The focused OHOS `MddsMessageFrameTest` target also built successfully, but the broader
16MiB gate remains FAIL.

MDDS bridge/fragment capacity refresh on 2026-07-07: after the late IPC recheck, DSoftBus/MDDS raised
`MDDS_MAX_PAYLOAD_SIZE` to 16MiB, kept a 512-byte frame payload allowance, and expanded the defragmenter receive
mask to `MDDS_FRAGMENT_MASK_WORDS`. The focused OHOS build passed for `MddsMessageFrameTest`,
`MddsDefragmenterTest`, `MddsDSoftBusBackendTest`, and `MddsPubSubTest`. RK3588A focused reruns passed the
16MiB capacity test, defragmenter oversized/wire-payload tests, pending queue/memory gates, and reliable
full-payload gate with `BOARD_RC=0`. `mdds_bridge_shared` was rebuilt and deployed to both boards with sha
`037553404f7a9d9a37ad69343f0f8a013ec66230d0da53af1c177a3ad55c18ef`; `libsoftbus_client.z.so` remained
`e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`. The cross-board large topic case
`large16m_total_after_mddsfix:16777164:1 ... 118` now passes with
`RESULT|cov2_large16m_total_after_mddsfix|PASS|received=1|valid_len=1` and
`COVERAGE2_SUMMARY|pass=1|fail=0`; the helper's final `String.data` is exactly 16MiB. The follow-up
`large16m_full_user_after_mddsfix:16777216:1 ... 119` still fails with
`RESULT|cov2_large16m_full_user_after_mddsfix|FAIL|received=0|valid_len=0` and publisher
`MDDS broker publish failed: failed to encode frame`; this case uses a 16MiB body plus validation prefix/suffix,
so it is above the exact 16MiB topic-string boundary. At that checkpoint the broad gate was improved to
exact-total 16MiB topic single-sample PASS, but still lacked service 16MiB evidence; the later recv-queue
follow-up below supersedes that service gap for single-sample exact-wire only. The broader production gate remains
incomplete for higher-count/concurrent/longer-soak large messages, oversized/error-propagation behavior, soak, sanitizer, and
performance evidence.

Pre-recv-queue-fix service exact-wire threshold follow-up on 2026-07-07: the then-current RK3588A deployment
was re-read as
`librmw_mdds_cpp.so=e347fe583811c9e3082476b804bae5a638f553756b1e27ef5d2e9e75225413b6`,
`rmw_mdds_broker=fb70c341e896fcf45a7341b9732edc1bdb4f7ffc6cc1a8051fd1c16c5cc6abab`,
`libmdds_bridge_shared.z.so=037553404f7a9d9a37ad69343f0f8a013ec66230d0da53af1c177a3ad55c18ef`, and
`libsoftbus_client.z.so=e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`. Board-side
SetParameters serialization checks mapped exact rmw_mdds request-wire targets to body sizes:
8MiB=`8388443`, 12MiB=`12582745`, 14MiB=`14679897`, 15MiB=`15728473`, and 16MiB=`16777049`. Retained
`/data/local/tmp/coverage2` logs show `service_large8m_wire_exact_after_mddsfix`,
`service_large12m_wire_exact_after_mddsfix`, and `service_large14m_wire_exact_after_mddsfix` all reached
`sent=1`, `server_req=1`, `server_valid=1`, `client_ok=1`, and `response_valid=1`. The 15MiB and 16MiB
exact request-wire probes both reached `sent=1` but stopped at `server_req=0`, `client_ok=0`, and
`response_valid=0`. This is a real service request admission/receive/defrag/queue/take-path blocker before the
server callback, not a response-path or stdout parsing issue.

Recv-queue burst-capacity follow-up on 2026-07-07 supersedes that 15/16MiB `server_req=0` result for the
single-sample exact-wire cases. The focused RK3588A regression
`MddsDSoftBusBackendTest.RecvQueueAcceptsFullFragmentBurstWhileDispatchBlocked_033e` failed before the fix at
`burst index=256 maxFragments=513` with `BOARD_RC=1`. DSoftBus/MDDS then changed `recv_queue.c` to size the
receive queue as `RECV_QUEUE_CAPACITY=(MDDS_MAX_FRAGMENTS_PER_MSG * 4)`, and the same focused regression passed
with `[ PASSED ] 1 test`, `BOARD_RC=0`, and XML `failures=0`. `mdds_bridge_shared` was rebuilt and redeployed
to both boards with sha `d314e6cd6d3937f37d9d5ea853225e9c581eafc3222145122860fbfc81c04a99`; `libsoftbus_client.z.so`
remained `e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d`. Cross-board reruns then passed
`service_large15m_wire_exact_after_recvqfix:15728473:1` and
`service_large16m_wire_exact_after_recvqfix:16777049:1`, both with
`sent=1`, `server_req=1`, `server_valid=1`, `client_ok=1`, and `response_valid=1`. This closes the single-sample
service exact-wire 15/16MiB blocker, but not higher-count/concurrent/longer-soak large-message, oversized/error-propagation,
soak, sanitizer, or performance gates.

Source review after the latest 4MiB rerun confirms the remaining blocker is real implementation scope, not
missing paperwork. Current DSoftBus/MDDS source now has `MDDS_RELIABLE_MAX_PAYLOAD_SIZE=MDDS_MAX_PAYLOAD_SIZE`,
`DSoftBusSendService()` fragments payloads larger than `MDDS_DEFAULT_FRAGMENT_SIZE`, and the frame/defrag path
uses `MDDS_MAX_FRAME_PAYLOAD_SIZE` while preserving the final user-payload cap. The corrected service 4MiB wire
probe is now green; the old `4194250` body probe remains only an oversized negative case and should not be used
as the exact 4MiB service blocker.

## 5. Final Completion Decision

- [x] 5.1 Update the parity matrix with all final command evidence and board markers.
- [x] 5.2 Verify tracked status is clean and generated/scratch artifacts are ignored or intentionally tracked.
- [x] 5.3 Validate all active OpenSpec changes with `openspec validate --strict`.
- [x] 5.4 Decide whether accepted unsupported rows still satisfy the user's "perfect/all ROS 2 middleware features" objective.
- [ ] 5.5 Mark the persistent goal complete only if every required row is proven or explicitly accepted out of scope and no required delivery work remains.

Final audit correction on 2026-07-06: the previous retained 50-way service stress artifacts were historical PASS
evidence, but the then-latest deployed overlay did not pass the default 50 independent-process hard gate. That
`processes --clients 50 --timeout 75` run reached `CLIENT_CREATED=50`, `CLIENT_SENT=50`, `SERVER_REQ=50`, and
`CLIENT_OK=50`, but failed with `PROCESS_TIMEOUT=39`, `ELAPSED_MS=90847`, and `PASS=False`. The old 30/50
request-admission ceiling was no longer the symptom; cleanup-marker round 61 narrowed that blocker to
response-afterward `node.destroy_node()`/process cleanup rather than request admission, server take, or response
delivery.
2026-07-07 service-stress repair refresh: after fixing client cleanup and pre-ACK delivery handling, the
`ee2a285...` RK3588A service-stress repair overlay passed smoke round 82 with `SERVER_REQ=50`, `CLIENT_OK=50`,
`PROCESS_TIMEOUT=0`, `CLIENT_DESTROY_DONE=50`, and `CLIENT_SHUTDOWN_DONE=50`. The hard gate rounds 83-92 then
produced 10/10 board-side `summary.json` PASS files with `SERVER_REQ=50`, `CLIENT_OK=50`, and
`PROCESS_TIMEOUT=0`.
The request-12 sequential service-soak blocker is fixed for the current broker-mode gate by RK3588A domain 288
1000/1000 and domain 289 10000/10000 PASS runs. The corrected topic plus service large-payload coverage2 gates
pass through 1.5MiB, focused DSoftBus unit gates for 4MiB handling pass on RK3588A, and corrected exact 4MiB
topic/service-wire probes pass cross-board. The 16MiB topic boundary is now partially closed: exact-total
16MiB `String.data` single-sample topic delivery passes cross-board with the refreshed MDDS bridge, while
body=16MiB plus helper validation bytes remains an oversized encode-frame failure. After the recv-queue
burst-capacity fix, service exact-wire 15/16MiB single-sample delivery also passes on the current deployment;
the broader service large-message multi-run/concurrent/error-propagation gates remain open.
The broad production/full-feature goal must remain open. The 2026-07-06 host delivery contract, rmw_mdds
`test_rmw_implementation` subset, coverage2 liveliness lane, topic rosbag2 record/play lane, service rosbag2
event record/info/playback lane, service request playback lane, action hidden-topic rosbag2 record/info/playback lane, strict
transient-local replay lane, request-path 50/50 evidence, topic large-payload lane through 4MiB, service
large-payload lane through 1.5MiB, graph churn domain 286/287 focused reruns, and protected signed SROS2
domain-315 current-bridge harness are proven. Current graph-churn recheck on the same `e347fe.../fb70c341...`
rmw/broker plus `d314e6cd...` bridge deployment also proves domain 316 rclpy node/topic/service 100/100,
domain 317 rclpy_action 100/100, and domain 318 CLI/cross-process node/topic/service 100/100 with
`RMW_IMPLEMENTATION=rmw_mdds_cpp`. Current cross-board action introspection CLI also passes on domain 320:
`ros2 action list -t`, `ros2 action type /fibonacci`, `ros2 action info /fibonacci -t`, and
`ros2 action send_goal ... --feedback` returned the expected Fibonacci type, server, feedback, result, and
`SUCCEEDED` status. The late IPC fix host regression is proven and deployed, and the later MDDS
bridge/fragment plus recv-queue refresh proves one cross-board exact-total 16MiB `String.data` topic sample
plus service exact-wire 15/16MiB single-sample cases on the current `e347fe.../fb70c341...` rmw/broker plus
bridge `d314e6cd...` deployment. The 50 independent-process stress hard gate is now also proven on that current
deployment: smoke round 301 passed, hard-gate rounds 302-311 are 10/10 PASS with `SERVER_REQ=50`,
`CLIENT_OK=50`, and `PROCESS_TIMEOUT=0`, and supplemental modes round 312 `many_clients_one_process` plus
round 313 `one_client_many_requests` both reached 50/50. The current deployment also passed a 2h paced
cross-board service soak on domain 322 with `RMW_MDDS_SERVICE_REQUESTS=36000` and
`RMW_MDDS_SERVICE_INTERVAL_SECONDS=0.2`: `TRIGGER_CLIENT_DONE expected=36000 sent=36000 ok=36000 timeout=0 error=0 elapsed_sec=7313.517`,
`TRIGGER_SERVER_DONE requests=36000 expected=36000 elapsed_sec=7325.966`, and
`RESULT|mdds_service_soak|PASS|domain=322|service=/rclpy_mdds_trigger|requests=36000|client_sent=36000|client_ok=36000|server_req=36000|timeout=0|error=0`.
The remaining P2/P3 evidence gates are:
higher-count/concurrent/longer-soak large-message gates, service oversized/error-propagation behavior,
concurrent graph churn,
dedicated action bag CLI support, broader board security matrices, ASAN, TSAN, and
current performance baseline.

2026-07-07 live spot-check update: the completion decision above is unchanged, but the service-stress blocker is
now superseded by the repair refresh. Re-ran the lightweight local contracts (`bridge_protected_transport_contract_ok`,
`rmw_mdds_script_contracts_ok`) and `openspec validate --changes --strict` in the ROS 2 workspace (`4 passed,
0 failed`). `hdc list targets` printed both RK3588A targets and then returned the known host exit 139. Direct
board hash re-read after the latest deploy matched `librmw_mdds_cpp.so=ee2a285...`, `rmw_mdds_broker=0ce6c5eb...`,
`libmdds_bridge_shared.z.so=8b918aa3...`, and `libsoftbus_client.z.so=e1771298...`. Current service-stress
artifacts show smoke round 82 passed 50/50 with `PROCESS_TIMEOUT=0`, and rounds 83-92 produced 10/10 PASS
summary files. Retained graph summaries directly show node/topic/service 100/100 PASS and rclpy_action 100/100
PASS; retained service-soak logs directly show 10000/10000 client/server completion. Domain 286/288/289 labels
remain prior command-record evidence rather than fields encoded in the retained summary/client/server logs re-read
in this spot-check.

Late 2026-07-07 IPC fix update: the deployment hash re-read above is superseded only for the rmw/broker
overlay by `librmw_mdds_cpp.so=e347fe...` and `rmw_mdds_broker=fb70c341...`; bridge, softbus client, and
protected probe hashes were unchanged at that intermediate checkpoint. The late overlay passed the five focused
IPC/broker host tests and removed the first-layer broker encode-frame failure for near-16MiB probes, but its
board probes still failed at delivery with subscriber `received=0 valid=0`. That late-IPC failure is superseded
for exact-total 16MiB topic status by the MDDS bridge/fragment refresh recorded above. The earlier 50-way
service-stress 10/10 PASS remains evidence for the `ee2a285...` service-stress repair overlay, and the same
gate has now been rerun on the current `e347fe.../fb70c341...` plus `d314e6cd...` deployment with rounds
302-311 at 10/10 PASS. Task 5.5 remains open because the remaining P2/P3 production gates are still incomplete.

2026-07-08 service availability-gating fix update: the host regression for treating remote graph-sync service
endpoints as graph-visible but not locally available is green. Focused tests
`RmwMddsIpcBroker.MarksRemoteGraphServicesAsNonLocal` and
`RmwMddsBrokerMode.RemoteGraphOnlyServiceDoesNotSatisfyAvailability` passed, full `test_ipc_broker` passed
15/15, full `test_broker_mode` passed 8/8 with 1 disabled, and
`bash ohos/test_rmw_mdds_script_contracts.sh` emitted `rmw_mdds_script_contracts_ok`. The AArch64 overlay was
rebuilt and deployed to both RK3588A boards with `librmw_mdds_cpp.so=50e8ee...`,
`rmw_mdds_broker=3555d7...`, protected probe `6aa06d...`, bridge `d314e6cd...`, and softbus client
`e1771298...`. The same three-case service exact-wire 16MiB repeat10 hard gate still failed once on the latest
deployment: `fix1` reported `sent=2 server_req=0 server_valid=0 client_ok=0 response_valid=0`, while `fix2`
and `fix3` both passed 10/10, so the summary was `COVERAGE2_SUMMARY|pass=2|fail=1`. Task 5.5 remains open
because this proves the latest availability-gating fix did not close the 16MiB service multiround P2 blocker.

Historical 2026-07-07 status-doc sync before the recv-queue fix: re-ran the lightweight local/document gates before editing and confirmed
`bridge_protected_transport_contract_ok`, `rmw_mdds_script_contracts_ok`, and `openspec validate --changes
--strict` in the ROS 2 workspace with `4 passed, 0 failed`. `hdc list targets` printed both RK3588A targets and
then returned the known host exit 139. Direct hash re-read on both boards confirms the current rmw/broker deployment
was `librmw_mdds_cpp.so=e347fe583811c9e3082476b804bae5a638f553756b1e27ef5d2e9e75225413b6` and
`rmw_mdds_broker=fb70c341e896fcf45a7341b9732edc1bdb4f7ffc6cc1a8051fd1c16c5cc6abab`; the later MDDS refresh
updated bridge to `037553404f7a9d9a37ad69343f0f8a013ec66230d0da53af1c177a3ad55c18ef` while softbus
client and protected probe remain unchanged. Board-side after-mddsfix logs show
`large16m_total_after_mddsfix` subscriber `received=1 valid=1`, while
`large16m_full_user_after_mddsfix` remains `received=0 valid=0` with publisher `failed to encode frame` because
that helper case exceeds the exact 16MiB topic-string boundary. This closes only the single-sample exact-total
16MiB topic evidence point. Additional retained service logs at that checkpoint showed exact-wire 8/12/14MiB PASS
and exact-wire 15/16MiB FAIL with `server_req=0`; this service threshold is superseded by the recv-queue fix
sync below.

Current 2026-07-07 recv-queue fix sync: DSoftBus/MDDS RED/GREEN testing showed the fixed 256-entry receive queue
could not absorb a full 16MiB fragment burst while the dispatch callback was blocked. After increasing the queue
capacity to `MDDS_MAX_FRAGMENTS_PER_MSG * 4`, the focused RK3588A regression passed and `mdds_bridge_shared` was
redeployed as sha `d314e6cd6d3937f37d9d5ea853225e9c581eafc3222145122860fbfc81c04a99` on both boards. Fresh
board-side log re-read shows `service_large15m_wire_exact_after_recvqfix` and
`service_large16m_wire_exact_after_recvqfix` both passed with `sent=1`, `server_req=1`, `server_valid=1`,
`client_ok=1`, and `response_valid=1`. This supersedes the older 15/16MiB `server_req=0` threshold for the
single-sample exact-wire cases. A later live recheck on this exact `e347fe.../fb70c341...` plus `d314e6cd...`
deployment passed the service-stress smoke round 301, hard-gate rounds 302-311 10/10, supplemental split
models rounds 312/313, and current domain 322 2h paced service soak 36000/36000 with `timeout=0 error=0`.
Task 5.5 remains open because P2/P3 higher-count/concurrent/longer-soak large-message, concurrent graph churn,
broader security matrix, sanitizer, and performance gates are still incomplete.

Current 2026-07-08 CST repeated 16MiB large-message sync: host-side `pgrep` showed no active coverage2,
graph_churn, service_stress, ros2_rmw_mdds, or rmw_mdds test runner, and both RK3588A boards showed no matching
ROS 2/rmw_mdds/coverage/graph test process after the run. `hdc` again printed valid board-side output and then
returned host exit 139, so this checkpoint is judged from board-side markers. Domain 328
`large16m_total_repeat3:16777164:3` passed with
`RESULT|cov2_large16m_total_repeat3|PASS|received=3|valid_len=3`,
`COVERAGE2_SUMMARY|pass=1|fail=0`, publisher log
`COV_BIGPUB_DONE size=16777164 times=3 subscriptions=0`, and subscriber log
`COV_BIGSUB_DONE size=16777164 received=3 valid=3`. Domain 329
`service_large16m_wire_repeat3:16777049:3` passed with
`RESULT|cov2_service_large16m_wire_repeat3|PASS|sent=3|server_req=3|server_valid=3|client_ok=3|response_valid=3`,
`COVERAGE2_SUMMARY|pass=1|fail=0`, client log
`COV_BIGSVC_CLIENT_DONE size=16777049 sent=3 ok=3 response_valid=3 service_available=1`, and server log
`COV_BIGSVC_SERVER_DONE size=16777049 requests=3 valid=3`. The publisher/client logs also contain `Signal 15`
after DONE markers due to harness cleanup, not a board-side feature failure. Task 5.5 remains open because
P2/P3 higher-count/concurrent/longer-soak large-message, concurrent graph churn, broader security matrix,
sanitizer, and performance gates are still incomplete.

Current 2026-07-08 CST higher-count 16MiB large-message follow-up: exact-total topic repeat10 passed on
domain 330 with `RESULT|cov2_large16m_total_repeat10|PASS|received=10|valid_len=10`,
`COVERAGE2_SUMMARY|pass=1|fail=0`, publisher log `COV_BIGPUB_DONE size=16777164 times=10 subscriptions=0`,
and subscriber log `COV_BIGSUB_DONE size=16777164 received=10 valid=10`. An intermediate service exact-wire
16MiB repeat10 run on domain 331 failed with `server_req=0`, and follow-up repeat3 reruns on domains 332/335
also failed, while 4MiB repeat3 and 16MiB single-sample sanity passed. This remains historical service-large
risk evidence. The harness was then corrected to embed `REQIDXnnnnnn_` inside the fixed-length service payload
and to require exact service counters; the fixed-index rerun passed domain 157 topic exact-total 16MiB repeat10
with `received=10 valid_len=10`, then passed domain 158 service exact-wire 16MiB repeat10 with
`sent=10 server_req=10 server_valid=10 client_ok=10 response_valid=10`. A later same-day hard-gate rerun showed
the service 16MiB production gap is still real, not just missing evidence: domain 162 4MiB control passed,
domain 163 16MiB single passed, and domain 164 standalone 16MiB repeat10 passed, but consecutive 16MiB repeat10
cases on domains 165/166/167 produced `COVERAGE2_SUMMARY|pass=1|fail=2`; rerun2 and rerun3 both reported
`sent=2 server_req=0 server_valid=0 client_ok=0 response_valid=0`. Task 5.5 remains open because the current
service 16MiB multiround hard gate fails before the server callback; concurrent/long-soak service-large evidence
also remains missing.

Current 2026-07-09 concurrent4x10 follow-up: domain 1262 first proved the repeated 4 clients x 10 exact-wire
16MiB service path was not fully closed (`CLIENT_SENT=40 CLIENT_RESP=38 SERVER_REQ=38 SERVER_RC=1`). Live
diagnosis then isolated two defects: MDDS DSoftBus `recv_queue` dropped fragments on temporary queue-full
admission, and the `rmw_mdds` broker service-bridge backpressure timeout released `max_unacked=1` instead of
remaining diagnostic-only. After adding the focused RK3588A recv-queue regression and changing broker timeout
semantics, the deployed artifacts were `librmw_mdds_cpp.so=a9456b23...`, `rmw_mdds_broker=67a7ad42...`, and
`libmdds_bridge_shared.z.so=7d7e20ba...`. Domain 1266 then passed the same default-environment concurrent4x10
shape with `CLIENT_SENT=40 CLIENT_RESP=40 SERVER_REQ=40 SERVER_RC=0`. A later load-path audit found
`rmw_mdds_env.sh` actually loads `/data/local/tmp/libmdds_bridge_shared.z.so`; after syncing the same
`7d7e20...` bridge to that path, fresh domains 1268 and 1269 both passed 40/40, but domain 1270 failed after
more than 630s at `CLIENT_SENT=33 CLIENT_RESP=31 SERVER_REQ=31` with one client timeout. Task 5.5 therefore
remains open: the current multi-round repeated concurrent4x10 gate is still a blocker, and large-message long
stability, oversized/error-propagation, sanitizer, performance, and full P2/P3 evidence remain incomplete.

Current 2026-07-07 graph-churn recheck: on the same `e347fe.../fb70c341...` plus `d314e6cd...` current
deployment, `ohos/tools/run_rmw_mdds_board_graph_churn.sh` passed three 100-round focused lanes on RK3588A.
Domain 316 `RMW_MDDS_GRAPH_CHURN_MODE=rclpy` reported `GRAPH_CHURN_PASS=100`, `GRAPH_CHURN_FAIL=0`,
topics/services created/destroyed all `100`, and `rmw_mdds_board_graph_churn_fast_ok`. Domain 317
`RMW_MDDS_GRAPH_CHURN_MODE=rclpy_action` reported `GRAPH_CHURN_PASS=100`, action created/destroyed `100/100`,
and `rmw_mdds_board_graph_churn_action_ok`. Domain 318 `RMW_MDDS_GRAPH_CHURN_MODE=cli` with
`RMW_MDDS_GRAPH_CHURN_CLI_TIMEOUT=8` reported `GRAPH_CHURN_PASS=100`, topics/services created/destroyed all
`100`, and `rmw_mdds_board_graph_churn_ok`. This updates current graph evidence, but Task 5.5 remains open
because concurrent graph coverage, higher-count/concurrent/longer-soak large-message gates, broader security matrix,
ASAN/TSAN, and performance baseline evidence are still incomplete.

Current 2026-07-07 2h graph-soak recheck: the same current `e347fe.../fb70c341...` plus `d314e6cd...`
deployment also passed a 2h rclpy node/topic/service graph soak on domain 326. The board-side output/summary recorded
`GRAPH_CHURN_TARGET_ROUNDS=50000`, `GRAPH_CHURN_ROUNDS=19770`, `GRAPH_CHURN_PASS=19770`,
`GRAPH_CHURN_FAIL=0`, topics/services created and destroyed all `19770`, `GRAPH_CHURN_ELAPSED_SEC=7201.019`,
`GRAPH_CHURN_DURATION_MET=1`, `RMW_IMPLEMENTATION=rmw_mdds_cpp`, `ROS_DOMAIN_ID=326`, and the PASS marker
`rmw_mdds_board_graph_churn_fast_ok`. The host-side `hdc` exit 139 seen during polling is not treated as the
board result because the board summary was complete and no graph-churn process remained. This closes the
rclpy node/topic/service 2h graph-soak checkpoint only; concurrent graph churn, sanitizer, and performance
evidence remain open.

Current 2026-07-08 action 2h graph-soak recheck: the same current `e347fe.../fb70c341...` plus
`d314e6cd...` deployment also passed a 2h rclpy action graph soak on domain 327. The board-side output/summary
recorded `GRAPH_CHURN_TARGET_ROUNDS=50000`, `GRAPH_CHURN_ROUNDS=13719`, `GRAPH_CHURN_PASS=13719`,
`GRAPH_CHURN_FAIL=0`, `GRAPH_CHURN_ACTIONS_CREATED=13719`, `GRAPH_CHURN_ACTIONS_DESTROYED=13719`,
`GRAPH_CHURN_ELAPSED_SEC=7201.497`, `GRAPH_CHURN_DURATION_MET=1`, `RMW_IMPLEMENTATION=rmw_mdds_cpp`,
`ROS_DOMAIN_ID=327`, and the PASS marker `rmw_mdds_board_graph_churn_action_ok`. A direct board re-read after
completion showed no graph-churn process remained and `summary.txt` contained the same domain 327 counters.
Host-side `hdc shell` again returned 139 after valid output, so the result is judged from board-side summary and
runner output. This closes the action-specific 2h graph-soak checkpoint only; concurrent graph churn, sanitizer,
and performance evidence remain open.

Current 2026-07-07 action introspection CLI recheck: on the same `e347fe.../fb70c341...` plus `d314e6cd...`
current deployment, a cross-board Fibonacci action CLI gate passed on domain 320. The server was
`ros2 run action_tutorials_cpp fibonacci_action_server`; the client verified `ros2 action list -t`
(`/fibonacci [action_tutorials_interfaces/action/Fibonacci]`), `ros2 action type /fibonacci`
(`action_tutorials_interfaces/action/Fibonacci`), `ros2 action info /fibonacci -t`
(`/fibonacci_action_server [action_tutorials_interfaces/action/Fibonacci]`), and
`ros2 action send_goal /fibonacci action_tutorials_interfaces/action/Fibonacci "{order: 5}" --feedback`.
The board marker was
`RESULT|rmw_mdds_cross_board_action_cli|PASS|domain=320|ready=1|accepted=1|feedback=4|result=1|succeeded=1|sequence=5|type_rc=0|info_rc=0`.
This closes the action introspection CLI gap for the current deployment. Task 5.5 remains open because
dedicated action bag CLI, higher-count/concurrent/longer-soak large-message, broader security matrix, sanitizer, and
performance gates are still incomplete.

Current 2026-07-10 approved Plan A implementation refresh: the MDDS bridge API now supports remote-only
publishers, and `rmw_mdds_broker` uses that capability for graph and endpoint bridge publishers. Local live and
durable loopback is skipped for those publishers while remote MDDS fan-out, reliability, history, and SHM paths
remain active. The broker rejects an old bridge ABI before opening its listener. Protected-transport capability
preflight is side-effect free in the client, while authenticated plus encrypted activation now runs in the broker
that owns the transport before opening an isolated `.protected` socket.

The final host build passed package CTest 23/23 and `test_pubsub_inproc` 37/37. The final DSoftBus build passed,
and the four focused remote-only RK3588A regressions passed with `BOARD_RC=0`. Both boards carry
`librmw_mdds_cpp.so=52f6c349...`, `rmw_mdds_broker=f1de2fa9...`, loaded colcon-prefix bridge `53049581...`, and
protected probe `6aa06d13...`; running broker maps on both boards confirm that the loaded bridge is the
colcon-prefix artifact. The upstream board program matrix returned 0 for 16/16 programs, and focused typed plus
serialized ignore-local passed 2/2. Current cross-board topic passed in both directions, sequential service passed
10/10, Fibonacci action reached `SUCCEEDED` with sequence `0,1,1,2,3,5,8,13`, and the approved protected SROS2
harness passed signed policy, authenticated/encrypted activation, authorized delivery of 30 samples, and
unauthorized-publisher denial.

Task 5.5 remains open. A true current cross-board transient-local late joiner received zero cached samples; the
same test also failed on the old baseline, so it is not a Plan A regression but remains a functional blocker. The
umbrella delivery contract currently rejects the controlled broker auto-start `execl()` path, while the script,
SROS2-policy, zero-copy, and artifact contracts pass independently. The current hashes have not refreshed the
50-way service stress gate, and allocator, serialized-size, and three loaned-subscription upstream cases remain
skipped. Full-stack sanitizer, performance, broader security, action-bag, and remaining long-stability gates also
remain incomplete. Therefore this refresh must not be marked full-feature or production-ready.
