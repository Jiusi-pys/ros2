# FastDDS named-baseline gate

`run_fastrtps_baseline_gate.sh` is a deliberately narrow evidence gate for the
one official CTest selector previously reported as an unrelated FastDDS
failure:

```text
test_rmw_implementation/test_subscription__rmw_fastrtps_cpp
```

It does **not** invoke `run_ros2_gw.sh`.  It asks the ordinary board-test
stager to replay exactly that CTest entry with `rmw_fastrtps_cpp` in the
selected board's existing ROS 2 image, then verifies the board runner's
run/nonce binding, terminal record, archive controls, captured archive hash,
activity-lock release, and the archived generated driver’s FastDDS environment
and subscription command.  The raw driver stdout and remote tar remain under
the gate directory and are the primary test evidence.

## Invocation

From `ros2/` in Git Bash:

```sh
./scripts/run_fastrtps_baseline_gate.sh A
./scripts/run_fastrtps_baseline_gate.sh A --exemption-file path/to/fastdds-exemption.record
```

The default output root is `ohos_test_logs/fastdds_baseline`.  Set
`ROS2_RUN_ID`, `ROS2_FASTDDS_BASELINE_NONCE`, and `FASTDDS_BASELINE_LOGROOT`
when a collector needs a caller-controlled evidence location.  Reusing a
run-ID/nonce directory is rejected.

The result file is `fastdds_baseline_gate.record`, a line-oriented
machine-readable record.  Its important fields are:

```text
RESULT=PASS|FAIL|EXEMPTION|BLOCKED
ACTUAL_TEST_RESULT=PASS|FAIL|UNOBSERVED
OVERALL_RMW_SUITE=NOT_RUN_BY_THIS_GATE
```

Exit status is `0` only for an actual `PASS`; `1` for an actual un-exempted
test `FAIL`; `2` for `BLOCKED` (for example, no authenticated terminal/archive
evidence or no proven lock release); and `3` for `EXEMPTION`.  Therefore a
recorded exemption cannot accidentally make an automation report the official
RMW suite as green.  This one-test gate also cannot replace the official RMW
suite: use its own raw results for that suite's pass/fail/skip count.

## Exemption record schema

A local exemption may only classify an observed failure as `EXEMPTION` when it
is a regular, non-symlink file with exactly these nine key/value lines, in this
order.  `EXPIRES_UTC` must be in the future at gate execution time.

```text
V=1
TARGET_PACKAGE=test_rmw_implementation
TARGET_TEST=test_subscription__rmw_fastrtps_cpp
RMW_IMPLEMENTATION=rmw_fastrtps_cpp
DECISION=EXEMPT
EXEMPTION_ID=FASTDDSTEST-YYYY-NNN
OWNER=Named maintenance owner
EXPIRES_UTC=2027-01-01T00:00:00Z
REASON=Exact reason and external issue or baseline reference
```

The gate stores the record's SHA-256 and exemption ID, but a local file is not
a cryptographic signature or immutable approval.  A release decision still
needs the named independent maintainer's signature and external/WORM retention
of the resulting manifest and raw evidence.
