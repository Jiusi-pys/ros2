## Why

The `rmw_mdds_cpp` work has strong host and dual-board evidence for core ROS 2 paths, but the goal is broader than isolated smoke tests: it asks for ROS 2 to call MDDS through `rmw_mdds` perfectly, with TDD and OpenSpec-backed closure. A durable completion track is needed so runtime evidence, remaining feature gaps, and delivery readiness are judged against explicit requirements instead of ad hoc pass markers.

## What Changes

- Introduce an OpenSpec capability that defines what "complete rmw_mdds feature closure" means for this workspace.
- Require host conformance, board-side doctor, native MDDS dual-board, and gateway interop evidence to be reproducible through checked scripts.
- Require verification for ROS 2 pub/sub, services, actions, lifecycle, parameters, graph/doctor, QoS-relevant lanes, and cross-RMW gateway paths.
- Track remaining gaps from the active goal note, including true zero-copy loaned samples, dynamic message take, event completeness, LD_PRELOAD dependence, security/RIHS/cross-RMW identity, and clean delivery state.
- Require future implementation changes under this change to follow RED-GREEN TDD with a failing contract or unit test before production code changes.

## Capabilities

### New Capabilities

- `rmw-mdds-feature-closure`: Defines the requirements and evidence gates for declaring the ROS 2 `rmw_mdds_cpp` and MDDS integration complete in this workspace.

### Modified Capabilities

- None.

## Impact

- Affected code and artifacts:
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/**`
  - `ohos/tools/run_*rmw_mdds*.sh`
  - `ohos/test_rmw_mdds_*_contracts.sh`
  - `ohos/stage_colcon_runtime_closure.sh`
  - board-side runtime payloads under `/data/local/tmp/ohos-colcon-rk3588a`
- Affected systems:
  - Host `ctest` conformance for `rmw_mdds_cpp`
  - RK3588/KaihongOS board runtime over HDC
  - MDDS/DSoftBus native transport
  - `mdds_dds_gateway` cross-RMW interop with FastDDS
