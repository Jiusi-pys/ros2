# Isolated local broker acceptance

Run from `ros2/` in Git Bash after building `mdds_broker_local_overlay`,
`mdds_broker_local_test_daemon` and the matching `rmw_mdds` library:

```sh
MDDS_RUN_ID=bl_local_01 bash scripts/run_mdds_broker_local.sh --scenario all
```

The default board is A. `--board <exact HDC serial>` selects another board.
Run IDs contain 1..32 letters, digits or underscores so the owned Unix socket
path fits `sockaddr_un`. Existing run directories are never reused.

The harness stages both libraries, daemon, profile and Python helpers under
one private owned directory, freezes their SHA-256 hashes and verifies them
before launch. It checks the libraries actually loaded through `/proc`,
rejects UDP sockets owned by each ROS probe and records real child exit status
including interpreter teardown. Every launched process is tracked by PID and
kernel start time; cleanup targets only those processes.

`--scenario contexts` tests two contexts in one process, exact bidirectional
payloads, endpoint ownership/GIDs, reliable acknowledgment waits, retirement
of alpha and communication between surviving beta and a new gamma context.
`--scenario processes` tests distinct alpha/beta processes with an explicit
completion barrier and observes alpha's removal from beta's graph. `all` runs
both. The daemon must report its exact READY and a successful STOP with an
empty final actor state. The host reports PASS only after cleanup succeeds.

`--variant baseline --scenario contexts` loads the current direct DSoftBus
library to reproduce the old second-context failure. `--variant overlay`
loads the test library built from real MDDS sources plus the broker-client
factory. The overlay is not installed, does not link the DSoftBus SDK and
cannot prove physical cross-board communication. Every result explicitly
records `physical_dsoftbus_proven=false`.

Evidence is retained under `ohos_test_logs/broker_local/<run-id>/`. The host
validator has nine tests covering false success, wrong identities, missing
payloads, library/UDP evidence and descriptor-bound executable permissions.
On 2026-09-06, `bl_ack_red_20260906` established two actual ROS contexts and
received five exact payloads in each direction, then failed on the existing
RELIABLE `wait_for_all_acked` UNSUPPORTED result. The daemon exited normally
and passed cleanup validation. That run is RED for this gate; neither partial
delivery nor the test-only factory counts as full ROS or DSoftBus acceptance.
