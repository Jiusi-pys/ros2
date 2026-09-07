# Policy generation from the live MDDS graph

```bash
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=policy \
  bash scripts/run_mdds_broker_ros.sh
```

Both RK3588A boards execute actual `ros2 security generate_policy` through the
cached graph and through `--no-daemon --spin-time 3`, writing distinct new XML
files. The real DSoftBus fixture includes two contexts per board, duplicate
node names in the alpha contexts, pub/sub, services/clients and action endpoints.

Acceptance compares every generated permission against an independent fixture
map: four enclave paths and six profiles, with exact publish/subscribe and
request/reply direction. Relative and private expressions are expanded before
comparison. The current Jazzy generator expresses action permissions through
feedback/status topics and send_goal/get_result/cancel_goal services; each is
checked. Extra permissions, wildcards, missing lanes, wrong enclaves and
duplicate profiles are rejected. Generating these policies does not enable or
prove runtime DDS security enforcement in MDDS.

Run `cli_policy_20260907_01` generated correct policies but failed immediate
daemon port isolation. This reproduced the earlier hello-run observation, and
this time TCP TIME_WAIT was captured. A native XMLRPC half-close test failed
before the POSIX address-reuse fix and passed on both boards afterwards, while
another active listener remained excluded.

`cli_policy_20260907_02` passed all four real policy commands, daemon lifecycle,
port isolation, base cross-board data/service traffic and graph withdrawal.
The full pure-Python ros2cli package is staged with a manifest and archive hash
so the private server fix is included in actual CLI execution. Partial manifest:
`37628da9d3f016bf73680bc33663ac6f619254f0a44c9bbd9abd7a3f2cd0d6d4`.

Six policy contract tests were RED before implementation and are GREEN.
Seven actual-receipt adversaries passed, including rehashed permissions with
wrong enclave/direction, missing action lanes and wildcard substitutions.
Eleven base ROS receipt tests also passed.

```bash
python scripts/mdds_e2e/check_policy_receipt.py \
  ohos_test_logs/ros_broker/cli_policy_20260907_02
```

This adds one case, bringing the aggregate CLI/graph/transport ledger to 75/98.
Remaining commands, complete graph/stress coverage and final core-only
deployment/provenance are still required. Gateway is outside the active goal.
