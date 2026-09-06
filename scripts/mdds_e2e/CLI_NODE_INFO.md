# Node ownership through the actual ROS CLI

The daemon batch now creates a real Fibonacci ActionServer and ActionClient
on both alpha and beta nodes. Each action client targets the other board's
opposite Context: alpha targets beta, and beta targets alpha. These actions
are graph fixtures; this case does not claim goal execution or cancellation.
Action waitables are explicitly destroyed before their nodes, including beta
retirement and final cleanup.

On each board, `node info` queries the opposite board's alpha, beta and
duplicate node names through both cached and direct modes. It also queries
alpha with `--include-hidden` in both modes. Every printed section must match
the independently constructed fixture contract: publishers, subscribers,
service servers, service clients, action servers and action clients. This
includes standard parameter-event and type-description endpoints, and checks
that endpoints owned by another Context do not migrate into the queried node.

The hidden view additionally checks the explicit hidden Bool topic and
AddTwoInts service, action feedback/status topics, and goal/result/cancel
services with canonical types. The duplicate-name query must print the
upstream warning for exactly two nodes. Both duplicate instances deliberately
have identical endpoint names; the separate node-list case checks their
multiplicity, while this query checks their exposed CLI view.

```sh
MDDS_RUN_ID=cli_node_info_fresh_01 MDDS_ROS_PROFILE_MODE=implicit \
  MDDS_ROS_CLI_BATCH=daemon bash scripts/run_mdds_broker_ros.sh
.pixi/envs/default/python.exe scripts/mdds_e2e/test_cli_node_info.py
```

Nine output-oracle cases went from three failures to zero. They reject wrong
node ownership, missing/swapped actions, ghost endpoints, missing sections
and duplicated fields. The host recomputes the required recipe for each board
and requires all eight node-info variants, alongside the owned daemon lifecycle
and surrounding physical ROS/DSoftBus receipt.

The first two physical runs failed before fixture retirement: remote nodes
disappeared and the native link closed/rebound. Instrumented runs traced the
close to local delivery admission reaching 506 queued frames plus six reserved
PeerDown slots. The broker actor was visiting each client only once per poll.
The MDDS burst-drain fix preserves budgets while making further fair visits
to ready clients. Its two native RED regressions pass after the change, as do
all 32 MDDS board test entries.

`cli_node_green_20260907_01` passes all eight node-info variants on both boards
and all eight cases in the expanded daemon batch (22 commands per board).
Eighteen host receipt tests and eleven surrounding ROS receipt tests pass.
Each board has exactly one native DSoftBus OnBind, with no intermediate rebind;
the host now rejects missing or repeated native bind evidence and includes the
native logs in each receipt. Unique CLI coverage reaches 37/98. This does not
certify action goal execution or the remaining full graph/CLI gates.
