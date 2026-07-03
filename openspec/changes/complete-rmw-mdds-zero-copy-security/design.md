## Context

The completed `complete-rmw-mdds-feature-closure` change proves a broad runtime surface: host conformance, board doctor, native MDDS pub/sub/service/action, gateway interop, dynamic take, events, type identity, script hygiene, and runtime-selectable overlay artifacts. It also records two explicit non-completion findings for the broader "perfect/all ROS 2 features" goal.

Current loaned-message code borrows bridge-owned storage, but `rmw_publish_loaned_message` still serializes the ROS typed object with `MessageAdapter::EncodeMddsIntoBuffer`, and bridge loaned take still decodes payload bytes with `MessageAdapter::DecodeMdds`. Current security code copies/finalizes `rmw_security_options`, stores enclaves, and exposes type identity, but no SROS2 policy enforcement, keystore handling, DDS security plugin integration, or secure MDDS transport enforcement path has been found.

## Goals / Non-Goals

**Goals:**

- Define a strict, testable zero-copy completion boundary for loaned publish and loaned take.
- Define a strict, testable security completion boundary for SROS2/security-required runtime configuration.
- Require RED-GREEN tests before changing either behavior.
- Preserve board runtime verification as part of the final acceptance gate.
- Keep the final goal open until both blockers have implementation evidence or a clearly documented non-completion decision approved by the user.

**Non-Goals:**

- This change does not re-prove the already completed host, board, gateway, event, dynamic take, or artifact gates except where they are affected by zero-copy/security work.
- This change does not treat hygiene checks, enclave propagation, or RIHS/type identity alone as full ROS security support.
- This change does not require destructive cleanup, commit creation, Gerrit upload, GitHub PR creation, or remote push unless separately requested.

## Decisions

### Decision: Treat Current Loaned Support As Bounded-Copy Compatibility

The current implementation is useful and should remain covered, but it is not enough for a true zero-copy claim because it still converts between ROS typed objects and MDDS payload bytes. The new zero-copy acceptance gate must prove that the loaned path avoids `EncodeMddsIntoBuffer`, `DecodeMdds`, and payload buffer copy fallback for loaned publish/take.

Alternative considered: declare bridge-backed loaned storage as "zero-copy enough." This is rejected because the broader goal asks for all ROS 2 features, and ROS 2 loaned-message semantics are specifically about avoiding unnecessary message copies across publish/take paths.

### Decision: Make Zero-Copy Measurable Before Optimizing

The first implementation step is instrumentation or fake-backend counters that fail against the current path by detecting serialization, deserialization, and copy fallback. Implementation can then move toward shared MDDS loan/sample ownership, typed view construction, or a carefully documented unsupported result for message shapes that cannot be represented safely.

Alternative considered: rewrite the bridge/backend loaned path first. This is rejected because there is already a working compatibility path, and changing it without a failing contract risks losing runtime behavior while still not proving zero-copy semantics.

### Decision: Fail Closed For Required Security Until Enforcement Exists

If the runtime is configured to require ROS security but `rmw_mdds_cpp` cannot enforce SROS2 policy and secure transport, it must fail explicitly instead of silently running insecurely. Full support may later map ROS security artifacts to MDDS/DDS security mechanisms, but the immediate requirement is to stop ambiguous success.

Alternative considered: keep copying `rmw_security_options` and rely on upper layers to know security is not enforced. This is rejected because it lets a "security required" deployment appear to run while the transport remains unenforced.

## Risks / Trade-offs

- [Risk] True zero-copy may require MDDS bridge/backend API changes outside `rmw_mdds_cpp`.
  [Mitigation] Keep an architecture spike task that identifies the exact MDDS loan/sample ownership API and records any DSoftBus/MDDS dependency before code changes.

- [Risk] Some ROS message types may not be safely constructible as direct views over MDDS shared memory.
  [Mitigation] Split acceptance by message shape: fixed-size POD messages first, then bounded strings/sequences, then unsupported dynamic shapes with explicit capability reporting.

- [Risk] Full SROS2 support may require credentials, policy parsing, governance/permissions validation, and transport crypto not currently present in this workspace.
  [Mitigation] Require fail-closed behavior for security-required mode as the first milestone, and only claim support after end-to-end positive and negative security tests pass.

- [Risk] Board evidence can be noisy because HDC may return exit 139 after valid output.
  [Mitigation] Reuse explicit board markers and reject known connection failures while preserving marker-based success checks.

## Migration Plan

1. Add failing host tests or script contracts for zero-copy counters and security-required fail-closed behavior.
2. Implement the smallest behavior change that makes the failing tests pass while preserving existing runtime closure tests.
3. Run focused host tests, then delivery contracts, then board runtime lanes affected by the change.
4. Update OpenSpec tasks with command evidence and exact non-completion findings if a blocker remains.
5. Keep the active goal open until both zero-copy and security requirements are either implemented with evidence or explicitly accepted as out of scope by the user.

Rollback is to keep the current bounded-copy loaned compatibility path and current security option propagation behavior, but the failing tests/spec tasks must remain as evidence that the broader "perfect" goal is still incomplete.

## Open Questions

- Should the first true zero-copy milestone target only fixed-size ROS messages, or must it cover strings/sequences immediately?
- Should security-required mode initially fail closed, or should this track implement positive SROS2 policy enforcement before any handoff?
- Does the MDDS bridge expose sufficient shared-memory ownership metadata to return the same underlying loan across publisher, transport, and subscriber without serialization?
