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
protected-policy evidence; board-visible MDDS/DSoftBus authenticated/encrypted transport is proven by the
2026-07-04 protected SROS2 board harness evidence below.

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
- [x] 4.4 Run affected native MDDS, cross-RMW gateway, zero-copy, and security board lanes with explicit PASS markers.
- [x] 4.5 Decide and execute the delivery endpoint: local handoff only, OpenSpec archive, push, PR, or Gerrit submission.

Fresh host delivery evidence on 2026-07-03: after the security, broker network-flow, dynamic-loaned-message,
and host CLI harness robustness changes, `ohos/test_rmw_mdds_delivery_contracts.sh` emits
`rmw_mdds_delivery_contracts_ok`, including package CTest `23/23`, upstream `test_rmw_implementation`
`16/16`, SROS2 local policy, zero-copy, artifact, type-description, and host CLI
pub/sub/service/action/params/lifecycle/graph/QoS/transient-local/message-info PASS markers. During an earlier
rerun, the params/lifecycle probes exposed daemon/discovery timing misses; the harness now uses daemon-free
discovery and state polling for those probes, and the final full delivery rerun passed.

Signed-security host refresh on 2026-07-03: after OpenSSL-backed signed protected SROS2 artifact validation
landed, the full `rmw_mdds_cpp` build succeeded with the workspace Python path exported,
`ctest --test-dir build/rmw_mdds_cpp --output-on-failure` passed 22/22 with
`LD_LIBRARY_PATH=/home/kaihong/ros2/build/rmw_mdds_cpp:/home/kaihong/ros2/install/lib`, and
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
3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00` emitted
`RESULT|rmw_mdds_deploy|PASS` for both boards with `librmw_mdds_cpp.so` sha
`45b901717a47ce9b7d6124131df3f6a10f952b86581160a5f5e7375de1b9bdbb` and broker sha
`85f5c7afc3c51f4bd98cad1c2e8f1edd3df968e6124b3c6e2435feac3680670f`; it also emitted
`RESULT|mdds_bridge_deploy|PASS` for both boards with bridge sha
`82e49a4c8e01df25a3980376ff5c24290bf4cded1c08a59c36d047c547e9e452`.

Board runtime evidence on 2026-07-03: `ohos/tools/run_cross_board_rmw_mdds_m2m.sh
3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00` emitted
`M2M_SUMMARY pass=3 fail=0` and `cross_board_rmw_mdds_m2m_ok` for native MDDS pub/sub,
service, and Fibonacci action. `ohos/tools/run_cross_board_rmw_mdds_matrix.sh
3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00` emitted
`MATRIX_SUMMARY pass=8 fail=0` and `cross_board_rmw_mdds_matrix_ok` for cross-RMW gateway
String, PoseStamped, best-effort QoS, and TFMessage lanes in both directions. The zero-copy
gate remains the host/static `RESULT|rmw_mdds_zero_copy_contracts|PASS` from the delivery
contract; board runtime preservation after the zero-copy/security changes is covered by the
native and gateway board lanes above.

Board SROS2 policy evidence on 2026-07-03: the board harness was aligned with the supported local
XML topic-policy scope (`NONE` protection kinds). It first failed against the old protected
`ENCRYPT` governance, which is now intentionally rejected by `rmw_mdds_cpp`; after the harness fix,
`ohos/tools/run_cross_board_rmw_mdds_sros2_policy.sh
3e01ff55454d202020104433991c3b00 3e01ff55454d202020104033bf453b00` emitted
`RESULT|board_sros2_authorized_pubsub|PASS|topic=/mdds_sros2_allowed|received=20`,
`RESULT|board_sros2_unauthorized_publish|PASS|topic=/mdds_sros2_forbidden|denied`, and
`cross_board_rmw_mdds_sros2_policy_ok`. Host signed/protected SROS2 policy behavior is now covered by the
full-parity host contract.

Final board runtime evidence on 2026-07-04: `./ohos/colcon_rk3588a.sh rmw_mdds_cpp` rebuilt the OHOS
overlay with target OpenSSL enabled, `llvm-readelf` showed `NEEDED libcrypto_openssl.z.so`, and
`ohos/tools/deploy_rmw_mdds_delta.sh 3e01ff55454d202020104033bf453b00
3e01ff55454d202020104433991c3b00` deployed `librmw_mdds_cpp.so` sha
`b08e129c4086b3593cf1271da6a2e2acc853a7c6b98e2f6f265995d081dbf1b1`, broker sha
`83b93da71c72133d89017c52cd95e5dc06c2d661626123bb94553eaa3d8be970`, and bridge sha
`771cec930859b407dc7c48aa94a0fb4b342478bb6403abaf72974fbb2d86e9c1` to both boards. After restarting
`softbus_server` to refresh LNN binding, native M2M emitted `M2M_SUMMARY pass=3 fail=0` and
`cross_board_rmw_mdds_m2m_ok`; the gateway matrix emitted `MATRIX_SUMMARY pass=8 fail=0` and
`cross_board_rmw_mdds_matrix_ok`; gateway service/action/params/lifecycle emitted PASS markers for
`sum=42`, Fibonacci `sequence=0,1,1,2,3,5,8,13`, parameter value `4242`, and lifecycle transition to
inactive. The protected SROS2 board harness emitted signed-policy PASS and
`RESULT|board_sros2_protected_transport|PASS|authenticated=1|encrypted=1` on both boards, authorized
protected pub/sub PASS with `received=20`, unauthorized protected publish PASS with `denied`, and
`cross_board_rmw_mdds_sros2_protected_ok`.

Delivery endpoint decision on 2026-07-04: no push, PR, Gerrit submission, or OpenSpec archive was requested,
so the executed endpoint is a local two-repo handoff. The DSoftBus/MDDS side is committed on
`/home/kaihong/M-DDS/OpenHarmony_lyl/foundation/communication/dsoftbus` branch `mdds-claude` at
`ba9a5f605` (`<feat><29168><MDDS零拷贝loaned samples与桥接验证><source:int;none>`). The ROS 2/rmw_mdds side is committed on
`/home/kaihong/ros2` branch `jazzy-ubuntu-20.04` at `86ab375` (`feat(rmw_mdds): complete MDDS parity board
closure`), with this OpenSpec evidence update committed separately. This resolves the local delivery endpoint
for the current request without touching remotes.

## 5. Final Completion Decision

- [x] 5.1 Update the parity matrix with all final command evidence and board markers.
- [x] 5.2 Verify tracked status is clean and generated/scratch artifacts are ignored or intentionally tracked.
- [x] 5.3 Validate all active OpenSpec changes with `openspec validate --strict`.
- [x] 5.4 Decide whether accepted unsupported rows still satisfy the user's "perfect/all ROS 2 middleware features" objective.
- [x] 5.5 Mark the persistent goal complete only if every required row is proven or explicitly accepted out of scope and no required delivery work remains.

Final audit snapshot on 2026-07-04: current technical parity rows are proven by host plus RK3588/KaihongOS
board evidence, the DSoftBus/MDDS memory-model work is committed locally, the ROS 2/rmw_mdds implementation is
committed locally, and the selected delivery endpoint is local handoff because no remote/archive action was
requested. After this OpenSpec evidence update is committed and `openspec validate --changes --strict` passes,
no required delivery work remains for the current local handoff endpoint.
