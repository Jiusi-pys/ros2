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
diagnostics. Current implementation still fails at the old protected-governance unsupported diagnostic before
signature, identity, or authenticated-transport semantics are implemented.

Host GREEN evidence on 2026-07-03: after adding OpenSSL-backed RSA/SHA-256 detached signature validation,
certificate trust-anchor checks, identity certificate binding, and an explicit authenticated/encrypted
protected-transport gate, `bash
ohos/test_rmw_mdds_full_parity_red_contracts.sh` emits
`RESULT|rmw_mdds_full_parity_signed_security|PASS`. This proves host signed-artifact semantics and
fail-closed protected-policy handling, but not RK3588/KaihongOS MDDS/DSoftBus protected transport activation.

Board harness GREEN evidence on 2026-07-04: `ohos/test_rmw_mdds_delivery_contracts.sh` now statically
requires `ohos/tools/run_cross_board_rmw_mdds_sros2_protected.sh`, and that board harness generates signed
governance/permissions/identity artifacts, deploys them to both boards, validates signed policy, activates
authenticated/encrypted protected transport, proves authorized protected pub/sub, and proves unauthorized
publish denial. After refreshing the OHOS overlay and restarting `softbus_server` on both boards,
`HDC_BIN=hdc ohos/tools/run_cross_board_rmw_mdds_sros2_protected.sh
3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00 109` emitted
`RESULT|board_sros2_signed_policy|PASS` and
`RESULT|board_sros2_protected_transport|PASS|authenticated=1|encrypted=1` on both boards,
`RESULT|board_sros2_protected_authorized_pubsub|PASS|topic=/mdds_sros2_protected_allowed|received=20`,
`RESULT|board_sros2_protected_unauthorized_publish|PASS|topic=/mdds_sros2_protected_forbidden|denied`,
and `cross_board_rmw_mdds_sros2_protected_ok`.

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
`ohos/test_rmw_mdds_sros2_policy_contracts.sh`. Board evidence now closes the remaining transport proof.

Host activation evidence on 2026-07-03: `BridgeBackend` now binds optional
`MddsBridgeActivateProtectedTransport(uint32_t)`, and `LoadSecurityPolicy()` attempts bridge protected-lane
activation before accepting signed protected governance when the env-only host override is absent. TDD RED was
`./build/rmw_mdds_cpp/test_pubsub_inproc
--gtest_filter=RmwMddsPubSub.DISABLED_FullParitySros2ActivatesBridgeProtectedTransport
--gtest_also_run_disabled_tests --gtest_color=no` failing with the missing authenticated/encrypted transport
diagnostic and fake bridge activation count `0`; GREEN passes the same test and the full
`RmwMddsPubSub.DISABLED_FullParitySros2*` group passes 6/6. The host path proves bridge activation semantics;
RK3588/KaihongOS authenticated/encrypted transport proof is covered by the 2026-07-04 board evidence in tasks
3.4 and 6.3.

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
ABI/fail-closed activation hook plus build proof is now paired with the 2026-07-04 board protected-SROS2
traffic evidence above.

## 6. Verification And Final Acceptance Sync

- [x] 6.1 Run focused host unit tests for dynamic loans and signed/protected SROS2 behavior.
- [x] 6.2 Run `ohos/test_rmw_mdds_delivery_contracts.sh` and affected full-parity contracts.
- [x] 6.3 Rebuild the OHOS overlay, deploy to RK3588/KaihongOS boards, and run affected native, gateway, and protected SROS2 board lanes.
- [x] 6.4 Update `complete-rmw-mdds-full-parity-acceptance/parity-matrix.md` and tasks with exact command evidence.
- [x] 6.5 Resolve the delivery endpoint with the user or execute the requested local/archive/push/PR/Gerrit path before marking the persistent goal complete.

Delivery endpoint resolution on 2026-07-04: the executed endpoint is local handoff because no push, PR,
Gerrit submission, or OpenSpec archive was requested. The paired local commits are DSoftBus/MDDS
`ba9a5f605` on `mdds-claude` and ROS 2/rmw_mdds implementation `86ab375` on `jazzy-ubuntu-20.04`; this
OpenSpec evidence update records the final local handoff state.

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
3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00` emitted deploy PASS markers on both
boards with `librmw_mdds_cpp.so` sha `b08e129c4086b3593cf1271da6a2e2acc853a7c6b98e2f6f265995d081dbf1b1`,
broker sha `83b93da71c72133d89017c52cd95e5dc06c2d661626123bb94553eaa3d8be970`, and bridge sha
`771cec930859b407dc7c48aa94a0fb4b342478bb6403abaf72974fbb2d86e9c1`.

Board lane evidence on 2026-07-04: after restarting `softbus_server` on both boards to refresh LNN binding,
`HDC_BIN=hdc ohos/tools/run_cross_board_rmw_mdds_m2m.sh
3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00 102` emitted
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
`RESULT|lifecycle_mdds_client_to_fastrtps_node|PASS`. Protected SROS2 board verification passed as recorded
under task 3.4.

Final host refresh on 2026-07-04: with
`PYTHONPATH=/home/kaihong/ros2/install/lib/python3.12/site-packages` and
`LD_LIBRARY_PATH=/home/kaihong/ros2/build/rmw_mdds_cpp:/home/kaihong/ros2/install/lib`,
`cmake --build build/rmw_mdds_cpp -j4` passed, `ctest --test-dir build/rmw_mdds_cpp --output-on-failure`
passed 23/23, `ohos/test_rmw_mdds_full_parity_red_contracts.sh` emitted PASS for loaned shapes, signed
security, and broker network-flow, `ohos/test_rmw_mdds_delivery_contracts.sh` emitted
`rmw_mdds_delivery_contracts_ok`, and `ohos/test_rmw_mdds_artifact_contracts.sh` emitted
`rmw_mdds_artifact_contracts_ok`.
