## ADDED Requirements

### Requirement: True loaned-message zero-copy evidence
`rmw_mdds_cpp` SHALL provide a reproducible evidence gate for true loaned-message zero-copy behavior across ROS typed messages, the rmw layer, and the MDDS bridge/backend path.

#### Scenario: Loaned publish avoids serialization and payload copy fallback
- **WHEN** a supported loaned message is borrowed and published through an active MDDS bridge/backend publisher
- **THEN** the test gate MUST prove the publish path does not call the ROS-to-MDDS serialization path and does not fall back to copying the encoded payload into a second bridge loan

#### Scenario: Loaned take avoids deserialization and payload copy fallback
- **WHEN** a supported loaned message is received through an active MDDS bridge/backend subscription
- **THEN** the test gate MUST prove the take path exposes the MDDS loan/sample as the ROS loaned message without decoding payload bytes into separate ROS-owned storage

#### Scenario: Unsupported message shapes are explicit
- **WHEN** a message type cannot be represented safely as a true zero-copy MDDS loaned sample
- **THEN** `rmw_mdds_cpp` MUST report unsupported loaned-message behavior explicitly instead of silently using a copy path while claiming zero-copy support

### Requirement: Security-required mode fails closed or enforces policy
`rmw_mdds_cpp` SHALL handle ROS security-required runtime configuration with explicit enforcement or explicit fail-closed behavior.

#### Scenario: Security required without enforcement is rejected
- **WHEN** ROS security is configured as required and `rmw_mdds_cpp` cannot enforce SROS2 policy and secure MDDS/DDS transport
- **THEN** initialization or endpoint creation MUST fail with a clear error instead of allowing insecure communication

#### Scenario: Security enforcement proves positive and negative cases
- **WHEN** `rmw_mdds_cpp` claims SROS2/security support
- **THEN** host and board tests MUST prove an authorized publisher/subscriber pair can communicate and an unauthorized pair is blocked

#### Scenario: Security metadata alone is not sufficient
- **WHEN** the final audit evaluates security support
- **THEN** enclave propagation, hygiene checks, RIHS/type identity, and option copying MUST NOT be counted as full security support unless policy enforcement or fail-closed behavior is also proven

### Requirement: TDD gates precede zero-copy and security implementation
Every behavior change under this capability SHALL start with a failing test, contract, or board harness assertion that captures the missing zero-copy or security behavior.

#### Scenario: Zero-copy implementation starts red
- **WHEN** loaned-message behavior is changed to support true zero-copy
- **THEN** a host test or fake-backend contract MUST first fail against the current bounded-copy path and identify the forbidden serialize/decode/copy operation

#### Scenario: Security implementation starts red
- **WHEN** security-required behavior is changed
- **THEN** a host test or script contract MUST first fail against the current behavior and identify the missing fail-closed or enforcement behavior

### Requirement: Board runtime regression evidence
Zero-copy or security closure SHALL preserve the previously proven `rmw_mdds_cpp` runtime behavior on RK3588/KaihongOS boards.

#### Scenario: Native MDDS lanes still pass after zero-copy or security changes
- **WHEN** zero-copy or security behavior changes are made
- **THEN** the affected native MDDS board lane MUST pass with explicit pub/sub, service, or action markers as applicable

#### Scenario: Delivery contracts still pass after zero-copy or security changes
- **WHEN** zero-copy or security behavior changes are made
- **THEN** the delivery contract suite MUST pass and MUST include the new zero-copy/security gate markers

### Requirement: Final completion audit blocks overclaiming
The active "perfect/all ROS 2 features" goal SHALL remain incomplete until zero-copy and security requirements are closed with evidence or explicitly accepted as out of scope.

#### Scenario: Goal cannot be marked complete with current bounded-copy security-limited evidence
- **WHEN** the final audit finds loaned paths still use serialization/deserialization or security-required mode still lacks enforcement/fail-closed behavior
- **THEN** the audit MUST record a non-completion finding and the active goal MUST NOT be marked complete
