## Context

`complete-rmw-mdds-full-parity-acceptance` shows that the current implementation is strong for scoped ROS 2 runtime behavior but still incomplete for the user's broad "perfect/all ROS 2 middleware features" objective. The remaining technical blockers are not independent one-line fixes:

- Generalized loaned messages are intentionally rejected once the message contains unbounded strings, sequences, arrays, nested dynamic members, or other non-flat storage. Relaxing the current guard would not create true zero-copy because generated C++ `init_function` code constructs `std::string` and `std::vector` members with default heap-backed storage outside the MDDS bridge loan.
- The current bridge loan record tracks the transport loan pointer, raw data pointer, and capacity. It does not expose an allocator, arena, segment table, or ownership API that generated dynamic members can use.
- SROS2 local XML policy enforcement is proven for unprotected governance, but protected signed DDS Security semantics are still rejected or incomplete. Full security parity needs signed governance/permissions validation and an authenticated/encrypted MDDS/DSoftBus protected transport lane.

## Goals / Non-Goals

**Goals:**

- Add RED tests before every production behavior change for dynamic generated-message loans and protected SROS2 security.
- Define a generator/typesupport and MDDS memory model that can back generated dynamic members from MDDS bridge loan storage without hidden serialization, copy, decode, or default heap fallback.
- Implement signed SROS2 governance and permissions validation with tamper rejection and fail-closed behavior.
- Activate an authenticated/encrypted MDDS/DSoftBus protected transport mode when signed protected governance requires it.
- Re-run host and RK3588/KaihongOS evidence and update the full-parity acceptance matrix only after the new behavior is proven.

**Non-Goals:**

- Do not claim ordinary generated C++ `std_msgs::msg::*` objects are loan-safe by casting or by serializing into an unrelated loan buffer.
- Do not mark dynamic loaned-message shapes supported unless dynamic member storage is bridge/MDDS-backed for the lifetime of the loan.
- Do not treat local XML topic-policy enforcement as full DDS Security parity for protected governance.
- Do not push, open a PR, or archive OpenSpec changes unless the user requests that delivery endpoint.

## Decisions

### Decision: Make Dynamic Loans Generator/Typesupport-Aware

Dynamic C++ generated messages cannot become true MDDS-backed loaned messages through `rmw_mdds_cpp` alone while their dynamic members are ordinary `std::string` and `std::vector` storage. The implementation shall add a loan-aware generated-message/type-support surface or a compatible generated loan-view contract so string and sequence storage can be placed inside the MDDS bridge loan.

Alternative considered: allow the current `MessageAdapter::ConstructMessageInPlace()` path for dynamic messages and serialize into the bridge loan during publish. This is rejected because it keeps dynamic storage outside MDDS and violates the existing RED contract intent.

Alternative considered: return `std_msgs::msg::String_<BridgeAllocator>` where callers expect `std_msgs::msg::String`. This is rejected because it is not ABI-compatible with the default generated C++ message type passed through the RMW API.

### Decision: Add An MDDS Loan Arena With Explicit Lifetime

The bridge loan record shall grow from a single raw buffer pointer into a loan arena/segment model that can allocate message headers, string bytes, sequence elements, and nested dynamic storage from one MDDS-owned loan lifetime. Returned loaned messages shall free all arena storage when `rmw_return_loaned_message_from_publisher()` or the matching subscription return path completes.

Alternative considered: use per-member heap allocations and record them beside the loan. This is rejected for parity because it reduces the bridge loan to a bookkeeping token rather than true MDDS-backed dynamic storage.

### Decision: Keep Fixed-Size Raw Loans As The First Stable Path

Existing fixed-size scalar/raw loan behavior remains valid and shall not regress. New dynamic support shall be added behind separate capability checks so fixed-size loan support stays green while the dynamic implementation grows through RED-to-GREEN slices.

Alternative considered: replace the existing raw path wholesale. This is rejected because it risks regressing a proven zero-copy surface before the larger memory model is ready.

