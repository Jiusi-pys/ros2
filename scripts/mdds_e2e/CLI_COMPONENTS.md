# Cross-board component-container acceptance

```sh
MDDS_RUN_ID=cli_components_fresh_01 MDDS_ROS_PROFILE_MODE=implicit \
  MDDS_ROS_CLI_BATCH=components bash scripts/run_mdds_broker_ros.sh
```

The runner stages the installed native component container, the five composition
plugin libraries and their exact component registry in a private run prefix.
Ament lookup uses that prefix first; dependencies remain in the existing board
installation. The actual container runs through the owned supervisor with the
private production rmw_mdds/MDDS libraries and DSoftBus-only policy.

Each CLI loads two standard composition::Talker instances into the opposite
container, with explicit run-specific node names and distinct topic remaps.
The fixture verifies actual received Hello World counters, exact endpoint
ownership, distinct writer GIDs and the generated String type hash. The native
publisher log must contain every accepted payload under the matching node name.
Types lists all five registered composition plugins; list checks both component
IDs/names and both containers.

Unload first removes ID 1. The observer must see its node and publisher disappear
while ID 2 keeps the same endpoint identity and delivers a further payload.
ID 2 is then removed and both endpoint/node names must disappear while the
container remains visible. The actual container is stopped only after the CLI
finishes; its exact PID/start and clean exit are required. Live inspection binds
its executable, arguments, RMW/MDDS/Talker mappings and hashes and absence of UDP.

Containers/components use a separate namespace from the base ROS graph fixture,
which still performs its exact bidirectional messages, service calls and node/
process retirement checks. Native DSoftBus bind/no-rebind and daemon ownership
remain required. Nine CLI oracle tests went from five failures to zero. This
batch does not claim standalone component execution, which remains a separate
CLI case, or successful construction of all other registered plugin types.

`cli_components_20260907_01` passes types/load/list/unload on both boards, with
private native executable/plugin provenance, peer payloads, selective removal,
survivor identity/data continuity and clean container exits. Nine component
receipt mutation tests and eleven surrounding ROS receipt tests pass.
Unique accepted CLI coverage is 57/98; standalone remains unverified.
