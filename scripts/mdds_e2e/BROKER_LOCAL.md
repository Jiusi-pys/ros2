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

To use the repaired rclpy service owner without changing shared deployment,
select its native extension explicitly:

```sh
MDDS_RUN_ID=bl_fixed_rclpy_01 bash scripts/run_mdds_broker_local.sh --scenario all --rclpy-native build_ohos/rclpy/test_rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so
```

`--rclpy-package <directory>` optionally freezes a specific Python package
source and requires `--rclpy-native`. Otherwise Python sources come from
`install_ohos/Lib/site-packages/rclpy`. The already-tested lifetime helper packs
the complete non-cache package, replacing only its sole native `.so` member
while retaining the actual installed import suffix. Archive/member bounds,
path/link rejection and exact hashes are the same as the lifetime harness.

The archive is extracted only to the owned run's `python/rclpy`, and only an
optional run-owned PYTHONPATH prefix activates it. No shared package or native
extension is replaced. Empty native/package options are rejected. Omitting
both options keeps the existing gate and environment behavior.

When selected, each context result's before/after provenance checks the actual
rclpy package and native `__file__`, the sole `/proc/self/maps` native path,
selected native SHA-256, immutable manifest hash, and every extracted package
file. Each worker adds a second check after its existing completion/release
barrier. Host validation requires all of these observations to match the
frozen package; simply staging a new `.so` cannot pass. All existing data,
reliable ACK, node/endpoint ownership, alpha retirement and fresh-gamma checks
remain required. New full acceptance runs should select the fixed native
explicitly; this option does not turn local broker evidence into DSoftBus proof.

Evidence is retained under `ohos_test_logs/broker_local/<run-id>/`. The host
validator has twelve tests covering false success, wrong identities, missing
payloads, library/UDP evidence, descriptor-bound executable permissions and
optional rclpy before/after import/mapping/hash validation. The new three tests
first failed against the optional-proof stub while the original nine passed;
all twelve pass after implementation. Shell native-option and empty-option
checks also completed RED/GREEN without HDC access. The nine shared package/
lifetime helper tests remain green. No board run was made while adding this
optional overlay feature.
On 2026-09-06, `bl_ack_red_20260906` established two actual ROS contexts and
received five exact payloads in each direction, then failed on the existing
RELIABLE `wait_for_all_acked` UNSUPPORTED result. The daemon exited normally
and passed cleanup validation. That run is RED for this gate; neither partial
delivery nor the test-only factory counts as full ROS or DSoftBus acceptance.

`bl_fixed_rclpy_20260906_01` subsequently passed the complete `all` scenario
using the repaired native extension (SHA-256
`e95eb339d4f8e0da438be2bbdfeb8fb34fd96163efbf3467d3e44b6039efcdc7`).
The contexts case passed exact bidirectional payloads, ACK waits, alpha removal
and beta/fresh-gamma communication. Separate alpha/beta processes passed their
payload, completion-barrier and peer-retirement checks. All three ROS children
and the daemon exited 0; every per-role validator returned no errors, the two
worker PIDs were distinct, and owned cleanup completed before the final PASS.
The contexts log SHA-256 is
`99e8443e79275f4f8e893c6c9837c53b4181f1e3067715671b2ba3e3afd05c1d`.
Sibling logs, status records, input hashes and `evidence.sha256` preserve the
full run. Its physical-DSoftBus proof flag remains false.