### Decision: Treat Protected SROS2 As Signed Artifacts Plus Protected Transport

Protected governance shall only load after governance and permissions artifacts are signed, validated, and bound to the configured identity/certificate material. If protected governance requires authenticated or encrypted transport, `rmw_mdds_cpp` shall not silently downgrade to local policy checks; it must activate an authenticated/encrypted MDDS/DSoftBus transport lane or fail closed.

Alternative considered: accept signed XML policy but keep transport unprotected. This is rejected because it can pass artifact checks while violating the runtime protection semantics users expect from protected SROS2 governance.

### Decision: Host Contracts Are Necessary But Not Sufficient

Host unit tests and script contracts shall prove API behavior, tamper rejection, and no-copy invariants. The protected transport claim still requires RK3588/KaihongOS board evidence showing the protected lane is enabled and communication succeeds only through authenticated/encrypted MDDS/DSoftBus transport.

Alternative considered: close security parity with host XML tests only. This is rejected because the previous audit already proved local policy checks and still found transport protection incomplete.

## Risks / Trade-offs

- [Risk] Loan-aware generated C++ support may require changes outside `rmw_mdds_cpp`.
  [Mitigation] Keep the first implementation slice narrow and require the design to identify exact generator/typesupport ownership before enabling dynamic loan advertisement.

- [Risk] The ROS 2 RMW loaned-message API expects a pointer to the generated message type, while allocator-specialized generated C++ types are distinct types.
  [Mitigation] Do not rely on allocator-specialized type substitution unless the generated type support provides an ABI-safe loan-view or construction hook.

- [Risk] DSoftBus protected transport APIs may not expose every DDS Security property directly.
  [Mitigation] Define the MDDS/DSoftBus contract in terms of authenticated peer identity, encrypted payload/session, fail-closed setup, and board-visible proof rather than pretending to implement byte-for-byte DDS Security wire format.

- [Risk] Large cross-repository changes can destabilize proven host and board lanes.
  [Mitigation] Keep each slice behind RED tests, preserve fixed-size raw loan tests, and re-run delivery contracts after every behavior change.

## Migration Plan

1. Add RED unit tests and script contracts for dynamic string, sequence, nested dynamic, and filtered loaned-message shapes. The tests must fail because loan-aware generated storage or MDDS arena support is missing, not because of typo/setup errors.
2. Add RED contracts for signed protected governance/permissions acceptance, unsigned/tampered artifact rejection, identity/cert mismatch rejection, and protected transport activation.
3. Implement the smallest loan-aware memory-model slice: fixed bridge loan arena metadata and one dynamic generated shape whose dynamic storage is demonstrably arena-backed.
4. Extend the generator/typesupport surface for additional dynamic shapes only after each shape has a failing test.
5. Implement signed SROS2 artifact validation and protected transport activation, preserving the existing unprotected local XML policy path.
6. Rebuild host artifacts, run focused tests plus `ohos/test_rmw_mdds_delivery_contracts.sh`, then rebuild/deploy to boards and run protected SROS2 and affected native/gateway lanes.
7. Update `complete-rmw-mdds-full-parity-acceptance/parity-matrix.md` and tasks with exact evidence, then decide the final delivery endpoint.

Rollback is to keep fixed-size raw loans and unprotected local XML SROS2 policy green, leave dynamic loans/protected security disabled with explicit diagnostics, and keep the final full-parity acceptance change open.

## Open Questions

- Which repository owns the generated loan-aware C++ message/type-support hook for Jazzy: a local `rmw_mdds` generator extension, `rosidl_typesupport_*` integration, or an MDDS-specific generated loan view?
- Which MDDS/DSoftBus API is the authoritative source for board-visible authenticated and encrypted session proof?
- Does the final delivery endpoint require a local handoff only, OpenSpec archive, push/PR, or Gerrit submission after implementation evidence is green?
