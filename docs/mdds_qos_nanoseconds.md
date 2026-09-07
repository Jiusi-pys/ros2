# QoS duration precision: design and tests before migration

Status: protocol v10 nanosecond migration implemented and verified by native
tests and the two-board 19-case DSoftBus QoS matrix. Aggregate coverage remains
93/98; the complete ROS functionality/unified release gate is still open.

Original defects that the migration addressed:

- `rmw_mdds/src/common.cpp` divides nanoseconds by 1,000,000. Positive
  sub-millisecond values become MDDS's zero/infinite duration. Other finite
  durations lose their remainder; non-normalized infinity is not canonical.
- `mdds::QosProfile` stores deadline, lifespan and liveliness lease as
  milliseconds in the fixed 32-byte ANNOUNCE QoS block. Preserving only the
  local RMW profile cannot preserve remote graph metadata or RxO ordering.
- DATA/DATA_FRAG carry publication time in milliseconds. Writer history,
  retry/replay and reader queues use this timestamp for lifespan checks.
- RMW publisher/receive/assertion activity and deadline/liveliness events
  use millisecond clocks. MDDS exposes remote last-seen observations in
  milliseconds. A conversion-only fix would leave actual timers broken.
- Jazzy `rmw/src/time.c` normalizes non-normalized durations and saturates
  overflow to INT64_MAX (the RMW infinite sentinel). The comment claiming
  `rmw_time_total_nsec()` overflows is stale and must be corrected.

Migration design:

1. Use explicit nanosecond names and values for QoS durations and publication
   timestamps throughout MDDS and RMW. Keep zero as the resolved MDDS infinite
   duration; map RMW unspecified and saturated/infinite values canonically.
   Preserve all finite representable values exactly, including INT64_MAX-1.
2. Bump the common frame protocol to v10. Keep QoS and DATA field widths and
   offsets, but declare their new units. Reject v9 rather than interpreting
   an old millisecond scalar as nanoseconds. Both RK3588A boards must use the
   same frozen build. Update golden wire fixtures for this deliberate protocol
   revision and retain explicit old-version rejection tests.
3. Propagate publication nanoseconds through fragmentation, reassembly,
   retransmission, historical replay, reader queues, and RMW source timestamps.
   Update expiration comparisons with subtraction after ordering checks so
   `publication + lifespan` cannot overflow. Retain distinct system-clock
   sample timestamps and steady-clock activity/lease observations.
4. Track local and remote liveliness/deadline activity in steady nanoseconds.
   Keep unrelated broker/discovery scheduling milliseconds where appropriate;
   do not silently rename unrelated timeout variables. Polling may deliver a
   status after the nominal deadline but must not convert a finite duration
   to infinite or count expired periods with integer overflow.
5. Migrate all direct MDDS callers/tests with explicit unit conversions.
   Update source-grounded API/wire documentation before feature commits.
   Public API/protocol changes require the requested agentic review before
   pushing. Gateway is outside this goal and does not constrain this migration.

Tests supplied before implementation:

- `rmw_mdds/test/test_qos_duration_precision.inc`: finite round trips for
  1 ns, 999,999 ns, nonintegral milliseconds and INT64_MAX-1; exact big-endian
  QoS scalars; canonical unspecified/infinite/non-normalized/overflow values;
  deadline and lease RxO pairs differing by 1 ns within the same millisecond.
  It runs in `test_type_hash`, which already compiles the production private
  conversion implementation, without changing the library's exported API.
- `test_liveliness_compatibility.inc`: 500,000 ns deadline and manual lease
  must produce expiration statuses after a 5 ms observation interval.
- `mdds/test/test_frame.cpp`: the intended v10 header must round-trip and a
  v9 header must be rejected. The previous v9 round-trip pin is deliberately
  advanced to v10; existing v2/v7 rejection tests remain. This is a protocol
  migration expectation, not evidence that production v9 was malformed.

Actual HDC RED evidence, with verified READY/terminal/archive bindings:

- Board A, `qos_precision_red_20260907`: 7 native targets passed, 2 failed,
  6 existing host-only checks skipped. Event target: 31 tests, 2 failures;
  conversion/type-hash target: 58 tests, 5 failures. Failures reproduced
  finite-duration truncation, noncanonical infinity, collapsed RxO ordering,
  and disabled sub-millisecond deadline/manual-lease expiration.
  Archive SHA-256:
  `5c970c619f2aa6d22fce74475eaa005bbb887cc526e8c296e1cb42e83e2f05ed`.
- Board B, `qos_wire10_red_20260907`: frame target 41 tests, 2 expected
  migration failures because production still emits/accepts v9.
  Archive SHA-256:
  `38f4db2b484c2c600ecc4c42fb877603b9475f8ec53da313b788a291092b24ac`.

Both original RED runs remain terminal failures; their test activity locks
were released. Subsequent implementation and GREEN evidence follow.

Implemented: exact QoS conversion and fixed-wire scalars; v10 header rejection
of v9; nanosecond publication/reassembly/history/queue/source timestamps;
overflow-safe lifespan/lease comparisons; steady nanosecond event observations;
64-bit deadline-period bookkeeping, signed-status saturation and re-arming
after new activity even when polling missed its initial unexpired interval.

Additional REDs exposed the deadline overflow/re-arm issues. A stale test clock
still used milliseconds for a newly nanosecond discovery observation; its unit
was corrected while preserving the freshness assertions. An old timestamp
assertion demanded millisecond alignment; it was replaced with a stricter exact
write-interval bound. No failure log was rewritten and no expiry assertion was
removed. See the package `docs/qos_nanoseconds.md` files for individual runs.

GREEN: MDDS 32 native targets (participant 105, QoS 12, frame 41 tests); RMW
9 native targets (event 33, conversion/type-hash 58, graph 46), with 6 existing
host-only skips. Lifespan boundary/overflow, maximal finite lifetime, delayed
sub-millisecond take, source timestamps, fragmentation/replay/history and ACK
regressions passed. The archived participant binary matches the final build.

Physical run `qos_nanoseconds_20260907_01` exited 0 and completed owned cleanup
on both RK3588A boards. Nineteen cases preserve the original matrix and add
one-nanosecond deadline/lease differences, exact fractional-millisecond metadata
and a 1 ns lifespan expiry control. Ten pairs delivered three exact peer samples
each; eight incompatible pairs had zero matches/delivery; the expiry control
remained matched but delivered no sample. Eight contract tests, 24 actual
receipt adversaries, 18 generic acceptance tests and 11 broker adversaries passed.

- Manifest: `4209f949dad3e73eaae2933174d0cc722834db4f7ba70d579e7d653204f25608`.
- Endpoint receipt: `8d30ce4ca98cd99cfa9b1ec6e4ff59db5792e28c84e4a3dc5a6894d15126c227`.
- MDDS: `c35102eedb8c94402dfd49b8129114528da0f379e31bdd17e612ff08abe4b1db`.
- RMW: `60a3208969c96a133bb4c84aa4cfef780d95afce9a9f794ee01b7d2fc2d9619a`.

No clock synchronization, hard real-time 1 ns scheduling, complete liveliness
recovery, gateway compatibility or unified release completion is inferred.
