## 1. OpenSpec Baseline

- [x] 1.1 Create and validate the proposal, design, and spec for the approved generator/typesupport, MDDS memory-model, signed SROS2, and authenticated transport scope.
- [ ] 1.2 Re-run `openspec validate implement-rmw-mdds-perfect-parity --strict` and `openspec validate --changes --strict` after each task status update.

## 2. RED Contracts For Dynamic Loaned Messages

- [ ] 2.1 Add a failing host unit test for unbounded string loan publish that requires string bytes to be MDDS bridge-loan backed.
- [ ] 2.2 Add a failing host unit test for dynamic sequence loan publish that requires sequence element storage to be MDDS bridge-loan backed.
- [ ] 2.3 Add a failing host unit test for nested dynamic loan storage and lifetime.
- [ ] 2.4 Add or update a script contract that runs the dynamic loan tests and emits explicit RED/GREEN markers for generalized loaned-message shapes.

## 3. RED Contracts For Signed And Protected SROS2

- [ ] 3.1 Add a failing host test that accepts valid signed protected governance and permissions artifacts only after signature and identity validation.
- [ ] 3.2 Add failing host tests that reject unsigned protected governance, tampered signed permissions, and identity/certificate mismatch before endpoint creation.
- [ ] 3.3 Add a failing protected-transport contract that rejects signed protected policy when authenticated/encrypted MDDS/DSoftBus transport cannot be established.
- [ ] 3.4 Add or update a board harness that emits signed-policy, authorized protected traffic, denied protected traffic, and authenticated/encrypted transport markers.

## 4. Generator/Typesupport And MDDS Memory Model

- [ ] 4.1 Identify the exact generated-message or type-support extension point that can construct ABI-safe loan-aware dynamic message storage.
- [ ] 4.2 Add an MDDS bridge loan arena or segment ownership API for dynamic member storage and lifetime tracking.
- [ ] 4.3 Implement the smallest unbounded string dynamic-loan GREEN slice without serialize/copy/default-heap fallback.
- [ ] 4.4 Extend the loan-aware path to dynamic sequences and nested dynamic members only after their RED tests fail for the expected reason.
- [ ] 4.5 Preserve fixed-size raw bridge loan behavior and zero-copy contracts while dynamic loan support is added.

## 5. Signed SROS2 And Authenticated Transport Implementation

- [ ] 5.1 Implement signed governance and permissions artifact loading, signature validation, certificate-chain validation, and identity binding.
- [ ] 5.2 Preserve the existing unprotected local XML policy path for `NONE` protection kinds.
- [ ] 5.3 Implement fail-closed protected-policy behavior for unsigned, tampered, mismatched, or unreadable artifacts.
- [ ] 5.4 Activate authenticated/encrypted MDDS/DSoftBus transport for valid protected governance and fail closed if the protected lane cannot be established.

## 6. Verification And Final Acceptance Sync

- [ ] 6.1 Run focused host unit tests for dynamic loans and signed/protected SROS2 behavior.
- [ ] 6.2 Run `ohos/test_rmw_mdds_delivery_contracts.sh` and affected full-parity contracts.
- [ ] 6.3 Rebuild the OHOS overlay, deploy to RK3588/KaihongOS boards, and run affected native, gateway, and protected SROS2 board lanes.
- [ ] 6.4 Update `complete-rmw-mdds-full-parity-acceptance/parity-matrix.md` and tasks with exact command evidence.
- [ ] 6.5 Resolve the delivery endpoint with the user or execute the requested local/archive/push/PR/Gerrit path before marking the persistent goal complete.
