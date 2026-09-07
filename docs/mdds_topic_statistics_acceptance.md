# Cross-board topic statistics CLI acceptance

```bash
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=statistics \
  bash scripts/run_mdds_broker_ros.sh
```

The two RK3588A fixtures each publish twenty nonce-bearing samples at a nominal
5 Hz for the actual `ros2 topic hz`, `bw` and `delay` commands. An independent
peer subscription checks every ordered payload, serialization size and receipt
time. Publisher matching requires both the CLI subscription and the independent
observer before the finite stream starts. The middleware uses the private
MDDS/RMW build and native DSoftBus broker; process paths, hashes, owned sockets
and physical link logs must agree.

Hz and bandwidth use fixed-size String messages. The checker compares all
reported full-window rates with the independently observed stream, allowing
bounded process-scheduling differences. The reported bandwidth sample sizes
must match independently calculated CDR String sizes, including encapsulation,
length and terminator. A valid row cannot hide a later invalid full-window row.

Delay uses PointStamped. The receiving board requests a known header timestamp
from its own clock, two seconds in the past; the request crosses DSoftBus to
the sender, which puts that exact timestamp into every sample. The receiver
checks the stamp, frame ID, point values and serialized size. CLI delay values
must agree with the observed timestamp-age range. This controlled header-age
test checks the command's time calculation without assuming synchronized board
clocks; it is not a measurement of physical transport latency.

These commands run continuously. After twenty verified samples and valid CLI
statistics, their supervisor captures the pre-signal output and sends SIGINT
to its exact PID/start-time child. The native CLI returns 2 on KeyboardInterrupt;
that real exit code is preserved. Acceptance permits this only with a matching
controlled-stop marker, child identity, captured-output hash and successful
functional evidence. Emergency cleanup and unrequested exits fail. A timer may
append a valid row during signal delivery, so the pre-signal observation is
kept separately, must prefix the final output, and both are checked.

Run `cli_statistics_20260907_01` passed on both boards on 2026-09-07:

- All six real statistics commands returned 2 following their requested SIGINT.
  The batch supervisor and ordinary host harness returned zero.
- 120 statistics samples were checked, alongside the base fixture's thirty
  pub/sub messages, four services, graph retirement and peer withdrawal.
- A-board Hz reports were about 4.95 Hz; bandwidth sample size was exactly
  107 bytes. Both boards' full outputs passed their stream-derived bounds.
- Partial manifest SHA-256:
  `e97b97bda0f5f0c6f0e539c2a1a3b830e15e0f161683c53fa1ae465c3d7e0bb9`.
- Eight statistics-output tests, eleven controlled-stop tests, seventeen core
  acceptance tests and three native-path tests passed. The new controlled-stop
  and native-path cases were RED before implementation. The mixed good/bad-row
  test was also RED before the checker required all full-window rows to agree.
- Eleven adversarial statistics receipt tests and eleven base ROS receipt tests
  passed, including source loss, changed payload/size/stamp, reversed receive
  times, foreign libraries, UDP, emergency stop and fabricated observation.

```bash
python scripts/mdds_e2e/check_topic_statistics_receipt.py \
  ohos_test_logs/ros_broker/cli_statistics_20260907_01
```

The aggregate CLI/graph/transport ledger reaches 67/98. Remaining CLI commands,
the full graph matrix, unresolved historical stress observations and final
single-release acceptance remain separate, unfinished gates. Gateway work is
still locked and no push has occurred.
