## Why

The current `rmw_mdds_cpp` implementation has strong scoped evidence for ROS 2 over MDDS/DSoftBus, including host conformance, dynamic take, event support, fixed-size loaned-message zero-copy, SROS2 policy gates, and dual-board runtime proof. The active goal still uses a broader standard: "perfect/all ROS 2 middleware features" with upstream-ready delivery, so a final acceptance track is needed to prevent the completed scoped work from being mistaken for universal parity.

## What Changes

- Define the final acceptance boundary for declaring `rmw_mdds_cpp` and MDDS integration complete under the broad "perfect/all ROS 2 features" goal.
- Require gap tests for any remaining unsupported or scoped surfaces, including generalized loaned messages, full security semantics, and upstream delivery.
- Preserve the completed runtime and zero-copy/security evidence as prerequisites, while making clear which evidence is still insufficient for a universal parity claim.
- Require each remaining implementation gap to begin with RED tests or executable contracts before production changes.
- Require a final audit that either proves full parity, explicitly documents accepted non-goals, or keeps the persistent goal open.

## Capabilities

### New Capabilities

- `rmw-mdds-full-parity-acceptance`: Defines the requirements and evidence gates for deciding whether the ROS 2 `rmw_mdds_cpp` and MDDS integration satisfies the broad "perfect/all ROS 2 middleware features" objective.

### Modified Capabilities

- None.

## Impact

- Affected code and artifacts:
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/**`
  - `ohos/test_rmw_mdds_*_contracts.sh`
  - `ohos/tools/run_*rmw_mdds*.sh`
  - `openspec/changes/complete-rmw-mdds-feature-closure/**`
  - `openspec/changes/complete-rmw-mdds-zero-copy-security/**`
- Affected systems:
  - Host `rmw_mdds_cpp` unit and conformance tests
  - RK3588/KaihongOS board runtime over HDC
  - MDDS/DSoftBus native transport
  - ROS 2 security/SROS2 surfaces
  - Upstream/local delivery workflow for the ROS 2 workspace
