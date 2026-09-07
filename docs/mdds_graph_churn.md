# Repeated graph creation and removal

Design and test program before implementation: each board retains a survivor
node and creates/destroys a separate rclpy context with a same-named transient
node for 20 rounds. Each transient owns topic pub/sub, service server/client,
and action server/client endpoints. Every active and removed phase is queried
while the peer is changing its graph, then accepted only at an exact stable
barrier. Transient publication GIDs must be fresh; survivor GIDs must remain
unchanged. Final cleanup must leave the complete scoped graph empty.

Each of the 40 phases requires a unique survivor peer message and RPC. Active
phases also require transient peer data and RPC. Phase barriers themselves are
reliable ROS messages over the real DSoftBus transport. A board publishes its
ready phase only after recording its exact graph and data results; it advances
only after receiving the peer's ready phase. This prevents a fast peer from
destroying entities before the other board has recorded that phase.

Queries may see intermediate distributed discovery states during mutation;
those are retained as diagnostics and cannot count as stable snapshots. The
test requires all 40 stable phases, exact publisher/subscriber/service/client/
action ownership, and both native process exits. Planned bound: 180 seconds,
including initial/final CLI queries and source cleanup. No phase is skipped or
retried after it is accepted.

`test_churn_graph_contract.py` supplies exact cardinalities, phase-completeness
checks and stale/reused-identity negatives before implementation. Its initial
missing-module RED and the later parameter-event ghost RED are preserved.
The global `/parameter_events` publisher ownership is checked independently
of scoped topic names so destroyed-node ghosts cannot evade prefix filtering.

The first physical run, `graph_churn_20260907_01`, failed before accepting a
churn phase because the script imported `NodeNameNonExistentError` from the
wrong rclpy module. Both native error logs and terminal records were saved;
owned CLI cleanup passed. The import was corrected to the actual public alias
in `rclpy.node`, and explicit source-resource cleanup was added to the probe's
exception/finally path. The original 20 rounds and snapshot assertions remain.

Final HDC run `graph_churn_20260907_02` passed with Bash/native exits 0 and
owned cleanup on both boards. Each board completed 20 fresh transient contexts,
40 stable phases, 60 peer message receptions and 60 peer RPCs. A performed 392
query attempts and B 389. Active/removed scoped counts were nodes 4/2, topics
10/4 and services 14/4; complete by-node action/service/client ownership was
also exact. Transient GIDs were fresh in each round; survivor GIDs remained
unchanged. Both final graphs, including parameter-event publisher owners, were
empty. Every phase retained its native graph, data, service and peer-control
callback evidence; both boards reported identical endpoint identities.

Seven contract tests, 15 actual-receipt adversaries, 18 generic acceptance
tests and 11 enclosing broker adversaries passed. The existing native
MDDS/RMW implementation passed this new scenario; this feature adds the
test harness and evidence gate.

- Final manifest SHA-256:
  `95208da41801407a89f2caee9b41fde69f11bf245ba401df077075541ad4c208`.
- Churn receipt SHA-256:
  `587c77f7d656843cf2593d5445c7e148962fcd128a8a1728c490fc9b7c709d89`.

Aggregate coverage is 93/98 across frozen runs. Reconnection,
isolation, transport negatives, advanced functionality and a unified release
remain open. Gateway is outside the current goal.
