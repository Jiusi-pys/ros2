# MDDS ROS 2 CLI and graph acceptance

`cli_acceptance_manifest.json` is an inventory and test specification, not a
test result. It was generated from the local `install_ohos` Python distribution
metadata: **22 top-level commands, 82 functional CLI cases, 13 graph cases and
3 transport cases**. Every case starts at `NOT_RUN`. The board deployment still
needs to be checked against these exact metadata files and hashes before a run.

The verifier never imports AArch64 Python extensions on Windows. It reads
`entry_points.txt` from installed egg/dist metadata and binds the snapshot to
the exact metadata bytes. Extra commands, absent commands, changed entry-point
targets, duplicate registrations and unmapped verb groups cannot silently pass.
The local sources for the main inventory are `src/ros2/ros2cli/*/setup.py`,
`src/ros2/rosbag2/ros2bag/setup.py`, and
`src/ros2/launch_ros/ros2launch/setup.py`. Installed `ros2trace`, `ros2test`,
`ros2plugin` and `sros2` add cases beyond those three repositories.

## Commands

Run from the ROS 2 meta repository with its pinned Python interpreter:

```powershell
& .pixi/envs/default/python.exe scripts/mdds_e2e/test_cli_acceptance.py -v

# Create a new run template; existing files are never overwritten.
& .pixi/envs/default/python.exe scripts/mdds_e2e/cli_acceptance.py inventory `
  --prefix install_ohos --output C:/run-owned-directory/cli_manifest.json

# Both gates return 1 until every required functional case has passed.
& .pixi/envs/default/python.exe scripts/mdds_e2e/cli_acceptance.py verify `
  --prefix install_ohos --manifest C:/run-owned-directory/cli_manifest.json `
  --evidence-root C:/run-owned-directory --phase 1 `
  --report C:/run-owned-directory/phase1_report.json

& .pixi/envs/default/python.exe scripts/mdds_e2e/cli_acceptance.py verify `
  --prefix install_ohos --manifest C:/run-owned-directory/cli_manifest.json `
  --evidence-root C:/run-owned-directory --phase 2
```

`--phase 2` checks **permission to begin gateway implementation/acceptance**.
It does not certify the gateway; `gateway_status` remains `NOT_TESTED` even
when the phase-1 prerequisite passes. Existing board/gateway runners do not yet
invoke this new verifier automatically. The coordinating workflow must require
its successful exit before advancing; a failed report is not an optional warning.

## Minimum first complete run

1. Pin source/build/deployed artifact hashes, board serials, a fresh run ID and
   isolated fixture names. Read installed entry-point metadata back from both
   boards and compare its hashes to this snapshot. Use the owned-process helpers
   and real remote terminal records; HDC's host exit status is insufficient.
   Source the deployed `share/rmw_mdds/config/ohos_dsoftbus.env` in every ROS
   fixture and the daemon. This pins `RMW_IMPLEMENTATION=rmw_mdds`, unsets the
   legacy `MDDS_TRANSPORT` override, and selects
   `MDDS_DEPLOYMENT_PROFILE=ohos_dsoftbus` with
   `ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT`. Setting only
   `MDDS_TRANSPORT=dsoftbus` can leave the default SUBNET policy active and
   disable DSoftBus during initialization. Capture DSoftBus API plus exact
   application data under the selected deployment profile.
2. Run graph RED fixtures before making the graph fix: two nodes in one remote
   context with disjoint endpoints, duplicate names in distinct participants,
   exact verbose metadata and by-node ownership. Then run guard wakeup, churn,
   late join, abrupt exit, reconnect, domain isolation and discovery-OFF controls.
3. Use the same isolated fixtures to execute every node/topic/service/param/action
   verb on A against B, then reverse the data/transport paths. The manifest gives
   the functional oracle per verb. `action send_goal --feedback` must accept the
   goal, return exact Fibonacci output, deliver feedback and finish SUCCEEDED.
   `service echo` needs a fixture with service introspection enabled;
   `topic delay` needs a timestamped message fixture with a declared clock bound.
4. Start a lifecycle node and a component container on B. Execute every lifecycle
   transition and component load/list/unload/standalone operation from A. Verify
   remote graph ownership and removal as well as service return values. Exercise
   daemon start/status/stop and repeat graph checks with and without the daemon.
