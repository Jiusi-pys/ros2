# MDDS tracing lifecycle probe

Status at this feature commit: five tracing inventory cases have passed the
formal CLI receipt gate on both boards. Overall case-receipt coverage is
81/98 after completing both C++ and Python `run` coverage; the incomplete
multi-node `launch` claim remains withdrawn.
This is not a full graph, full CLI, or unified release claim. Gateway remains
outside this goal. The historical tracing batch added five valid case receipts;
the later scope audit independently removed two earlier overbroad claims.

Run from Git Bash in the ROS workspace:

```bash
MDDS_RUN_ID=cli_trace_formal_20260907_01 \
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
rclpy node, calls the other board's AddTwoInts service with a run-specific
request, publishes its exact run/nonce/role/phase payload once and waits for
the peer callback acknowledgement. Process PID/start, node identity, service
response, loaded private MDDS/RMW paths and actual return codes are retained.
The source fixture independently records all five ordered peer publications.
The 94 tracing Python/native files are compared with a frozen host manifest
before and after tracing. Actors also record their actual tracing mappings.

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

## Formal receipt run on 2026-09-07

Run `cli_trace_formal_20260907_01` completed normally through HDC, including
the enclosing cross-board DSoftBus broker/graph baseline. Both boards executed
all five real CLI operations, received all five peer publications, produced
the expected CTF event identities, and reported no remaining private tracing
processes after normal cleanup. The system namespace is explicitly excluded.

- Formal manifest SHA-256:
  `e9c2960d18f8f9942c54d10091a7cf6e5d62100ac36703f413d89a7a78d45c16`.
- Frozen tracing runtime manifest (94 files):
  `5c318d987876e2323fb018a6c16bdb1c17a56cca9dc1234d761cce9307e270c7`.
- A trace archive:
  `8eb8a98ee59daa397e3bfa538501c0f695b9ec538c40e73e645458e211414eb8`.
- B trace archive:
  `657c0fcb00fcc52f6e82d131ba5a3d26da8c9a03ce5eb1fb3317636c3309e031`.
- Validation: 7 event tests, 13 structured report tests, 14 real-receipt
  adversaries, 18 general CLI gate tests, and 11 broker receipt adversaries.

`verify_trace.py` binds actual argv/PID/start/terminal markers, archived output,
private namespace, runtime manifest, normal cleanup, independently recorded
peer publications and decoded event identities. `verify_cli_daemon.py` adds
the five case receipts only after these checks. The decoder itself retains
`cli_acceptance_advanced: false`; decoding alone does not issue acceptance.

## Remaining release work

The original `cli:run` receipt contained only a C++ talker. That gap is now
resolved by `cli_run_languages_20260907_03`, which verifies both languages on
both boards; see [process CLI acceptance](mdds_process_cli_acceptance.md).
The `cli:launch` receipt contains one launched node
although its recipe requires multiple nodes. These were verified directly
against both boards' stored actual argv and the launch definition; the old
artifacts remain intact as narrower evidence, while the current coverage
ledger still excludes `cli:launch` until its missing tests are implemented.
The remaining 16 graph/transport cases and single-release provenance gate
are also incomplete. Only `ros2:rcl_node_init` is selected here; these results
do not prove that every available tracepoint is implemented.

## Namespace failure cleanup

`owned_trace_namespace.py` supervises the private workload. The mount helper
writes PID/start/namespace identity through an inherited pipe, then waits.
The supervisor validates that identity, opens a namespace descriptor to keep
the namespace identity alive, and only then releases the workload. Cleanup
rechecks PID/start/namespace before signalling and rescans after termination
to include late-forked descendants. It rejects the system and supervisor's
mount namespaces. A successful worker with surviving descendants is a failed
test, even when the supervisor subsequently cleans them up.

Native RK3588A TDD evidence:

- RED: four cases, three failures. Worker failure and timeout leaked a child;
  successful worker exit with a leak was incorrectly accepted.
- GREEN: six cases on **each** board, zero failures. Cases cover timeout,
  failure, successful exit with leak, normal completion, supervisor SIGINT
  and supervisor SIGTERM. All cases retain a separate system-namespace
  sentinel and assert that it remains alive.
- RED archive SHA-256:
  `5e2f88ddabc6aaca611defb93ea61707ea9930409ae9e960a7e8add2d000d827`.
- Final tested input archive SHA-256:
  `85c2916d697e6da96e0c50116738ec7262ee805ae1e04ae0ba5f5151aab103df`.
- RED log SHA-256:
  `bb11544383063e3b2db4b7f9d6ff6a926aef935b0a190d1b12826ffca7c4e13e`.
- Final A/B logs:
  `b9fb3a0fb9169c081b6858c302664cee11ec396f8171e2f18fffa510ba7be2dc`,
  `7cb0d0db6356e7a3c8d8c42f5fb706904fe333e08ccb225d20109e96dfcfe891`.

Logs and archives live in the workspace-level
`verification_evidence/goal1_20260906/trace_cleanup_*` files. RED teardown
removed only fixture-recorded PID/start identities from their private
namespace. Supervisor SIGKILL/power loss cannot execute in-process cleanup
and is outside these six fault-injection claims.

The repaired supervisor also passed the full normal two-board run
`cli_trace_cleanup_20260907_01`: DSoftBus data/services/graph baseline, five
tracing CLI cases, decoded CTF identities, and both normal supervisor receipts
showing `completed: true`, return code 0, and no initial/residual cleanup
members. Its CLI manifest SHA-256 is
`45fb4b69ce12a9b9301c7b797ec98739fa047f36a0437125020b866d79ea37c1`.
All 17 real-receipt adversaries pass, including missing supervisor receipt,
nonempty cleanup and wrong launched workload. The frozen board input inventory
determines whether the new supervisor receipt is mandatory; removing a source
copy from a receipt-checking directory cannot disable that check.
