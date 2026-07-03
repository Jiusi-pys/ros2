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
storage outside MDDS and would not satisfy true zero-copy. Unsupported dynamic loan attempts now return a
specific error explaining that only flat fixed-size scalar types are bridge-loan backed and that dynamic
string/sequence storage is not backed by the MDDS bridge loan.

Focused verification evidence on 2026-07-03: `ohos/test_rmw_mdds_full_parity_red_contracts.sh` still exits
nonzero by design with `RESULT|rmw_mdds_full_parity_loaned_shapes|RED|status=1`, while
`RESULT|rmw_mdds_full_parity_signed_security|PASS` and
`RESULT|rmw_mdds_full_parity_broker_network_flow|PASS` remain green. Keep 3.4 open until every future
behavior change in this acceptance track has matching focused verification.

## 4. Runtime And Delivery Evidence

- [x] 4.1 Run `ohos/test_rmw_mdds_delivery_contracts.sh` and confirm markers cover all affected host surfaces.
- [x] 4.2 Rebuild the OHOS `rmw_mdds_cpp` overlay after parity changes.
- [x] 4.3 Deploy the refreshed runtime delta to both RK3588/KaihongOS boards.
- [x] 4.4 Run affected native MDDS, cross-RMW gateway, zero-copy, and security board lanes with explicit PASS markers.
- [x] 4.5 Decide and execute the delivery endpoint: local handoff only, OpenSpec archive, push, PR, or Gerrit submission.

Fresh host delivery evidence on 2026-07-03: after the security, broker network-flow, explicit
dynamic-loaned-message diagnostic, and host CLI harness robustness changes, `ohos/test_rmw_mdds_delivery_contracts.sh`
emits `rmw_mdds_delivery_contracts_ok`, including package CTest `22/22`, upstream
`test_rmw_implementation` `16/16`, SROS2 local policy, zero-copy, artifact, type-description, and host CLI
pub/sub/service/action/params/lifecycle/graph/QoS/transient-local/message-info PASS markers. During the
current-tree rerun, the params/lifecycle probes exposed daemon/discovery timing misses; the harness now uses
daemon-free discovery and state polling for those probes, and the final full delivery rerun passed. Re-run
this contract again if the remaining loaned-shape implementation decision lands as another behavior change.

OHOS overlay rebuild evidence on 2026-07-03: `./ohos/colcon_rk3588a.sh rmw_mdds_cpp` rebuilt and installed
the target package into `install/ohos-colcon-rk3588a`, and `ohos/test_rmw_mdds_artifact_contracts.sh`
emitted `rmw_mdds_artifact_contracts_ok`. Rebuilt artifact checksums:
`librmw_mdds_cpp.so` sha `45b901717a47ce9b7d6124131df3f6a10f952b86581160a5f5e7375de1b9bdbb`;
`rmw_mdds_broker` sha `85f5c7afc3c51f4bd98cad1c2e8f1edd3df968e6124b3c6e2435feac3680670f`.

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
`cross_board_rmw_mdds_sros2_policy_ok`. Protected signed/transport SROS2 behavior remains a separate
incomplete full-parity security row unless accepted out of scope.

Delivery endpoint decision on 2026-07-03: no push, PR, Gerrit submission, or archive was requested, so the
executed endpoint is a local handoff on branch `jazzy-ubuntu-20.04`. The branch is clean and ahead of
`origin/jazzy-ubuntu-20.04` by 18 commits, from upstream base `991c434` to local head `366b6b0`. The latest
acceptance commits are `366b6b0 test(rmw_mdds): refresh board and delivery parity gates`,
`f8a0735 docs(rmw_mdds): record OHOS overlay rebuild evidence`,
`c0821f2 docs(rmw_mdds): record fresh host parity evidence`, and
`6b602d8 fix(rmw_mdds): clarify dynamic loaned message rejection`. This local handoff is explicit, but it
does not by itself satisfy the broader "perfect/all ROS 2 middleware features" objective because local-only
delivery still requires user acceptance before final completion.

## 5. Final Completion Decision

- [x] 5.1 Update the parity matrix with all final command evidence and board markers.
- [x] 5.2 Verify tracked status is clean and generated/scratch artifacts are ignored or intentionally tracked.
- [x] 5.3 Validate all active OpenSpec changes with `openspec validate --strict`.
- [x] 5.4 Decide whether accepted unsupported rows still satisfy the user's "perfect/all ROS 2 middleware features" objective.
- [ ] 5.5 Mark the persistent goal complete only if every required row is proven or explicitly accepted out of scope and no required delivery work remains.

Final audit snapshot on 2026-07-03: `git status --short --branch` reports
`## jazzy-ubuntu-20.04...origin/jazzy-ubuntu-20.04 [ahead 18]` with no tracked or untracked file entries, and
`openspec validate --changes --strict` reports all three changes valid:
`complete-rmw-mdds-feature-closure`, `complete-rmw-mdds-full-parity-acceptance`, and
`complete-rmw-mdds-zero-copy-security`. Current unsupported/incomplete rows are not accepted as satisfying
the "perfect/all ROS 2 middleware features" objective: generalized dynamic loaned-message storage remains
RED, and signed SROS2 artifact validation plus encrypted/authenticated MDDS/DSoftBus transport remain
incomplete or external-dependency work. Therefore the persistent goal must remain open.
