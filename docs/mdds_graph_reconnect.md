# Owned transport interruption and peer recovery

Design before implementation: keep the broker's local Server and ROS clients
alive while stopping the test broker's complete native DSoftBus remote driver.
This calls the actual SDK shutdown path and drains its callbacks. Recreate a
fresh channels/driver generation to restore the physical service; never restart
the one-shot driver object and never use UDP or a local-only success fallback.

Use a test-only driver slot and a dedicated fixture executable, not a production
daemon control backdoor. Pause/resume commands must be bound to the owned run,
nonce and phase. Native start failure or exception during restoration must fail
the test and retire the partially created driver. Local clients and the Server
remain owned by the enclosing daemon actor throughout the deliberate pause.

Tests before implementation cover exactly-once remote teardown, a fresh driver
on the same Server/domain, no work sent to a paused remote driver, rejected
duplicate transitions, and failed/throwing restore without fallback. These
tests use a deterministic driver substitute and do not prove physical traffic.

Required end-to-end phases remain: exact graph/data before interruption;
remote graph/service withdrawal while local entities survive; restoration via
real Socket/Bind/OnBytes evidence with fresh channel/receive identities; restart
of the owned peer fixture with new participant/endpoint GIDs; complete recovered
graph and exact bidirectional payload/RPCs; old identities absent; and final
owned cleanup. All phases need native exit records and immutable evidence.

Controller implementation is in the noninstalled MDDS test header
`test/reconnect_driver_slot.hpp`. It binds one local Server/domain, tears down
each remote instance exactly once, creates a new instance on resume, and fails
closed on a false/throwing restore. The test executable was built only after
the missing-header RED had been recorded.

HDC run `reconnect_slot_20260907_01` passed the remote-daemon target's 11 tests,
including the 4 new controller tests, with READY/terminal/archive agreement.
Archive SHA-256:
`fa4963ef194c7960fb1268d099099e5742d65dca533ac0945dff7b236a25bdb9`.
The test activity lock was released. The remote driver in this target is fake;
this is native controller verification, not Socket/Bind/OnBytes evidence.

The dedicated `mdds_broker_reconnect_fixture` is now built and linked only to
the real SDK-backed remote implementation. Its control files validate ownership,
nonce, regular-file identity and bounds; records cannot overwrite prior phases.
The two additional control-file tests passed natively with the existing tests
(13 total) in `reconnect_control_20260907_01`.

Pilot invocation (always use a fresh run ID):

```bash
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=none \
MDDS_ROS_REMOTE_CYCLE=1 bash scripts/run_mdds_broker_ros.sh
```

`sdk_cycle_20260907_01` passed the real two-board SDK cycle and unchanged ROS
baseline. Both brokers retained 2 local connections/ports, drained all native
channels while paused, then restored a channel/link in generation 2 with fresh
receive nonces. Baseline ROS messages were received after restoration and final
cleanup passed. Four host contract tests, eight actual-receipt adversaries and
eleven baseline broker adversaries passed. Pilot report SHA-256:
`90229a89380a3a949fdf6f57cfe8e8233a0b92965eadeeec51bea42de178b505`.

The pilot explicitly reports `full_reconnect_case: false`. At that stage it
still lacked disconnected snapshots, peer restart/new identities and full
recovered graph/RPC evidence. The next section records the graph/RPC extension.
Coverage at that stage remained 92/98. Source and native archive details are in MDDS's
`docs/broker_reconnect_fixture.md`.
The previous abrupt-process-death case does not substitute for this case.

## Graph withdrawal and restored RPC

The next test program was supplied before implementation in
`test_cycle_graph_contract.py`, with its missing-module RED preserved. It
requires an exact local partition of the initial endpoint snapshot during
the outage and an exact restored snapshot afterward. The collector includes
native topic names, node/enclave multiplicity, endpoint GIDs, owner/name,
direction, type/hash and every QoS field. It also queries previously known
topics/GIDs, so a disappearing catalog entry or missing owner label cannot
hide a stale endpoint. Client/subscriber-only names remain where appropriate.

Enable this scenario with `MDDS_ROS_CYCLE_GRAPH=1` in addition to the pilot
variables above. The host waits for the ROS graph to withdraw and for a local
cross-context message exchange before restoring the SDK. It then waits for
complete graph equality and a new peer RPC. Local probe entities are removed
only after both boards have recorded restoration, before the original ROS
baseline continues. This changes observation/barriers, not the native driver.

