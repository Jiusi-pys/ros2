# Cross-board parameter CLI acceptance

The `parameter_read` batch enables parameter services on each board's existing
alpha node. Its seed includes all nine ROS scalar/array parameter types,
nonce-derived values, a bounded integer, a read-only parameter and a dynamic
parameter. Standard use_sim_time and type-description settings remain visible
and are included in the exact expected name/value set.

```sh
MDDS_RUN_ID=cli_param_read_fresh_01 MDDS_ROS_PROFILE_MODE=implicit \
  MDDS_ROS_CLI_BATCH=parameter_read bash scripts/run_mdds_broker_ros.sh
```

Each board runs actual param list, nine typed param get commands, two param
describe commands and param dump against the opposite board. The type labels
and values must match exactly. Descriptions include the integer min/max/step
and additional constraints, and the explicit read-only flag. The dump's actual
stdout is also saved as a run-owned YAML file; its full node key, parameter
names, types and values are checked. Byte arrays retain their bytes/YAML binary
representation and cannot pass as ordinary integer arrays. Recursive type
comparison prevents Python's True==1 equality from concealing a type error.

The host checks each peer's actual initial parameter snapshot, the dump file
against captured CLI stdout, every command and log hash, and the surrounding
owned-daemon/ROS/DSoftBus lifecycle. Eight output-oracle tests went from five
failures to zero. These tests alone are not physical acceptance. Parameter
mutation/event/restore and YAML load remain separate cases until executed.

`cli_param_read_20260907_02` passes all four parameter read cases on both
boards (13 parameter commands and 21 total CLI commands per board). Six host
receipt mutation tests and eleven surrounding ROS receipt tests pass. The
first run completed the board commands but used the wrong host collection
branch; it was excluded and the full corrected runner was repeated. Current
unique CLI coverage is 46/98.

The `parameter_write` batch covers set, load and delete. Set changes the
counter, reads it back, restores the initial value and reads that back. Load
uses a run-owned YAML file changing an integer, double, bool array and string
array; every changed value is read back. Delete removes a dynamic parameter,
checks its absence from list and verifies get reports `Parameter not set`
on stderr with its actual exit code 1. For an undeclared parameter the rclpy
service returns no values; this differs from the CLI's separate NOT_SET-value
branch, which can print a message with exit code 0.

A subscriber on the initiating board records the opposite node's parameter
events. The host requires the exact ordered set/restore/load/delete events,
with names, value types and values, plus a matching final peer state and raw
event log entries. The four YAML changes must preserve unrelated values.
Helper get/list commands belong to their mutation case receipts; they do not
claim another complete get/list acceptance case. Eight mutation-oracle tests
went from five failures to zero.

Expected failure is accepted only for the delete case's follow-up get, with
exact stderr and an earlier successful delete of the same node/parameter on
the same board. Other errors, a different target, missing deletion and timeout
remain failures; exit codes are never rewritten. Nine absence-contract tests
went from one unsupported-positive error to zero, and the original acceptance
tests still pass. The first physical mutation run exposed the incorrect
NOT_SET-value assumption and was excluded; the corrected recipe is rerun.

`cli_param_write_20260907_02` passes set/load/delete on both boards, including
seven exact remote parameter events and the complete final state. Nine
mutation-receipt tests reject missing restore/readbacks/events, wrong event
ownership, retained deleted parameters, failed loads and altered YAML/logs.
The eight output-oracle, nine absence-contract, seventeen original acceptance
and eleven surrounding ROS receipt tests also pass. Unique CLI coverage is
49/98; helper reads remain part of their mutation receipts.
