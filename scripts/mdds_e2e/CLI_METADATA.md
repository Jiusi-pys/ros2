# First ROS 2 metadata CLI batch

`board_cli_metadata.py` executes twelve real CLI cases on board A and produces
receipts accepted by the existing `cli_acceptance.py` schema. It does not run
HDC, start ROS nodes or claim transport, graph, gateway or full CLI acceptance.
The unchanged 98-case template remains authoritative; the other 86 cases stay
`NOT_RUN`, and the phase-1/gateway gate remains locked even when this batch passes.

| Cases | Functional oracle |
| --- | --- |
| `pkg list` | Exact installed ament package-marker inventory, including fixture packages |
| `pkg prefix rmw_mdds` | Exact deployed prefix and existing package marker |
| `pkg xml rmw_mdds` | Complete parsed package XML, name and RMW group membership |
| `pkg executables` | Both C++ and Python demo packages, exact executable paths and existing files |
| `interface list` | Exact installed interface sets in the message/service/action sections |
| `interface package` | Complete public `example_interfaces` inventory |
| `interface packages` | Exact installed rosidl-interface package inventory |
| `interface show` | All fields and section separators for String, AddTwoInts and Fibonacci |
| `interface proto` | Parsed String prototype YAML with the exact empty string field |
| `extension_points` | Every declared extension point loads and appears once |
| `extensions` | Every registered CLI command/verb extension loads and appears in its correct group |
| `plugin list` | Exact SQLite3 plugin name, type and base; its registered XML and shared library exist |

The executable command lists are fixed in `CASE_COMMANDS`; `--help` cannot
replace any command. Expected data is read independently from ament resource
files, interface sources, package/plugin XML and distribution entry-point
metadata, without calling the CLI implementation to generate its own oracle.
Relevant sources are the installed versions of `ros2pkg/verb`,
`ros2interface/verb`, `rosidl_runtime_py/get_interfaces.py`,
`ros2cli/command/{extension_points,extensions}.py`, and `ros2plugin/verb/list.py`.
Duplicate demo `console_scripts` names are legitimate and unrelated to the
registered CLI extension groups; duplicate entries inside those groups fail.

## Parent-managed board execution

The host harness establishes ownership, stages all five dependencies, waits for
the real metadata child, collects exact-hash evidence and replays acceptance:

```bash
MDDS_RUN_ID=cli_metadata_20260906_01 ./scripts/run_mdds_cli_metadata.sh
```

It targets board A only. `MDDS_METADATA_WAIT_SECONDS` accepts 2..1200 seconds
and defaults to 1200; a summary file cannot satisfy the wait. The supervisor
uses `board_graph_ownership.supervise_command` to record the actual metadata
process return code and its PID/start identity. It then publishes a bounded
`cli_metadata.tar` and a separate packaging terminal record. The child PID is
registered for exact cleanup before the supervisor PID, and the host prints
batch PASS only after cleanup succeeds.

The collected archive is SHA256-checked before extraction. Only regular flat
JSON/log members are accepted: no directories, links, device names, duplicate
members or path traversal. Limits are 16 MiB archive/expanded bytes, 4 MiB per
member and 128 members. The host requires the original acceptance verifier to
report exactly twelve passing receipts, 86 `NOT_RUN` cases and a locked gateway
gate, in addition to the real metadata-process RC 0. Nonzero child exit remains
a failure even when the board summary and every receipt claim success.

The harness freezes its board dependencies and host verifier under
`ohos_test_logs/cli_metadata/<run-id>` and retains the archive, process log,
PID/status records and host verification report there. It does not replace the
shared board libraries or deployment. The following manual invocation describes
the underlying board runner when a parent already manages its own harness.

The parent must establish the usual MDDS activity lock and a fresh owned run
directory. Its `owner` file must contain the exact run ID:

```text
MDDS_RUN_OWNER RUN_ID=<run-id> LABEL=<safe-label>
```

Stage these three files beside each other in that owned directory, verifying
their transfer hashes: `board_cli_metadata.py`, `cli_acceptance.py`, and the
unchanged `cli_acceptance_manifest.json`. Source the board's existing ROS
environment and OHOS DSoftBus profile before invoking the runner:

```sh
. /data/local/tmp/ros2/env.sh
. /data/local/tmp/ros2/share/rmw_mdds/config/ohos_dsoftbus.env
python3.12 -B /data/local/tmp/ros2/.mdds-owned-runs/<run-id>/board_cli_metadata.py \
  --prefix /data/local/tmp/ros2 \
  --run-id <run-id> \
  --board-serial 3e01ff55454d202020104033bf453b00 \
  --output-dir /data/local/tmp/ros2/.mdds-owned-runs/<run-id>/cli_metadata
```

The output directory must not already exist. The runner writes only inside that
new `cli_metadata` directory; bytecode generation is disabled and subprocess
home/cache/temp locations are redirected there. It requires the exact selected
production profile but does not initialize middleware merely to inspect files.
It waits for each real Python CLI subprocess and records its actual return code;
HDC status is never used as the command verdict. Timeout or interruption kills
the owned CLI subprocess. Parent-owned process cleanup and final log transfer
remain the parent's responsibility.

Outputs include `partial_manifest.json`, `summary.json`, `phase_gate.json`, the
hashed `oracle_context.json`, and one receipt plus raw execution logs per case.
Each log contains actual and normalized argv, stdout/stderr and the existing
run/case/board/command-bound terminal marker. A functional assertion is emitted
only after all commands and oracles for that case pass. Case failures retain
their logs and failure status; a zero runner exit means only that all twelve
selected metadata cases passed. Preflight or output-ownership failure returns 2.

## Host verification

```powershell
& .pixi/envs/default/python.exe scripts/mdds_e2e/test_board_cli_metadata.py -v
& .pixi/envs/default/python.exe scripts/mdds_e2e/test_cli_acceptance.py -v
& .pixi/envs/default/python.exe scripts/mdds_e2e/test_cli_metadata_archive.py -v
& 'C:/Program Files/Git/bin/bash.exe' scripts/test_cli_metadata_harness.sh
```

The host tests cover every selected oracle's positive and negative output,
nonzero process exits and timeouts, missing show variants, misplaced interface
sections, duplicate inventories, missing files, bad XML/YAML, unloaded
extensions, altered plugin classes, owned output paths and real receipt/schema
compatibility. Their synthetic receipts are test fixtures and are not board
acceptance evidence.
