# Actual cross-board lifecycle CLI acceptance

```sh
MDDS_RUN_ID=cli_lifecycle_fresh_01 MDDS_ROS_PROFILE_MODE=implicit \
  MDDS_ROS_CLI_BATCH=lifecycle bash scripts/run_mdds_broker_ros.sh
```

Each board's alpha fixture is a real rclpy LifecycleNode; beta and the duplicate
nodes remain ordinary nodes. The CLI must discover exactly the two alpha nodes
as managed nodes, count them, and read the opposite alpha's unconfigured state
with its exact label and ID. Available transitions are checked in unconfigured,
inactive, active and finalized states.

The actual CLI drives configure, activate, deactivate, cleanup and shutdown on
the opposite board. Each successful transition is followed by an actual state
query. The readbacks remain inside the set receipt; they do not inflate the
get case count. The peer must record exactly five callbacks with the correct
previous states and finish in finalized [4]. A subscriber on the initiating
board must receive the exact ten start/goal state events, covering entry into
and successful exit from each intermediate state.

The pinned Jazzy rcl_lifecycle notification implementation fills start/goal
states but leaves transition and timestamp fields at their initialized values
(ID 0, empty label, timestamp 0). The fixture checks those actual values; it
does not invent transition IDs from the event. CLI transition-list responses
separately verify the available transition IDs. Regular fixture publishers
continue to support the surrounding transport checks; managed-publisher
activation behavior and lifecycle error paths are not certified by this case.

The original daemon ownership, exact ROS payloads, graph withdrawal, native
DSoftBus bind/no-rebind and no owned UDP checks remain required. Nine output
oracle tests went from six failures to zero. Host receipt tests use the actual
completed batch to reject missing callbacks/events/readbacks and wrong states.

`cli_lifecycle_20260907_01` passes all four lifecycle cases on both boards,
with 17 lifecycle commands and 25 total CLI commands per board. Each side
records five peer callbacks, ten matching remote events and finalized [4].
Nine lifecycle receipt tests and eleven surrounding ROS receipt tests pass.
Unique accepted CLI coverage is 53/98. Full graph and single-release acceptance
remain open.
