# Abrupt process exit: design and tests

Before implementation: each board owns a survivor node in the existing ROS
process and a separate victim Python process with topic, service and action
entities. Both directions must exchange exact nonce-bound data and complete
RPCs before failure injection. Only the child created by the CLI worker may
receive SIGKILL, after checking PID, start time, exact argv, broker root and
loaded private MDDS/RMW paths. The victim's expected native exit remains -9;
the supervising test's successful exit must not relabel the victim as exit 0.

Survivors retain subscriptions and clients referring to the victims. After
death, victim topic/service names can therefore remain valid client-only names.
The required result is zero victim publishers/servers, unavailable victim
clients, no victim node/action/parameter-event ownership, unchanged survivor
GIDs, and working survivor peer data/RPCs. All final source resources must be
removed before final CLI graph checks.

Both observers arm before the host releases either kill command. Removal time
is measured from the local arm timestamp, so it is a conservative upper bound
on post-kill removal and never subtracts clocks from different boards. The
bound is 5 seconds, tighter than the default 30-second reliable-association
lease. EOF/channel retirement may remove the graph earlier. Survivor traffic
after removal is verified separately. This is process-death coverage, not
transport interruption/reconnection coverage.

`test_abrupt_graph_contract.py` specifies exact graph cardinalities, retained
client-only names, dead-server/parameter-event negatives, expected SIGKILL and
causal bounded removal before implementation. Ownership tests separately reject
reused PIDs, changed argv, foreign broker roots/libraries and already-dead
processes. The initial missing-module RED outputs are preserved.

The first physical run, `graph_abrupt_20260907_01`, passed. Its conservative
arm-to-removal bounds were A 4934.722246 ms and B 4651.957509 ms. Most of this
was HDC control-file handoff, leaving little margin under the unchanged 5 s
bound. The handoff was therefore changed to one bounded, hashed export and
one checked atomic import per board, using parallel direct HDC calls. New
handoff tests first failed without that implementation, then passed. No graph,
ownership, timing or data assertion was weakened.

Final run `graph_abrupt_20260907_02` passed with both victim process exits -9,
both observing ROS/CLI processes exiting 0, and successful owned cleanup.
Conservative arm-to-removal times were A 2400.348366 ms and B 2064.968707 ms.
These include control overhead and are not estimates of network latency.

Both sides observed exact node, topic, service, client, action and parameter-
event ownership before and after the kill. Scoped nodes changed 4 to 2;
topic names changed 8 to 4 and service names 14 to 6, retaining valid
subscriber/client-only names. Victim publishers/servers became zero and victim
clients reported unavailable. Survivor GIDs stayed unchanged and post-kill peer
messages/RPCs succeeded. Final source cleanup left all scoped graph sets and
parameter-event publisher ownership empty.

Nine contract/ownership/handoff tests, 18 actual-receipt adversaries, 18 generic
acceptance tests and 11 broker adversaries passed. This feature extends the
harness and evidence; the existing native MDDS/RMW implementation passed it.

- Final manifest SHA-256:
  `b03a7d0389ac51f165c0c4cd8f3dfaa5cc10ba70ced2a1856087e5177f32a7c2`.
- Abrupt-exit receipt SHA-256:
  `c7e45e649981df0aa7e04ea6925e0f04a240b203839c26fb938e0276d7f2f9e7`.

Aggregate coverage is 92/98 across frozen runs. Reconnection, isolation,
transport negatives, remaining advanced functionality and a unified release
remain open. Gateway stays outside the current goal.
