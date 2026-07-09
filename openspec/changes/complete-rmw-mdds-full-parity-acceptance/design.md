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

The local commits and clean tracked status are useful only when the final audit records the selected handoff
action: local branch handoff, archive/sync of OpenSpec specs, or PR/push path. The final audit must not confuse
"committed locally" with "delivered upstream"; if no remote or archive action is requested, the selected
endpoint is an explicit local handoff with commit hashes.

Alternative considered: mark complete with an uncommitted local workspace. This is rejected because the pasted
note explicitly called out local-only dirty delivery as a blocker.

### Decision: Broker Bridge Publishers Are Remote-Only

The RK3588A `test_rmw_implementation` failures for typed and serialized
`ignore_local_publications` are caused by two delivery paths for one broker-local publish. The broker first sends
the sample directly to matching local IPC clients and correctly excludes subscriptions whose non-zero
`local_context_id` matches the publisher. It then publishes the same payload through MDDS so other devices can
receive it. A normal MDDS publisher also loops that sample back to every same-process bridge subscriber. The
broker's payload-based `LocalBridgeEcho` record is consumed by the first bridge callback, so a second subscription
can receive the loopback as though it came from another device. Payload matching can also suppress a legitimate
remote sample with identical bytes.

The approved architecture makes every publisher owned by `IpcBroker` remote-only. Broker-local IPC delivery is
the single authoritative local path, while MDDS remains responsible for remote transport. Direct/non-broker RMW
publishers and existing bridge API callers retain normal MDDS local loopback.

The coordinated interface is:

- Add `MddsCreatePublisherEx(node, config, publisherFlags)` and define
  `MDDS_PUBLISHER_FLAG_REMOTE_ONLY` as bit 0. Keep `MddsTopicConfig` and `MddsCreatePublisher` unchanged;
  the legacy function is the zero-flag path. This preserves both source and binary compatibility for existing
  callers instead of extending an unversioned public structure. Unknown flag bits reject creation.
- Store the validated flag in `MddsPublisher`. `MddsTopicManagerPublishEx` skips only
  `MddsTopicRegistryDeliverToLocal` for a remote-only publisher. History insertion, reliable-writer state,
  discovery, remote endpoint fan-out, native-discovery fan-out, and remote SHM loan descriptors remain active.
- `ReplayLocalHistoryToSub` also skips remote-only publishers. This prevents a late local
  `TRANSIENT_LOCAL` bridge subscriber from reintroducing the same loopback through cached history; remote
  late-joiner replay remains enabled.
- Add `MddsBridgeCreatePublisherQosEx(..., flags)` and
  `MDDS_BRIDGE_PUBLISHER_FLAG_REMOTE_ONLY` as bit 0. Keep `MddsBridgeCreatePublisherQos` unchanged and implement
  it as the zero-flag compatibility path. Unknown bridge flag bits reject creation.
- Extend `BridgeBackend::CreatePublisher` with an explicit publisher mode. Only `IpcBroker` endpoint and graph
  publishers request remote-only mode. Direct/non-broker RMW publisher and service callers, plus gateway callers,
  keep the default mode.
- Load the extended bridge symbol as a capability. A broker configured to use a bridge must fail startup with a
  clear version-mismatch error when remote-only publisher creation is unavailable; it must not silently run the
  known incorrect loopback path. Local-only operation with no bridge remains valid.
- Remove the broker's payload-based `LocalBridgeEcho` remember/consume/forget state. With remote-only broker
  publishers there is no same-broker bridge echo to consume, and removing the heuristic prevents false suppression
  of byte-identical remote samples.

This option applies equally to ordinary and loaned publication because both enter
`MddsTopicManagerPublishEx`. A remote-only loan therefore acquires no local loan references while retaining the
remote SHM descriptor and copy-based remote fan-out behavior.

Alternatives rejected:

- Per-target echo accounting is smaller but still identifies origin from payload bytes and a timeout. It cannot
  be collision-free for identical remote data, especially when BEST_EFFORT transport has no usable writer GUID.
- Adding source metadata to every MDDS receive entry and bridge callback is semantically general, but it changes
  a wider ABI and all local/remote receive paths. It remains a possible future bridge-routing enhancement, not the
  narrow fix for a broker that already owns an authoritative local IPC path.

The required RED/GREEN evidence is:

1. A fake-bridge broker test registers the normal subscription first and the same-context ignore-local
   subscription second, proving the current single-consumer echo guard fails deterministically.
2. The same test proves a local publish reaches the normal subscription exactly once, reaches the ignore-local
   subscription zero times, and a separately injected remote bridge sample reaches both subscriptions.
3. MDDS unit tests prove remote-only ordinary and loaned publishers skip live local delivery and local durable
   replay while still invoking remote fan-out.
4. Host `rmw_mdds_cpp` broker/direct-mode tests pass, including typed and serialized ignore-local coverage.
5. On the current RK3588A overlay, the two focused upstream tests pass, then full `test_subscription` and the
   complete board `test_rmw_implementation` program matrix pass with board-side result markers.
6. Cross-board topic, service, action, transient-local, and loaned/zero-copy regressions pass using
   `RMW_IMPLEMENTATION=rmw_mdds_cpp` and the newly built bridge artifact.

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
2. Add the deterministic bridge-enabled ignore-local RED test before changing bridge or broker behavior.
3. Add the extended MDDS publisher creation API, live-delivery and durable-replay gates, bridge extended creation API, and broker capability check.
4. Remove the payload-based broker echo heuristic only after the remote-only tests are GREEN.
5. Add RED contract tests for every other row that is currently unproven but intended to become supported.
6. Implement other missing behavior only after its RED gate exists.
7. For surfaces that remain out of scope, add explicit unsupported contracts and record the accepted boundary.
8. Re-run host delivery contracts, upstream RMW tests, and affected board lanes.
9. Decide the delivery endpoint: local-only handoff, OpenSpec archive, push, PR, or Gerrit submission.

Rollback is to keep the two completed scoped changes and leave this final parity change open as the record that universal completion is still unproven.

## Open Questions

- Must the final definition of "perfect" include zero-copy for unbounded strings/sequences and dynamic message data, or is explicit unsupported behavior acceptable?
- Must security parity include signed SROS2 artifact validation and transport crypto, or is local policy enforcement plus fail-closed behavior acceptable?
- Answered on 2026-07-04: for the current request, delivery is satisfied by a committed local handoff in ROS 2
  plus DSoftBus/MDDS, with no push, PR, Gerrit submission, or OpenSpec archive requested.
- Answered on 2026-07-10: the user approved remote-only MDDS publishers for broker-owned bridge transport,
  with direct/non-broker local loopback preserved and payload-based broker echo suppression removed.
