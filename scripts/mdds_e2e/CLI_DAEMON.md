# ROS CLI daemon lifecycle and graph comparison

```sh
MDDS_RUN_ID=cli_daemon_fresh_01 MDDS_ROS_PROFILE_MODE=implicit \
  MDDS_ROS_CLI_BATCH=daemon bash scripts/run_mdds_broker_ros.sh
```

Each board runs the actual installed ROS CLI, with the production rmw_mdds
library and a private native DSoftBus broker, against the opposite board's
live ROS fixture. The batch starts with no domain-175 ROS CLI daemon and a
bindable loopback CLI port. It executes status, start, running status, cached
node list, direct node list, service graph queries, stop, stopped status and a
final direct node list.

The two graph query modes must return the same eight node rows, including
both instances of each duplicate name and both nodes in each board's distinct
Contexts. Duplicate-node warnings are also required. The daemon is inspected
before and after graph queries: exact PID/start, command, private broker root,
RMW/MDDS mappings and hashes, no owned UDP sockets, and ownership of the
expected loopback XML-RPC listener. This TCP listener is local CLI control;
cross-board middleware traffic uses the separately inspected native DSoftBus
broker and the surrounding exact-payload ROS test.

Stopping must terminate that exact daemon process; the daemon must remain
absent through the subsequent direct query. Any emergency cleanup disqualifies
the batch. Cleanup acts only on processes matching the complete run ownership
contract. A pre-existing foreign daemon is an error and is left untouched.

Every actual CLI child has argv, PID/start, stdout/stderr, a real wait result
and a hash-bound log. The host rechecks the complete lifecycle before issuing
canonical dual-board receipts for every executed case. The original cases
are daemon start/status/stop and node list; service graph and node-info
extensions are also part of the current batch.
Daemon process disappearance is recorded separately from CLI stop's return
code, since the standard CLI detaches the daemon rather than returning a
waitable child handle to the test driver.

The eight output-oracle tests went from four failures to zero. They preserve
node multiplicity and reject existing-daemon startup, ghost nodes and wrong
namespaces. The receipt tests use an actual batch:

```sh
.pixi/envs/default/python.exe scripts/mdds_e2e/test_cli_daemon.py
.pixi/envs/default/python.exe scripts/mdds_e2e/test_cli_daemon_receipt.py \
  ohos_test_logs/ros_broker/<run_id>
```

This is a bounded daemon/graph acceptance case. Full graph, remaining CLI
operations and a single-release final acceptance are still separate gates.

`cli_daemon_20260907_01` passed all four cases on both boards, all eleven
surrounding ROS receipt checks and ten daemon receipt tests. The latter reject
missing exit evidence, a replaced daemon, wrong library hashes, owned UDP,
foreign broker roots, missing cached queries, direct queries substituted for
cached queries, emergency cleanup, and a daemon left running. Revalidation of
the active per-case batches now yields 33 unique CLI operations out of 98.

The service graph extension queries the opposite board's isolated alpha
service type, finds exactly four visible AddTwoInts services and six when
including hidden services, and checks the hidden-inclusive count. Service info
must return one client, one server and the correct type through both cached
and direct queries. Type/find do not expose a no-daemon option in this Jazzy
CLI; their queries run within the proven owned daemon lifecycle.

`cli_service_graph_20260907_01` passed all seven cases on both boards, with
14 actual commands per board and eleven surrounding ROS receipt tests. Ten
service output-oracle tests went from four failures to zero. Thirteen daemon
receipt tests also pass, including rejection of a missing hidden query,
incorrect service counts and cached queries substituted for direct queries.
Deduplicated coverage is now 36/98.

`CLI_NODE_INFO.md` documents the subsequent action-bearing node-info fixture,
the broker scheduling defect it exposed, and the corrected physical run.
With node info included, current unique CLI coverage is 37/98.
