# Endpoint metadata and QoS matrix

Design and tests precede implementation. Seven declared cases distinguish
graph-visible endpoints from actual matching and delivery: reliable/reliable,
best-effort/reliable, reliable/best-effort, volatile/transient-local,
transient-local/volatile, slow/fast deadline and fast/slow deadline. Negative
cases are never replaced with an absent provider or a skipped send.

Each board checks exact topic type/hash, node/namespace, GID, direction and
all reported QoS fields. It invokes `qos_check_compatible()` with publisher and
subscriber profiles in the documented order, and checks writer/reader matched
counts separately from graph counts. Both sides finish all sends before the
host starts the at-least-one-second negative observation window. Receiver
callbacks reject unexpected, duplicate or incompatible samples even after
the result is written.

Run using a fresh ID:

```bash
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=endpoint_qos \
  bash scripts/run_mdds_broker_ros.sh
```

Verified HDC run `endpoint_qos_20260907_04`:

- Both boards sent three samples for each case. The four compatible cases
  each received exactly three; reliability, durability and deadline negative
  cases each received zero. Both endpoints remained visible in graph data.
- Matched counts, compatibility query results/reasons, incompatible-event
  policy IDs and endpoint metadata agreed with the declared matrix.
- Five initial contract tests, 16 actual-receipt adversaries, 18 generic gate
  tests and the enclosing 11 broker receipt adversaries passed.
- Manifest SHA-256:
  `077f3a920e98a4b8a71b95ec3c7586ef06e3cfdd245713b358d508d4c0e749c2`.
- Endpoint case receipt SHA-256:
  `5eedda8bbb99094d96e492e92148195ae4d645efb4606da2a849e92ffcd499da`.
- MDDS / RMW library SHA-256:
  `444a20a873dba2367bed18731382579a5095f32b0192b56a74a947186a944978`,
  `afb1de869595261d8f15f2286b35a779f1cb1aa9725b2299266608bbdfc5f6b9`.

The tests exposed production defects in RMW matched counting/events and MDDS
deadline matching, plus the RMW compatibility-query deadline path. Their
native RED/intermediate/GREEN evidence is recorded in the package QoS docs.
All original failure expectations were retained.

Earlier runs remain failures: `_01` and the diagnostic `_02` did not complete
the matrix; the initially captured final pending counts were insufficient to
identify the pre-cleanup state, so later diagnostics append distinct states.
`_03` had a B-board CLI failure due to the first run's orphaned daemon, not
merely a transfer problem. Recovery assertions stopped any pass reconstruction.
PID 9563/start 47179471 was retired only after exact old-run ownership,
argv, broker-root and library-path checks. Its port was then verified reusable.
The outer failure-cleanup gap needs its own fault-injection fix before release.

Coverage is 88/98. This is not completed liveliness matching, complete
graph/failure/isolation coverage or a unified release. Gateway is out of scope.