HDC run `cycle_graph_20260908_01` passed. Each board changed from 8 nodes and
68 endpoints to exactly its 4 local nodes and 34 local endpoints during the
outage. All three local messages arrived. The remote service became unavailable
and the remote subscription count became zero. Restoration recovered every
initial endpoint/GID/QoS/type hash and a new peer RPC completed with its actual
server callback. The baseline's later cross-board messages and cleanup passed.

Five contract tests, eleven graph-receipt adversaries, eight SDK-cycle receipt
adversaries, eighteen generic acceptance tests and eleven baseline broker
adversaries passed. Final report SHA-256:
`784cc1e2964cccf6f70d6c4319688445be3b15b104b4aae80bc45ba6904a2de0`.

The remaining full-case requirement at that stage was to restart an owned peer process and prove fresh
participant/endpoint identities, absence of old identities and complete recovered
graph/data/RPCs. This run deliberately preserves the original ROS contexts and
did not satisfy that restart requirement. The completed scenario is recorded below.

## Peer restart test design

Before implementation: add one owned child peer on each board in a namespace
separate from the baseline. Each child owns all 25 topic/service/action/parameter
endpoints of a rich ROS node, exchanges three generation-tagged messages and an
RPC with the other child, and reports its real PID/start and native provenance.
Pause the real SDK as above, verify the old peer graph is local-only, and send
SIGKILL only after exact child ownership checks. Wait until the local old peer
is absent before starting generation 2 while the SDK is still paused.

Require the new local graph and created-process record before restoring the SDK.
After restoration, both rich peer graphs must have new, disjoint GIDs and new
enclave generation tags, while endpoint types/QoS remain identical. Both new
children must complete new generation-tagged messages and RPCs. Stop generation
2 normally only after both observers record recovery, then require complete peer
graph removal. Retain old exit -9 and new exit 0 separately. The original main
ROS contexts continue through the existing graph/RPC cycle. The first contract
tests are in `test_peer_restart_contract.py`.

## Completed peer restart case

Five independent contract tests were supplied before implementation, with the
initial missing-module RED retained. `peer_reconnect_20260908_01` correctly
rejected the fixture's invalid numeric enclave token before peer traffic.
Generation tokens were changed to valid `g1/g2` names while preserving all
generation assertions. `peer_reconnect_20260908_02` completed native peer
replacement but failed with exit 127 because the runner called a nonexistent
wait helper. Diagnostics remain preserved. The runner now uses
`graph_wait_status` and distinct initial/final worker identity captures.

Final HDC run `peer_reconnect_20260908_03` passed the full declared sequence.
Before interruption both observers saw 2 peers and 50 endpoints; during pause,
each saw only its local peer and 25 endpoints. After owned SIGKILL each local
old-peer graph became empty before generation 2 was created. New local peers
were visible while SDK transport was still paused. After restoration both
observers saw 50 fresh endpoints disjoint from all old GIDs, with unchanged
types, hashes and QoS and new enclave generation tags. Each generation exchanged
three exact peer messages and completed a peer RPC. Generation 2 then exited
normally and the peer graph became empty. Main ROS contexts retained their
graph/GIDs, local outage traffic, restored RPC and later baseline peer traffic.

Actual PID/start identities: A changed `21317/49519558` to `21417/49520594`;
B changed `14510/49520818` to `14599/49521558`. Old exits were -9; new exits
were 0. Worker identity readbacks, ordered native phases, private RMW/library
provenance and fresh SDK receive nonces were checked. Run/cleanup exits were 0.

Five peer contract tests, 17 peer receipt adversaries, 11 main-graph receipt
adversaries, 8 SDK receipt adversaries, 18 generic acceptance tests and 11
baseline broker adversaries passed. A frozen `graph:reconnect` declaration and
native case-specific terminal markers are required before issuing acceptance.

- Manifest: `ac021a442cdf7fc780fe37ae12a6d608965e6f54d326278bb1d500e9d87f02ca`.
- Reconnect receipt: `9b6119249143ddcc4c13be8780f3c24467e02b141457584dc882fd5ec5a6a664`.
- Full cycle report: `cf6cf597f5a7a5f8f8fa21695b41cdbd9cfa80e87fce6e784e3f71d3e2898bb5`.

Coverage is 93/98 across frozen runs. Domain/discovery isolation, dedicated
transport negatives, advanced API/liveliness work and a unified release remain
open. Gateway stays outside the current goal.
