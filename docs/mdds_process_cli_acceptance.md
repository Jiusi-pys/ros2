# CLI-created native ROS processes

## ros2 run

```bash
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=process_run \
  bash scripts/run_mdds_broker_ros.sh
```

The fixture stages the actual demo_nodes_cpp talker executable and its
libtalker_library.so implementation into a fresh private prefix. Actual
`ros2 run demo_nodes_cpp talker` resolves that prefix and supplies explicit
node, namespace and topic remappings. Native process evidence must bind the
exact executable, argv, parent PID, process group, start time and hashes of
the executable, implementation library, MDDS and RMW. Owned UDP sockets are
rejected; native transport logs must select the DSoftBus broker.

Both CLI processes complete their initial cached/direct node queries before
a two-board start barrier. Independent peer subscribers then require at
least three consecutive Hello World samples, the expected node/type/hash/GID
metadata and matching native publication logs. Only after both peers have
received these samples does the host authorize stopping the owned process
groups with SIGINT. Both the CLI and native child must exit normally without
emergency cleanup. Each peer must observe the publisher and node disappear
before the CLI daemon's final lifecycle checks.

Run `cli_process_run_20260907_01` passed on both RK3588A boards with the
ordinary harness exit zero. It also retained the base thirty cross-board ROS
messages, four service calls and graph retirement/peer-withdrawal checks.
The partial CLI manifest SHA-256 is
`3f4fd52983fdc871991a8ad38929f890cf6c2bc5f4f17a5f58e2b7067c6dede1`.

Six process-identity unit tests were introduced RED before implementation and
passed GREEN. Eleven actual-receipt adversaries passed, covering foreign
parents/groups/binaries, UDP, emergency stop, a surviving child, stale stop
barriers, missing peer messages, incorrect endpoint types and ghost nodes.
The eleven base ROS receipt tests also passed.

```bash
python scripts/mdds_e2e/check_cli_process_receipt.py \
  ohos_test_logs/ros_broker/cli_process_run_20260907_01
```

This adds the `cli:run` case, bringing the aggregate CLI/graph/transport ledger
to 68/98. Launch/test, remaining commands, the full graph matrix, historical
stress observations and final single-release acceptance remain unfinished.
No gateway acceptance or push is implied.

## ros2 launch

Mode `MDDS_ROS_CLI_BATCH=process_launch` runs the staged
`process_talker.launch.py` through the actual CLI, with declared launch
arguments for node name, namespace and output topic. It uses the same
cross-board data/start/stop/withdrawal contract as run. The supervisor signals
only the launch CLI, so the launcher must forward SIGINT and collect its child.
Acceptance requires the launch start, forwarding and native exit events,
the exact native child and library hashes, and a native exit code of zero.
A launcher exit code of zero alone is insufficient.

This test exposed two real OHOS shutdown failures before acceptance:

- `cli_process_launch_20260907_01`: native talker SIGSEGV (-11) inside FFRT,
  called through rclcpp's incorrect single-argument signal-handler ABI.
- `cli_launch_signal_fix_20260907`: after correcting the ABI, FFRT's old
  dispatcher requeued SIGINT and the native process exited -2.

The rclcpp fix selects sigaction on OHOS, distinguishes restored dispositions
from chainable callbacks, and retains the system FFRT dispatcher for uninstall
without calling it again for a ROS-handled SIGINT/SIGTERM. Ordinary user
handlers and their saved masks remain covered by native regression tests.
See the rclcpp package's `OHOS_SIGNAL_HANDLING.md` for native RED/GREEN evidence.

`cli_launch_ffrt_fix_20260907` passed on both boards with exact private rclcpp
SHA-256 `cde55d5d806eb738c98fbb5c0d34d7e7cb8b5b7b4c41b7141d8507972fa1cd55`.
Both launcher and native child exited zero, peer data matched native publishing
logs, and graph withdrawal completed. Partial manifest SHA-256:
`0f5d03fbcf2276eec116f5be4fde97e82ccbecba60ac3c93876d31ac6de15559`.
The matching run regression `cli_run_ffrt_fix_20260907` also passed; manifest
`da912da1e6f7e966067aff26509a35b4401081f0d19ef5bf59806d1f00ea88c2`.

Five launch/startup tests and five native-exit parser tests pass, including a
pre-exec child window, PID reuse, missing/conflicting exit events and native
crash despite a successful launcher exit. Fifteen actual launch-receipt
adversaries and eleven run-receipt adversaries pass. The verifier rejects
changed launch definitions, missing forwarding, malformed stdout boundaries,
wrong processes, missing peer messages and surviving graph entities.

```bash
python scripts/mdds_e2e/check_cli_launch_receipt.py \
  ohos_test_logs/ros_broker/cli_launch_ffrt_fix_20260907
```

Launch adds one unique case, taking the aggregate ledger to 69/98. The tests
use private libraries; board-wide deployment and the full graph/single-release
gates remain unfinished.

## ros2 test

Mode `MDDS_ROS_CLI_BATCH=process_test` configures the standalone
`scripts/mdds_e2e/fixtures` CMake project with Ninja and installs its package,
ament resource marker and launch-testing definition into a private prefix.
Keeping this test package above the middleware avoids adding a client-library
dependency cycle to rmw_mdds. The runner stages the installed files with hashes
and invokes the real `ros2 test` CLI with `--package-name mdds_cli_fixture` and
an explicit `--junit-xml` output file.

Each board executes three real assertions: the native talker publishes the
expected messages using DSoftBus; the peer observer reports consecutive
messages and the expected endpoint; and launch-testing observes native exit
zero after shutting the process down. The host releases a two-board data
barrier, then the tests return and the framework performs shutdown. The outer
supervisor sends no normal stop signal in this mode. It still checks the exact
native child, library hashes, exit event and graph withdrawal.

The JUnit verifier requires the exact three test names and classes, consistent
counts, zero failures/errors and no skipped tests. It also checks the installed
definition, test configuration and runtime assertion records against the actual
CLI logs and child PID. Current Jazzy combines active and post-shutdown results
in one test-run suite. Run `cli_process_test_20260907_01` remains failed evidence:
the real assertions passed, but the initial checker incorrectly required two
suites and caused the batch supervisor to fail.

Corrected run `cli_process_test_20260907_02` passed on both RK3588A boards:
three tests per board, native/CLI/supervisor exit zero, exact peer data and graph
withdrawal, plus the base thirty cross-board messages and four services.
Partial manifest SHA-256:
`9411cc1995d33dd4d342ee39505629d05e0cba00ce643e988c3ebad61a0ab758`.
Seven XML contract tests and the installed-test recipe regression are GREEN
after RED failures. Eighteen actual-receipt adversaries passed, including
rehashed hidden failures/skips, false totals, missing assertions, altered
definitions/configuration and incorrect native exit evidence. The existing
fifteen launch-receipt checks still pass.

```bash
python scripts/mdds_e2e/check_cli_test_receipt.py \
  ohos_test_logs/ros_broker/cli_process_test_20260907_02
```

This adds `cli:test`, bringing the combined CLI/graph/transport ledger to
70/98. Remaining commands, full graph scenarios and the final single-release
gate remain unfinished; gateway work is still locked.