5. Record remote samples to both sqlite3 and mcap, verify metadata, replay exact
   samples, burst from a paused player, convert a bag and reindex a scratch copy.
   Run C++/Python fixtures via `run` and multi-node fixtures via `launch`; execute
   a real launch test via `test`. Exercise trace start/pause/resume/stop and decode
   ROS events. Each owned long-running process must stop with a recorded terminal
   result; an expected timeout is not a functional PASS.
6. Execute the metadata/file-generation utilities (`pkg`, `interface`,
   `extension_points`, `extensions`, `plugin`, security artifact commands) in a
   run-owned scratch directory, and verify their resulting data/files. Exercise
   doctor/wtf and hello against live fixtures. `multicast send/receive` is an
   independent UDP diagnostic utility; its traffic must be isolated and cannot
   count as MDDS communication or authorize an MDDS UDP fallback.
7. Collect complete receipts for all 98 cases, run the verifier and archive the
   report with the raw evidence. Any `NOT_RUN`, `SKIP`, `BLOCKED`, `FAIL`, unknown
   status, missing/extra case, hash drift or incomplete receipt keeps phase 1
   failed and the gateway start gate locked.

This is command/verb coverage plus explicit graph/transport requirements.
Each recipe may need several executions, QoS variants and payload types; one
receipt groups those executions. It is not a claim that a help parser or one
happy-path invocation tests every option or proves the absence of all bugs.

## Receipt contract for functional runners

Keep the generated inventory, target, case ID, command, requirement, assertion
IDs and `execution_boards` unchanged. Set the run manifest's `run_id`, and set a
case to `PASS` only after its runner has passed the full recipe. Each case has
one `evidence` reference containing a relative POSIX `path` and file `sha256` for
a UTF-8 JSON receipt inside the evidence directory. The receipt contains:

- `schema_version: 1`, the exact `run_id` and `case_id`, `kind: "functional"`,
  `status: "PASS"`, both `board_serials`, `rmw_implementation: "rmw_mdds"`,
  and `transport: "dsoftbus"` (the selected middleware transport).
- Nonempty `executions`: each has literal `argv`, integer `returncode: 0`,
  `board_serial` and a raw `log` reference (`path`, `sha256`). CLI invocations
  use the normalized `ros2` argv; the actual runner/launcher command should also
  be retained in the raw log. Cross-board recipes need execution logs from both
  board serials, including the peer fixture and its clean shutdown.
- Exactly the case's declared `assertions`: each has its `id`, `passed: true`,
  the zero-based `execution` index and a nonempty literal `pattern` that was
  observed in that execution's raw log. The runner must check the recipe's exact
  expected values before recording this assertion. A terminal marker alone
  cannot satisfy a functional assertion.

Each raw log includes exactly one line returned by:

```python
from cli_acceptance import terminal_marker
line = terminal_marker(run_id, case_id, returncode, normalized_argv, board_serial)
```

The marker binds the run, case, return code, board and command hash. Preserve the
remote command result rather than manufacturing zero from an HDC return code.
The coverage verifier checks receipt/log integrity, identity and assertion
coverage; it does not independently reconstruct every ROS protocol assertion
from free-form output. Functional runners and their tests remain necessary.

## Existing coverage gaps

`scripts/run_mdds_cli.sh` currently has nine scenarios. It does not drive most
installed verbs, and its action scenario substitutes `cli_probe_action.py` for
`action send_goal`. That probe is useful diagnostic evidence, but cannot pass
the action functional case. `run_mdds_e2e.sh` invokes a C++ action client and its
parameter check only searches for the word "parameter"; neither is full CLI
acceptance. `run_ohos_generic_acceptance.sh` has useful genuine action, lifecycle
and bag patterns but restricts its RMW to Fast DDS/Cyclone DDS, so its results
cannot certify rmw_mdds.

The current `run_board_tests.sh` and `_parse_ctest_env.py` have no
`__rmw_mdds` name filter. `AGENTS.md` still says those tests are excluded at both
ends; that description is stale. The parser does skip Python/launch wrappers
without a native executable, so board CTest totals alone are not CLI/graph
acceptance. The new files do not modify these existing runners.
