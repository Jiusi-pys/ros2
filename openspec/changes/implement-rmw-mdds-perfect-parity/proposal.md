## Why

The final full-parity audit still blocks the broad `rmw_mdds_cpp` and MDDS "perfect/all ROS 2 middleware features" goal on two implementation classes: generalized loaned-message support for dynamic generated messages, and SROS2 security parity beyond local XML topic-policy enforcement. The user has now approved the larger scope, so the remaining work needs an implementation-grade OpenSpec change rather than another audit-only note.

## What Changes

- Add a loan-aware generator/typesupport and MDDS memory-model contract for dynamic generated messages whose strings, sequences, arrays, and nested dynamic members must be backed by bridge/MDDS loan storage before the publisher advertises loan support.
- Require RED tests for unbounded string, sequence, nested, and filtered loaned-message shapes that forbid serialize/copy/decode fallbacks and prove dynamic member storage is not default heap storage.
- Add signed SROS2 governance and permissions artifact validation, including tamper rejection and fail-closed behavior for protected governance.
- Add authenticated/encrypted MDDS/DSoftBus protected transport activation for protected security policy, with host contracts and board evidence before security parity can close.
- Keep the existing local XML SROS2 policy path as the supported unprotected mode while adding a separate protected signed/security mode.
- Feed the implementation evidence back into `complete-rmw-mdds-full-parity-acceptance` so the final persistent goal is closed only when the matrix, runtime gates, and delivery endpoint agree.

## Capabilities

### New Capabilities

- `rmw-mdds-perfect-parity-implementation`: Defines the implementation requirements for dynamic generated-message loan storage, signed SROS2 artifact validation, authenticated protected transport, and final evidence sync needed to finish the broad parity goal.

### Modified Capabilities

- None.

## Impact

- Affected code and artifacts:
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/**`
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/test/**`
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/ohos/test_rmw_mdds_*_contracts.sh`
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/ohos/tools/run_*rmw_mdds*.sh`
  - `openspec/changes/complete-rmw-mdds-full-parity-acceptance/**`
- Affected systems:
  - Generated ROS 2 C/C++ message allocation and type support boundaries
  - MDDS/DSoftBus bridge loan ownership and lifetime
  - Host `rmw_mdds_cpp` unit, conformance, and contract tests
  - RK3588/KaihongOS board protected SROS2 runtime lanes
  - Final local or upstream delivery handoff for the ROS 2 workspace
