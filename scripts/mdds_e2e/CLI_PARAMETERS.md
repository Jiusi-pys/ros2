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
