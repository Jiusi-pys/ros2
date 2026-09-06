# Actual cross-board action CLI acceptance

```sh
MDDS_RUN_ID=cli_action_fresh_01 MDDS_ROS_PROFILE_MODE=implicit \
  MDDS_ROS_CLI_BATCH=action bash scripts/run_mdds_broker_ros.sh
```

This batch uses `action_tutorials_interfaces/action/Fibonacci`, as required by
the acceptance inventory. Each board's alpha node owns an action server and
its beta node owns a client for the opposite board's server. The action names
include the unique run namespace. The action batch is separate from the
example_interfaces action endpoints used by the node-info batch.

The actual CLI lists both actions with exact types and counts, queries the
remote action type, and checks exact client/server node names and counts.
Discovery queries use the previously started, run-owned CLI daemon. It then
sends order 5 with feedback to the opposite board. Acceptance requires one
nonzero goal UUID, the five complete feedback prefixes from [0, 1] through
[0, 1, 1, 2, 3, 5], that exact final result, and SUCCEEDED status.

The host also checks the opposite server's callback record against the CLI's
goal UUID, run nonce, board identity, action name, order, every feedback prefix,
result, status and a count of exactly one. Its raw ROS log must contain the
same callback once. A successful CLI exit alone cannot pass a timeout or
rejection. The record is included as a hashed supporting artifact.

The standard daemon lifecycle, exact node-list multiplicity, DSoftBus library
and native link evidence, no owned UDP, the surrounding ROS exchange and
node/process withdrawal remain required. This successful-goal fixture does
not certify cancellation, rejection or all action concurrency contracts.

Ten output-oracle tests went from five failures to zero. Receipt tests use an
actual completed batch to reject UUID, payload, identity and lifecycle errors:

```sh
.pixi/envs/default/python.exe scripts/mdds_e2e/test_cli_action.py
.pixi/envs/default/python.exe scripts/mdds_e2e/test_cli_action_receipt.py \
  ohos_test_logs/ros_broker/<run_id>
```

`cli_action_20260907_01` passes all four action cases on both boards, with
complete feedback and UUID-matched opposite-side execution records. The nine
action receipt mutation tests and eleven surrounding ROS receipt tests pass.
The first host aggregation rejected the generic send-goal assertion schema;
the corrected receipt records all four required assertions directly from the
unchanged, hash-verified CLI output. No goal execution was repeated for this
host-only correction. Revalidating all accepted case receipts yields 41 unique
CLI operations out of 98, without a final single-release acceptance claim.
