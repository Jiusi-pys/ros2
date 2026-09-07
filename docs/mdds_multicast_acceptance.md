# Standalone multicast CLI acceptance

```bash
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=multicast \
  bash scripts/run_mdds_broker_ros.sh
```

This mode runs real `ros2 multicast receive` and `send` on both RK3588A boards
with a run-derived multicast group/port, explicit Ethernet interface and
disabled sender loopback. Before releasing the send barrier, it verifies each
receiver's owned UDP endpoint and the selected interface's IGMP membership.
Each receiver must return the exact diagnostic payload from its peer's IP.
The actual commands, PID/start times, source ports, exit codes and barrier
nonces are captured. Timeout/emergency cleanup, a local packet, wrong data or
duplicate receive reports fail acceptance.

The private source overlay includes ros2cli and ros2multicast, with archive,
file and preparation hashes. Four source API regressions passed after RED;
four receive-output tests and seven actual-receipt adversaries passed. The
original sender rejected TTL 255 and leaked its socket when TTL configuration
failed; both cases are fixed alongside explicit interface selection.

Run `cli_multicast_20260907_01` passed on both boards with all four real CLI
exit codes zero. The ordinary harness also verified thirty cross-board ROS
messages, four service transactions, graph retirement, daemon lifecycle and
native DSoftBus broker provenance. Those separate checks provide the middleware
evidence; the utility's UDP packets do not substitute for it.

Partial manifest SHA-256:
`a73efb075b3fd701040f518c158149b2e4429d8b087e2c9a92ba55875bbce573`.

```bash
python scripts/mdds_e2e/check_multicast_receipt.py \
  ohos_test_logs/ros_broker/cli_multicast_20260907_01
```

Two unique cases bring the aggregate CLI/graph/transport ledger to 77/98.
Tracing, remaining graph/transport/stress cases and final core-only deployment
are unfinished. Gateway is not part of the active objective.
