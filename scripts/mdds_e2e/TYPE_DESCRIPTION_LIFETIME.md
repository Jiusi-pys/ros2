# Same-Context type-description service lifetime

This harness runs the standalone rclpy unittest on one RK3588A using production
`rmw_mdds` and the DSoftBus-only profile. Each of its two cases creates one
Context and two Nodes. No local broker daemon is launched. This is component
lifetime evidence; every receipt explicitly leaves cross-board proof false.

The original test file is owned by the rclpy repository:
`src/ros2/rclpy/rclpy/test/test_type_description_service_lifetime.py`.
Its frozen first-RED SHA-256 is
`73060118067f7e0f28adde885c9befedd99886585c90c7b44fa207ce7bab8b92`.
The harness does not modify its assertions, create another Context, disable the
default service, invoke Context.destroy, or call garbage collection.

## Parent-run commands

From the ros2 workspace in Git Bash, select the board explicitly and use a
fresh run ID for each invocation:

```sh
bash scripts/run_mdds_type_description_lifetime.sh --board 3e01ff55454d202020104033bf453b00 --domain 51 --run-id td_lifetime_red_20260906_01
```

By default it freezes `install_ohos/Lib/site-packages/rclpy` and the sole actual
`_rclpy_pybind11*.so` within that package. It copies the complete non-cache Python
package into a bounded archive and replaces the native member only when
`--native <path>` is supplied. The selected native basename must match the
actual installed import suffix and its ELF header must identify AArch64 ELF64.

After the rclpy native fix is built by the parent, use its observed build path:

```sh
bash scripts/run_mdds_type_description_lifetime.sh --board 3e01ff55454d202020104033bf453b00 --domain 51 --run-id td_lifetime_green_20260906_01 --native build_ohos/rclpy/test_rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so
```

The two MDDS/RMW libraries are always frozen from `install_ohos/lib` into a
private run lib directory. The token executable is frozen from
`build_ohos/mdds/mdds_token_exec`; `--token` can select an explicit alternative.
`--package` selects a previously frozen rclpy package to keep Python sources
identical across RED/GREEN. `--wait-seconds` accepts 10..300, default 120.
These commands do not install anything or overwrite shared board deployment.

## Provenance and process ownership

The host stores each complete input under
`ohos_test_logs/type_description_lifetime/<run-id>/` before taking a board lock.
It reuses `graph_stage_artifact` and `graph_fetch_verified`, checking frozen
SHA-256 before launch and after real process completion. The board validates
archive/member hashes, rejects duplicate/traversing/link members, and extracts
only into a fresh run-owned package directory. Archive members are limited to
256, each file to 16 MiB, and total expanded bytes to 32 MiB. `__pycache__`, pyc,
and pyo files are excluded; no Python bytecode is written during the run.

`PYTHONPATH` selects the complete private package; no namespace shim is used.
Before and after the original unittest, the child checks both package/native
`__file__` paths, `/proc/self/maps` for the exact three native library paths,
and their actual file hashes. Every extracted Python package file is also
rechecked against the frozen package manifest. `/proc/self/cmdline` records
the actual child command separately from the original fixture argv.

The profile is copied from the installed `ohos_dsoftbus.env` and sourced after
the board ROS environment. It unsets legacy MDDS_TRANSPORT and selects
SYSTEM_DEFAULT discovery, then the harness pins RMW, isolated domain and the
private LD_LIBRARY_PATH. Any broker socket environment override is removed.
The receipt requires two successful DSoftBus Context-start diagnostics and no
UDP backend selection.

The shared `mdds_owned_processes` launcher uses the explicitly staged
`MDDS_TOKEN_EXEC`. The Python `supervise_command` also execs that token launcher
before its child interpreter, keeping the child's PID/start identity across
exec. Mode permission is applied with the existing fd-bound
`broker_local_run.mark_executable`, after its exact hash check. Permission or
import failure is infrastructure failure, not an accepted lifecycle RED.

`supervise_command` waits for real interpreter teardown, publishes status by
atomic rename, and supplies the child ownership record. The host captures that
record immediately, registers it with the existing owned cleanup, waits for
terminal status, and fetches immutable log/status/hash evidence. Final PASS is
printed only after that helper cleans the owned processes and releases its
board activity lock. No separate process cleaner or broad process kill exists.

## Acceptance and TDD evidence

A successful receipt requires actual child exit 0, exactly two unittest tests
with exact OK (no skips), the matching process/run identity, and all five
expected case/phase observations. It checks context liveness, observer service,
node names, exact service/request/response counts/types, and nonzero endpoint
GIDs. A printed marker alone cannot satisfy the gate.

`host_verification.json` separates `valid_evidence` from `passed`: complete
lifecycle assertions can fail with a genuine exit 1 and still produce useful
RED evidence. Missing phases, failed Context initialization, wrong native
mapping/hash, stale PID identity or a missing actual wait record do not count
as a valid lifecycle RED. The expected original rclpy failure is a retained
service/request-reader/response-writer after the final owner is released.

Host TDD: the initial Python stub produced 8 tests with 8 assertion failures
(including subtests); implementation then passed all eight, with a ninth test
added to distinguish complete functional RED from infrastructure failure.
The shell option stub failed its first routing assertion; implemented option
validation passes without any HDC call. No C++ build or board execution was
performed while preparing this harness.

```sh
.pixi/envs/default/python.exe -B -m unittest discover -s scripts/mdds_e2e -p test_type_description_lifetime_harness.py -v
bash scripts/test_type_description_lifetime_harness.sh
```

A host archive/extract smoke used the actual installed 64-member rclpy package
(1,726,561 expanded bytes). Its native SHA-256 was
`474a481d1934c7d719a433051afcc2f1abcc0f633afe717e2d046da346b722bd`.
This smoke verifies packaging. Actual board lifetime evidence is separate:

- `td_lifetime_red_20260906_01`: both tests failed their final-owner-release
  assertions with complete, valid evidence and actual child exit 1. Log SHA-256
  `742594cc1f679a21944839824fb684c6149c643b80512388a8553f4d1288c7fd`.
- `td_lifetime_green_20260906_01`: both tests and all five observations passed,
  with actual child exit 0 and successful owned cleanup. Log SHA-256
  `83bca57ca7568e8c81bc0295fdc3255e98a298a9d7c1f16266b3ad54b4533465`.

These runs used identical Python files, test, MDDS/RMW libraries and profile.
Only the native extension changed to SHA-256
`e95eb339d4f8e0da438be2bbdfeb8fb34fd96163efbf3467d3e44b6039efcdc7`.
Both receipts retain `cross_board_proven=false`; the fix was exercised through
the run-owned package and did not replace the shared board installation.
