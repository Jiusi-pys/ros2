## Why

The scoped `rmw_mdds_cpp` feature-closure change is complete, but it explicitly does not prove the broader "perfect/all ROS 2 features" objective. Current code and evidence still leave two completion blockers: loaned-message support is bridge-backed but still serializes/deserializes payloads, and security evidence is limited to hygiene, enclave propagation, and type identity rather than SROS2 or secure transport enforcement.

## What Changes

- Add a focused completion track for true end-to-end zero-copy semantics across ROS typed messages, `rmw_mdds_cpp`, and MDDS bridge/backend delivery.
- Add a focused completion track for ROS security behavior, including explicit support or explicit unsupported behavior for SROS2 policy inputs and secure transport expectations.
- Require TDD gates that fail on the current behavior before any implementation change claims zero-copy or security closure.
- Require board/runtime evidence, not only host unit tests, before these blockers can be marked closed.
- Keep the previously completed runtime closure evidence intact; this change only covers the remaining blockers that prevent the overall goal from being complete.

## Capabilities

### New Capabilities

- `rmw-mdds-zero-copy-security`: Defines the requirements and evidence gates for declaring `rmw_mdds_cpp` true zero-copy and ROS security support complete.

### Modified Capabilities

- None.

## Impact

- Affected code and artifacts:
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/src/rmw_unsupported.cpp`
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/src/message_adapter.cpp`
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/src/bridge_backend.*`
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/src/rmw_init.cpp`
  - `src/ros2/rmw_mdds/rmw_mdds_cpp/test/**`
  - `ohos/test_rmw_mdds_*_contracts.sh`
  - `ohos/tools/run_*rmw_mdds*.sh`
- Affected systems:
  - Host `rmw_mdds_cpp` loaned-message and security tests
  - RK3588/KaihongOS native MDDS board runtime
  - MDDS bridge/backend loaned sample APIs
  - ROS 2 security configuration surfaces and enclave/type-identity reporting
