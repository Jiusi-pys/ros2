# Endpoint metadata and QoS matrix

Design and tests precede implementation. The original seven cases distinguish
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

Earlier verified seven-case HDC run `endpoint_qos_20260907_04`:

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
The outer failure-cleanup gap is now fixed and verified by the independent
failure injection in [CLI failure cleanup](mdds_cli_failure_cleanup.md).

The previous matrix had 14 cases. Seven additional cases cover automatic versus
manual-by-topic liveliness, valid manual offers, equal manual kinds, longer
and infinite offered leases against finite requests, and shorter/equal valid
leases. The three new negatives retain real discovered peers and all three
publication attempts. Kind and lease incompatibilities must both emit the
DDS LIVELINESS policy, matching the imported Fast DDS/Cyclone mappings.

`liveliness_qos_20260907_01` passed with native/Bash exit 0 and successful
owned cleanup on both boards. Eight compatible pairs each received three
exact peer samples; six incompatible pairs received zero, with zero strict
matched counts and one visible publisher/subscriber. Six contract tests,
twenty receipt adversaries, eighteen generic tests and eleven enclosing
broker adversaries passed. Before implementation, the native MDDS QoS,
RMW query and RMW event tests independently reproduced the defects; those
RED/GREEN archives are documented in each package's `docs/qos_liveliness.md`.

- Current manifest SHA-256:
  `21c97fce4ef00985a896794ef10e68e638d6ef5b3d9baa0daa59c27011d5141b`.
- Current endpoint receipt SHA-256:
  `ba8a20a131050ace1db191ed545d8a7829864805c4e115765bf80a224cbda7a5`.
- Tested MDDS / RMW library SHA-256:
  `24e754e20f0ea629ce4f0694dec23629bb305c22788cf781ddc9c23c6fc7964d`,
  `23afe3759f04e44d68de49512250ac198cdb54cb42ec27b158c7d0ab39e61df2`.

The current 19-case protocol-v10 matrix passed in `qos_nanoseconds_20260907_01`.
It adds one-nanosecond RxO differences, exact fractional-millisecond metadata
and a matched-but-expired 1 ns lifespan control. Ten pairs receive three samples
each; eight incompatible pairs and the expired pair receive none. Eight contract
tests, 24 receipt adversaries and 18 generic tests passed. Exact native hashes
and the final manifest are recorded in [Nanosecond QoS](mdds_qos_nanoseconds.md).

Coverage remains 93/98. Complete liveliness expiry/assertion/recovery behavior,
remaining graph/failure/isolation and advanced APIs, and a unified release
remain open. Gateway is out of scope.
