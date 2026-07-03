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
- [ ] 3.4 Add or update a board harness that emits signed-policy, authorized protected traffic, denied protected traffic, and authenticated/encrypted transport markers.

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

## 4. Generator/Typesupport And MDDS Memory Model

- [x] 4.1 Identify the exact generated-message or type-support extension point that can construct ABI-safe loan-aware dynamic message storage.
- [x] 4.2 Add an MDDS bridge loan arena or segment ownership API for dynamic member storage and lifetime tracking.
- [ ] 4.3 Implement the smallest unbounded string dynamic-loan GREEN slice without serialize/copy/default-heap fallback.
- [ ] 4.4 Extend the loan-aware path to dynamic sequences and nested dynamic members only after their RED tests fail for the expected reason.
- [ ] 4.5 Preserve fixed-size raw bridge loan behavior and zero-copy contracts while dynamic loan support is added.

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

## 5. Signed SROS2 And Authenticated Transport Implementation

- [x] 5.1 Implement signed governance and permissions artifact loading, signature validation, certificate-chain validation, and identity binding.
- [x] 5.2 Preserve the existing unprotected local XML policy path for `NONE` protection kinds.
- [x] 5.3 Implement fail-closed protected-policy behavior for unsigned, tampered, mismatched, or unreadable artifacts.
- [ ] 5.4 Activate authenticated/encrypted MDDS/DSoftBus transport for valid protected governance and fail closed if the protected lane cannot be established.

Implementation evidence on 2026-07-03: `LoadSecurityPolicy()` now accepts protected governance only after
`governance.xml.sig` and `permissions.xml.sig` verify against a trusted `permissions_ca.cert.pem`,
`identity.pem` validates against `identity_ca.cert.pem`, the identity certificate common name matches the
permissions `subject_name`, and the protected transport gate reports authenticated plus encrypted transport.
The `NONE` protection local XML policy path remains green through
`ohos/test_rmw_mdds_sros2_policy_contracts.sh`. Task 5.4 remains open because the current host gate does not
yet activate or prove board-side MDDS/DSoftBus authenticated/encrypted transport.

## 6. Verification And Final Acceptance Sync

- [ ] 6.1 Run focused host unit tests for dynamic loans and signed/protected SROS2 behavior.
- [ ] 6.2 Run `ohos/test_rmw_mdds_delivery_contracts.sh` and affected full-parity contracts.
- [ ] 6.3 Rebuild the OHOS overlay, deploy to RK3588/KaihongOS boards, and run affected native, gateway, and protected SROS2 board lanes.
- [ ] 6.4 Update `complete-rmw-mdds-full-parity-acceptance/parity-matrix.md` and tasks with exact command evidence.
- [ ] 6.5 Resolve the delivery endpoint with the user or execute the requested local/archive/push/PR/Gerrit path before marking the persistent goal complete.

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
`rmw_mdds_artifact_contracts_ok`; installed `librmw_mdds_cpp.so` sha is
`f2c8f6708594bed141aa6d185f4f07c7723d2d4093e1cca5e5af426acadae287`.
