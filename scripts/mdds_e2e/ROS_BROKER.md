# Production ROS broker integration gate

From the ROS workspace in Git Bash:

```sh
MDDS_RUN_ID=ros_broker_fresh_01 bash scripts/run_mdds_broker_ros.sh
```

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
per-node endpoint ownership on each board. Phase 2 destroys the beta node and
one duplicate while keeping beta's Context alive; each survivor exchanges five
fresh messages and verifies endpoint/node removal. Finally A actually exits,
and B must observe removal of A's nodes, publisher match and service availability
before B may exit. Both native daemons then drain and stop.

The receipt binds real child PID/start records to actual wait results. Each ROS
process reports its precise MDDS/RMW library mappings, full rclpy package/native
verification and absence of owned UDP sockets. The native daemon is inspected
for its exact executable, actual DSoftBus SDK mapping and absence of owned UDP.
Host validation checks the exact received strings, service values, node counts,
withdrawal and resource cleanup. Eight negative receipt checks reject corrupted
payloads, ghost nodes, missing withdrawal, wrong libraries, UDP, failed child
exits and unfinished broker cleanup. Run them separately with:

```sh
.pixi/envs/default/python.exe scripts/mdds_e2e/test_ros_broker_receipt.py \
  ohos_test_logs/ros_broker/ros_broker_20260906_04
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
In particular, observed endpoint type hashes remain `INVALID`; custom enclave
metadata and other remaining graph contracts still require implementation and
tests. CLI acceptance remains 21/98 until actual command receipts are added.
