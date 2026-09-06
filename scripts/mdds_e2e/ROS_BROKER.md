# Production ROS broker integration gate

From the ROS workspace in Git Bash:

```sh
MDDS_RUN_ID=ros_broker_fresh_01 bash scripts/run_mdds_broker_ros.sh
```

Set `MDDS_ROS_PROFILE_MODE=implicit` to verify production startup with
`MDDS_DEPLOYMENT_PROFILE` and `MDDS_TRANSPORT` absent. The runner still sets the
supported ROS discovery range to `SYSTEM_DEFAULT`. The actual policy environment
is included in both ROS provenance snapshots and verified by the host.

Use a fresh run ID containing at most 32 letters, digits or underscores. The
runner uses the two configured RK3588A serials, domain 175, the current
production MDDS/RMW build libraries and broker service, and a complete private
rclpy package containing the selected fixed native extension. Inputs are
hashed and staged beneath the owned run directory. Shared ROS installations
are not modified. The caller must build the selected artifacts beforehand.

Each board creates two Contexts, each with an independent node, reliable
publisher/subscription and AddTwoInts service/client. Two same-name nodes
share one Context on each board. Phase 1 requires ten exact incoming messages,
two correct service results, ACK completion, correct duplicate cardinality and
per-node endpoint ownership on each board. Each Context selects its own enclave
(`/A/alpha`, `/A/beta`, `/B/alpha`, `/B/beta`); every node/enclave association is
checked, including duplicates. Phase 2 destroys the beta node and
one duplicate while keeping beta's Context alive; each survivor exchanges five
fresh messages and verifies endpoint/node removal. Finally A actually exits,
and B must observe removal of A's nodes, publisher match and service availability
before B may exit. Both native daemons then drain and stop.

Expected type hashes are frozen from the generated String and AddTwoInts
request/response type-description JSON files. Both publishers and subscriptions
on ordinary topics and raw request/reply topics must match those hashes; retired
endpoints must disappear. `MDDS_ROS_RMW_LIBRARY` can select a frozen baseline
library explicitly for a RED run; the selected file is still hashed and staged.

The receipt binds real child PID/start records to actual wait results. Each ROS
process reports its precise MDDS/RMW library mappings, full rclpy package/native
verification and absence of owned UDP sockets. The native daemon is inspected
for its exact executable, actual DSoftBus SDK mapping and absence of owned UDP.
Host validation checks the exact received strings, service values, node counts,
withdrawal and resource cleanup. Eleven negative receipt checks reject corrupted
payloads, ghost nodes, missing withdrawal, wrong libraries, UDP, failed child
exits, incorrect enclave associations, wrong type hashes and unfinished broker
cleanup, and a conflicting policy environment. Run them separately with:

```sh
.pixi/envs/default/python.exe scripts/mdds_e2e/test_ros_broker_receipt.py \
  ohos_test_logs/ros_broker/ros_no_profile_20260907
```

`ros_broker_20260906_04` passed this gate: 30 total exact received messages,
four completed service calls, four Contexts, two ROS processes, node and
process withdrawal, and two clean native daemon exits. All eight receipt
negative checks passed. The preceding scratch run `_03` also passed; `_02`
completed both data/graph phases but incorrectly rechecked peer presence after
authorizing that peer's exit. The corrected gate explicitly requires withdrawal.
Run `_01` stopped during preparation because an older helper required a different
owner label; no ROS test ran in that attempt.

This is a graph/data integration subset, not the full graph or CLI gate.
Other remaining graph contracts still require implementation and tests. The strengthened enclave
case failed in `ros_enclave_red_20260907` with empty enclave fields, then passed
in `ros_enclave_green_20260907`, together with all nine negative receipt checks.
The strengthened hash case then observed 24 groups of `INVALID` values with the
frozen old RMW in `ros_hash_red_20260907_02`; `ros_hash_green_20260907` passed all
generated hash checks and ten negative receipt tests. The first hash run stopped
before launching ROS because a host path needed Windows conversion.
The optional live CLI batch is documented in `CLI_GRAPH_BASIC.md`; its complete
dual-board receipts, including topic data and visibility comparisons, cover eight
live CLI cases. The separate `MDDS_ROS_CLI_BATCH=daemon` batch and its ownership
requirements are documented in `CLI_DAEMON.md`. With its four new operations,
current deduplicated CLI coverage, including the separate `MDDS_ROS_CLI_BATCH=action`
batch documented in `CLI_ACTION.md` and service introspection in `CLI_SERVICE_ECHO.md`,
and parameter reads/mutations in `CLI_PARAMETERS.md`, is 49/98. Earlier expanded basic receipts were
superseded by the daemon-isolated run described in `CLI_GRAPH_BASIC.md`.

`ros_no_profile_20260907` passed the full current subset in implicit policy mode,
including all eleven negative receipt checks. This proves no-profile startup
does not select UDP; unsupported discovery restrictions still fail explicitly.
