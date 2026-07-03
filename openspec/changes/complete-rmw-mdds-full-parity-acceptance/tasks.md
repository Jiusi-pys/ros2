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
- [ ] 2.4 Preserve explicit unsupported tests for rows that are accepted as out of scope.

Initial RED contract evidence: `ohos/test_rmw_mdds_full_parity_red_contracts.sh` exited nonzero by design on 2026-07-03 and emitted:
`RESULT|rmw_mdds_full_parity_loaned_shapes|RED|status=1`,
`RESULT|rmw_mdds_full_parity_signed_security|RED|status=1`, and
`RESULT|rmw_mdds_full_parity_broker_network_flow|RED|status=1`.

## 3. Implementation For Missing Supported Rows

- [ ] 3.1 Implement the smallest behavior change needed to turn the generalized loaned-message RED gates green.
- [x] 3.2 Implement the smallest behavior change needed to turn the security-parity RED gates green, or document the external MDDS/DSoftBus dependency blocking implementation.
- [x] 3.3 Implement any remaining supported RMW API rows identified by the parity matrix.
- [ ] 3.4 Re-run focused unit tests after each behavior change and keep unsupported rows explicit.

Security GREEN evidence: after the local XML/protected-governance split, `test_pubsub_inproc` passes
`RmwMddsPubSub.DISABLED_FullParitySros2RejectsTamperedUnsignedPermissions`, `ohos/test_rmw_mdds_sros2_policy_contracts.sh` still emits
`RESULT|rmw_mdds_sros2_policy_contracts|PASS`, and `ohos/test_rmw_mdds_full_parity_red_contracts.sh` now emits
`RESULT|rmw_mdds_full_parity_signed_security|PASS` while remaining nonzero for the two still-open implementation rows.

Broker network-flow GREEN evidence: `test_broker_mode` passes
`RmwMddsBrokerMode.DISABLED_FullParityBrokerModeNetworkFlowEndpointsReportMddsTransport`, and
`ohos/test_rmw_mdds_full_parity_red_contracts.sh` now emits
`RESULT|rmw_mdds_full_parity_broker_network_flow|PASS` while remaining nonzero for the still-open loaned-shape row.

Loaned-shape blocker evidence: `test_bridge_loaned_rmw` still keeps
`RmwMddsBridgeLoanedRmw.DISABLED_FullParityLoaned*` RED, and those gates now require unbounded string and
sequence dynamic member storage to come from the bridge loan before publish. Do not turn these gates GREEN by
serializing a default-allocator `std::string` or `std::vector` into a bridge loan; that would keep dynamic
storage outside MDDS and would not satisfy true zero-copy.

## 4. Runtime And Delivery Evidence

- [ ] 4.1 Run `ohos/test_rmw_mdds_delivery_contracts.sh` and confirm markers cover all affected host surfaces.
- [ ] 4.2 Rebuild the OHOS `rmw_mdds_cpp` overlay after parity changes.
- [ ] 4.3 Deploy the refreshed runtime delta to both RK3588/KaihongOS boards.
- [ ] 4.4 Run affected native MDDS, cross-RMW gateway, zero-copy, and security board lanes with explicit PASS markers.
- [ ] 4.5 Decide and execute the delivery endpoint: local handoff only, OpenSpec archive, push, PR, or Gerrit submission.

Interim host delivery evidence: after the security and broker network-flow behavior changes,
`ohos/test_rmw_mdds_delivery_contracts.sh` emits `rmw_mdds_delivery_contracts_ok`, including package
CTest `22/22`, upstream `test_rmw_implementation` `16/16`, SROS2 local policy, zero-copy, type
description, and host CLI pub/sub/service/action/params/lifecycle/graph/QoS/transient-local/message-info
PASS markers. Keep 4.1 open until the remaining loaned-shape implementation decision is complete and the
contract is rerun as final evidence.

## 5. Final Completion Decision

- [ ] 5.1 Update the parity matrix with all final command evidence and board markers.
- [ ] 5.2 Verify tracked status is clean and generated/scratch artifacts are ignored or intentionally tracked.
- [ ] 5.3 Validate all active OpenSpec changes with `openspec validate --strict`.
- [ ] 5.4 Decide whether accepted unsupported rows still satisfy the user's "perfect/all ROS 2 middleware features" objective.
- [ ] 5.5 Mark the persistent goal complete only if every required row is proven or explicitly accepted out of scope and no required delivery work remains.
