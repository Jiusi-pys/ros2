## Context

`complete-rmw-mdds-feature-closure` and `complete-rmw-mdds-zero-copy-security` are complete and validated. Together they prove a broad runtime surface: host conformance, board doctor, native MDDS pub/sub/service/action, gateway interop, dynamic take, events, type identity, script hygiene, fixed-size raw loaned-message zero-copy, explicit unsupported loaned shapes, and SROS2 policy positive/negative gates.

The active user goal is broader than those scoped changes. It asks for `rmw_mdds` and MDDS to be "perfect" for ROS 2, and the original note names "all ROS 2 middleware features, true zero-copy, dynamic types, security, RIHS/cross-RMW type identity, and clean upstream-ready delivery." The next problem is not one known failing function; it is missing acceptance structure for proving or rejecting a universal parity claim.

## Goals / Non-Goals

**Goals:**

- Convert the broad "perfect/all ROS 2 middleware features" objective into an executable acceptance matrix.
- Require RED tests or contracts before expanding support for any currently scoped or unsupported feature.
- Preserve existing completed evidence as prerequisites rather than rerunning unrelated discovery.
- Distinguish three outcomes for every surface: fully proven, explicitly unsupported with acceptable rationale, or incomplete.
- Require upstream-ready delivery evidence before marking the persistent goal complete.

**Non-Goals:**

- This change does not immediately implement every remaining feature.
- This change does not require pretending that unsupported message shapes are complete.
- This change does not require destructive cleanup, remote pushes, PR creation, or branch rewriting.
- This change does not archive prior OpenSpec changes unless separately requested.

## Decisions

### Decision: Treat "Perfect" As A Matrix, Not A Single Smoke Test

The final audit must enumerate ROS 2 RMW surfaces and evidence for each: initialization, node lifecycle, pub/sub, services, clients, actions, graph, wait sets, guard conditions, QoS compatibility, QoS events, content filters, dynamic message APIs, serialized APIs, loaned messages, security, type identity, network-flow metadata, logging, board runtime, and delivery state.

Alternative considered: accept the current delivery contract as enough. This is rejected because it proves a strong subset but cannot show universal parity for features outside its assertions.

### Decision: Allow Explicit Unsupported Results Only When They Are Contracted

Some ROS 2 surfaces may not be safely representable over MDDS today, especially unbounded or complex loaned-message shapes. Those can be accepted only if the final spec says they are out of scope or explicitly unsupported, tests prove they fail clearly, and the user accepts that boundary.

Alternative considered: require all shapes immediately. This is ideal for a literal parity claim, but it may turn the work into an open-ended MDDS memory-model redesign. The acceptance track should expose that decision rather than hide it.

### Decision: Security Parity Requires More Than Local Policy XML

Current SROS2 policy gates prove positive and negative authorization behavior. Full security parity also needs a decision on signed SROS2 artifacts, governance/permissions validation semantics, and transport protection. If those are non-goals, the final audit must say so explicitly.

Alternative considered: count local topic policy enforcement as full security. This is rejected because ROS 2 security users may expect signed permissions and encrypted/authenticated transport semantics.

### Decision: Delivery Completion Requires A Reproducible Handoff State

The local commits and clean tracked status are useful, but upstream-ready delivery still needs a chosen handoff action: local branch ready for push, archive/sync of OpenSpec specs, or PR/push path. The final audit must not confuse "committed locally" with "delivered upstream."

Alternative considered: mark complete with local commits only. This is rejected because the pasted note explicitly called out local-only delivery as a blocker.

## Risks / Trade-offs

- [Risk] The parity matrix can grow indefinitely if "all ROS 2 features" is not bounded.
  [Mitigation] Require each row to be classified as proven, accepted unsupported, or incomplete, and require user acceptance for any unsupported row.

- [Risk] Full security parity may require DSoftBus/MDDS transport changes outside this ROS 2 workspace.
  [Mitigation] Separate local policy enforcement from transport cryptography and record external dependencies before implementation.

- [Risk] General loaned-message zero-copy may need a new memory ownership model for bounded strings/sequences and dynamic types.
  [Mitigation] Require failing shape-specific contracts before expanding support beyond fixed-size raw loans.

- [Risk] Board evidence can be noisy because HDC may exit 139 after valid output.
  [Mitigation] Keep using explicit board markers and reject connection-failure banners instead of trusting host exit status alone.

## Migration Plan

1. Build a parity audit table from current `rmw_mdds_cpp` APIs, existing delivery contracts, upstream `test_rmw_implementation`, and board scripts.
2. Add RED contract tests for every row that is currently unproven but intended to become supported.
3. Implement missing behavior only after the RED gate exists.
4. For surfaces that remain out of scope, add explicit unsupported contracts and record the accepted boundary.
5. Re-run host delivery contracts, upstream RMW tests, and affected board lanes.
6. Decide the delivery endpoint: local-only handoff, OpenSpec archive, push, PR, or Gerrit submission.

Rollback is to keep the two completed scoped changes and leave this final parity change open as the record that universal completion is still unproven.

## Open Questions

- Must the final definition of "perfect" include zero-copy for unbounded strings/sequences and dynamic message data, or is explicit unsupported behavior acceptable?
- Must security parity include signed SROS2 artifact validation and transport crypto, or is local policy enforcement plus fail-closed behavior acceptable?
- Is upstream-ready delivery satisfied by local commits and clean status, or does it require push/PR/archive in this workflow?
