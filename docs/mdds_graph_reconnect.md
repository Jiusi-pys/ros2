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

The pilot explicitly reports `full_reconnect_case: false`. Still required:
snapshots while disconnected, restart of the owned peer with new participant/
endpoint identities, and complete recovered graph/RPC evidence. Coverage stays
92/98. Physical transport stop/rebuild works; the full reconnect case is not
accepted yet. Source and native archive details are in MDDS's
`docs/broker_reconnect_fixture.md`.
The previous abrupt-process-death case does not substitute for this case.
