## Context

This workspace contains a ROS 2 Jazzy source tree plus OpenHarmony/KaihongOS deployment helpers under `ohos/` and an in-progress `rmw_mdds_cpp` implementation under `src/ros2/rmw_mdds/rmw_mdds_cpp/`. Current evidence shows meaningful progress:

- host `rmw_mdds_cpp` package and upstream `test_rmw_implementation` conformance can pass;
- board-side `ros2 doctor --report` can run with `RMW_IMPLEMENTATION=rmw_mdds_cpp`;
- native MDDS dual-board pub/sub, service, and action lanes can pass;
- gateway-based FastDDS interop can pass pub/sub matrices, service, action, lifecycle, and parameter flows.

The goal remains broader than those checks. The pasted objective note and current dirty workspace show unresolved completion concerns: OpenSpec tracking was absent in `/home/kaihong/ros2`, true zero-copy loaned sample behavior is not yet proven as end-to-end shared-memory zero-copy, dynamic message take and event completeness must be audited against current code, `LD_PRELOAD` remains present in runtime scripts, security and cross-RMW identity behavior still need explicit gates, and the delivery state is local-only and dirty.

## Goals / Non-Goals

**Goals:**

- Define the evidence required before declaring ROS 2 `rmw_mdds_cpp` and MDDS integration complete.
- Keep future changes under RED-GREEN TDD: every behavior fix or feature closure starts with a failing unit, contract, or board harness check.
- Preserve board-runtime proof as a first-class requirement rather than accepting build-only or host-only results.
- Make remaining feature gaps explicit enough that each can be closed or consciously deferred with evidence.

**Non-Goals:**

- This change does not claim the full objective is already complete.
- This change does not replace the DSoftBus MDDS design documents or archived DSoftBus OpenSpec records.
- This change does not require unrelated ROS 2 packages to be fully ported if they do not participate in `rmw_mdds_cpp` runtime closure.
- This change does not authorize destructive worktree cleanup or remote pushes.

## Decisions

### Decision: Treat Board Evidence As Required

Host conformance is necessary but not sufficient. The completion gate includes RK3588/KaihongOS board-side `ros2` execution because previous runtime closure work showed that builds can pass while Python modules, environment variables, log paths, or bridge payloads still fail on device.

Alternative considered: accept host `ctest` plus local scripts as completion proof. This is rejected because the goal asks for ROS 2 calling MDDS on the actual runtime, and the user preference is board-side evidence for ROS 2/OpenHarmony work.

### Decision: Keep Native MDDS And Gateway Interop Separate

Native `rmw_mdds_cpp` over MDDS/DSoftBus and `mdds_dds_gateway` interop with FastDDS prove different behavior. Completion evidence must show both:

- native MDDS lanes for rmw_mdds-to-rmw_mdds communication;
- gateway lanes for cross-RMW interoperability with FastDDS.

Alternative considered: use gateway success as proof for native RMW behavior. This is rejected because gateway routing can hide native RMW gaps.

### Decision: Use Script Contracts For Board Harness Correctness

Board scripts are part of the product evidence. They must fail on missing runtime files, wrong RMW selection, report-class failures, missing bridge counters, and lane-level failures. Contract scripts in `ohos/test_rmw_mdds_*_contracts.sh` provide cheap RED tests before harness edits.

Alternative considered: rely on manual log inspection after board runs. This is rejected because it is not repeatable and can overclaim success when a script prints a PASS despite weak evidence.

### Decision: Track Remaining Gaps As Requirements, Not Narrative Notes

The remaining work items from the objective note become explicit requirements: true zero-copy, dynamic message take, event completeness, `LD_PRELOAD` dependence, security/cross-RMW identity, and clean delivery. Each must have an evidence gate before the final goal can be marked complete.

Alternative considered: treat the pasted note as stale and ignore it after recent improvements. This is rejected because the goal still says "perfect" and the current checkout has not produced stronger evidence for every item.

## Risks / Trade-offs

- [Risk] Board-side HDC exits with status 139 after valid output, which can make naive scripts report false failure.
  [Mitigation] Scripts must key on explicit board markers and reject known HDC connection failure text separately.

- [Risk] Gateway counters are emitted periodically, so sampling immediately after a ROS 2 action or service can miss the final reply count.
  [Mitigation] Harnesses must wait for the relevant counter to advance before PASS.

- [Risk] OpenSpec artifacts in the ROS 2 root may overlap conceptually with DSoftBus MDDS OpenSpec records.
  [Mitigation] This change scopes itself to the ROS 2 workspace and references DSoftBus records as external implementation context.

- [Risk] "All ROS 2 features" can expand without a completion boundary.
  [Mitigation] The spec defines concrete runtime surfaces and explicitly requires a requirement-by-requirement audit before `update_goal complete`.

## Migration Plan

1. Keep existing working runtime scripts and board artifacts intact.
2. Add missing contract checks before any further behavior changes.
3. Close remaining feature gaps one at a time using RED-GREEN TDD.
4. Re-run host conformance and board runtime gates after each behavior change.
5. Do not mark the goal complete until OpenSpec tasks are checked off and the final audit proves every requirement.

Rollback for script-only changes is to restore the previous script version and rerun the corresponding contract. Rollback for `rmw_mdds_cpp` implementation changes must include the failing test that motivated the change and the command output proving rollback restores the old failure.

## Open Questions

- Should the final delivery target be a local commit only, a Gerrit patch, a GitHub PR, or a pushed branch?
- Should true zero-copy be defined as no payload copy across `rmw_mdds_cpp` to MDDS SHM, or is a bounded-copy loaned-message compatibility mode acceptable for an interim milestone?
- Should `LD_PRELOAD` removal be required for all board scripts, or can it be retained as an explicit platform workaround with a documented reason?
