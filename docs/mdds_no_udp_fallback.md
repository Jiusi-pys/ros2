# Forced SDK failure without MDDS UDP fallback

Design before implementation: use the existing real SDK pause/restore fixture
and full graph-cycle observations with explicit `MDDS_TRANSPORT=dsoftbus`.
Both ROS contexts remain alive while the native SDK channels are destroyed.
Require local-only endpoint partition, unavailable peer service, zero peer
subscriptions, exact local messages, then complete graph restoration and an
actual peer RPC plus the normal exact cross-board ROS message phases.

A separate test-only native broker executable links the verified socket
audit library directly. The ROS child preloads the same library, including
the staged Python launcher's audited exec handoff. Native executable and ROS
lifetimes must close with zero IPv4/IPv6 datagram attempts, no in-flight calls
or audit failures, and a nonzero total socket count. The audit must be mapped
from the frozen private run path with the same hash as the separately verified
dual-board positive/zero/missing controls. Those controls are copied into
the evidence bundle and independently revalidated.

The full audit interval includes MDDS shutdown: the ROS context cleanup
marker and native broker stop must precede final audit records. No cached PASS,
implicit transport selection, missing process, late loaded audit, unclosed
image, stale graph or restored-local-only behavior can complete this case.
The dedicated case is now VERIFIED by the run below. Advanced RMW work and a
unified release remain open independently of this case.

The first physical attempt `no_udp_20260908_01` failed before phase 1 node
creation. Audit injection had replaced the runtime's existing CPython preload;
NumPy reported missing `PyModule_AddObject`. The real native SDK broker did
start, but the ROS failure prevents any acceptance. Injection now preserves
the original preload and appends the audit library. The native audit broker
retains the normal launch environment and uses its explicit DSO dependency.

`audit_runtime_20260908_01` verified this exact combination on both boards:
native NumPy allocation returned `[0, 0]`, positive and zero socket counts
remained exact, all process images were accounted for, and 11 control receipt
tests passed. The no-fallback verifier now requires this runtime control in
addition to the existing socket positives. Historical controls without NumPy
remain evidence for their original narrower instrument scope only.

## Final physical result

HDC run `no_udp_20260908_02` passed with explicit dsoftbus selection on both
RK3588A boards. Native SDK pause destroyed all channels/remote links while
preserving each board's two local clients/ports. Graph state went from 8 nodes
and 68 endpoints to exactly the local 4 nodes and 34 endpoints. Peer services
became unavailable and peer publisher matches became zero. Each board still
received its three exact local messages.

The native SDK was rebuilt in generation 2 with fresh receive identities;
the complete original endpoint metadata/GIDs returned and both peer RPCs
succeeded. The ordinary baseline also delivered 15 exact cross-board messages
and two initial service replies per board. Paused-to-restored native intervals
were 14.137 s on A and 13.829 s on B; these are scenario intervals, not claimed
failure-detection latency bounds.

All four native children exited zero. Final socket counts were A ROS 6,
A broker 11, B ROS 6 and B broker 5. Every IPv4/IPv6 datagram attempt count,
datagram success count, in-flight count and instrumentation error count was
zero. Audit mappings matched the frozen tested DSO. ROS context destruction
and native broker resource shutdown preceded their audit finalizers, and
Python's bootstrap exec counters were also zero and fully accounted for.

Validation passed: 6 no-UDP contract tests, 11 lifetime tests, 17 combined
receipt tests, 11 runtime-audit control tests, 18 generic acceptance tests and
11 baseline broker receipt tests. The initial contract/lifetime RED and the
NumPy/preload failure are retained. No assertion about graph loss, local
survival, recovered data/RPC, native process exit or UDP absence was removed.

- Native audited broker: `272d555f160b25e166cdf375781be67fdc249bccc17e490b6173153ba9598c49`.
- Audit DSO: `da3beccc8803d1c13f5a8bd85c2291ad07ba1c59ad35fd5a039c9a09e97a413c`.
- Combined report: `5910d50f3c16048ca049a7d58b273ba970477af9bfcbb14b64bd9ada02c76f08`.
- Case receipt: `d2465872e6efedb3fd21234851ae25f5546d88a3de126ae44c8daec725660852`.
- Partial manifest: `f300e957ecfded1df0e4348c4f249e26769ecb796ad8ab08c48bec2527969af1`.

Reproduce with a fresh run ID and `MDDS_ROS_NO_UDP=1`,
`MDDS_ROS_REMOTE_CYCLE=1`, `MDDS_ROS_CYCLE_GRAPH=1`,
`MDDS_ROS_PROFILE_MODE=selector`, `MDDS_ROS_CLI_BATCH=none` and
`MDDS_AUDIT_CONTROL_DIR=ohos_test_logs/socket_audit/audit_runtime_20260908_01`,
then execute `bash scripts/run_mdds_broker_ros.sh`. Run
`check_no_udp_receipt.py` against the resulting directory for adversaries.

The CLI/graph/transport inventory now has 98/98 cases across historical builds.
This is not one release and does not prove full RMW feature completion. Current
source still has 18 unsupported API macros plus unsupported serialized-size
calculation; publication-sequence capability reporting and reception sequence
support also need work. The full goal, unified validation and pre-push review
remain open. Gateway remains outside scope.
