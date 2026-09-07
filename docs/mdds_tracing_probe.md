# MDDS tracing lifecycle probe

Status at this feature commit: the two-board probe and independent CTF event
checks pass. The five tracing inventory cases are **not yet accepted** by the
formal CLI receipt gate. Overall coverage remains 77/98. This is not a full
graph, full CLI, or unified release claim. Gateway remains outside this goal.

Run from Git Bash in the ROS workspace:

```bash
MDDS_RUN_ID=cli_trace_probe_20260907_01 \
MDDS_ROS_PROFILE_MODE=implicit MDDS_ROS_CLI_BATCH=trace_probe \
bash scripts/run_mdds_broker_ros.sh
```

Use a fresh run ID on subsequent executions. The normal broker fixture runs
on the explicit A/B RK3588A serials and freezes the MDDS/RMW/rclpy input copies.
Each board then launches a private mount namespace using
`trace_mount_namespace.py`. Its `/var/run` and `/dev/shm` are separate from the
existing system LTTng daemon. All trace commands, LTTng processes and traced
actors share that private namespace; the ordinary MDDS local broker socket
remains accessible. The physical peer path remains DSoftBus Socket/Bytes.

The probe executes the actual installed `ros2 trace start`, `pause`, `resume`,
`stop` and interactive `ros2 trace` entry points. The interactive subprocess
receives each newline only after its corresponding prompt is observed.
Every active, paused, resumed, stopped and interactive actor creates a real
rclpy node and calls the other board's AddTwoInts service with a run-specific
request. Process PID/start, node identity, service response, loaded private
MDDS/RMW paths and actual command return codes are retained.

The host fetches both the report and compressed trace archive with the
existing HDC hash checks. `decode_trace_probe.py RUN_DIRECTORY BOARD_SERIAL`
extracts bounded regular-file archives and invokes WSL Ubuntu-20.04 Babeltrace.
Its separate event oracle requires precisely the active/resumed node and PID
events in the lifecycle session, and the interactive node/PID in the second
session. Paused or stopped events, missing events, duplicate events, wrong
PIDs and cross-session substitution fail. Metadata files alone cannot pass.

## Verified run on 2026-09-07

Run: `cli_trace_probe_20260907_01`.

- Both boards: all five real tracing commands returned 0; all five traced
  actors obtained the expected response from the opposite board.
- Both boards: decoded lifecycle CTF contains exactly active/resumed node
  events and no paused/stopped node event. Interactive CTF contains its actor.
- A trace archive SHA-256:
  `66478f511b2aa21dbdbd05819ffea0929738c791d1df469434a373e798b838ca`.
- B trace archive SHA-256:
  `dfeda6662c286d2cb35d214cdb8ff7e0365ba011c51cbf4073cd239372209f3c`.
- The enclosing real two-board broker run passed pub/sub, service, graph
  ownership/cardinality and retirement checks, plus 11 receipt adversaries.
- Seven event-oracle tests pass after the initial missing-module RED.
- Post-run HDC process inspection found only the existing system LTTng
  daemons (A 12184/12185, B 12300/12301); the probe processes were absent.

Evidence lives in `ohos_test_logs/ros_broker/cli_trace_probe_20260907_01`,
including per-board archives, reports and `*.trace_decoded/verification.json`.
The host run log is in the workspace-level
`verification_evidence/goal1_20260906/trace_probe_01.host.log`.

## Remaining acceptance work

Bind every tracing command to the standard per-case terminal/log receipts,
freeze tracing package/native runtime provenance, validate namespace cleanup
and failure paths as part of the machine gate, and add receipt mutation tests
before admitting the five tracing cases. The current decoder deliberately
records `cli_acceptance_advanced: false`. Only `ros2:rcl_node_init` is selected
here; this does not prove that every available tracepoint is implemented.
