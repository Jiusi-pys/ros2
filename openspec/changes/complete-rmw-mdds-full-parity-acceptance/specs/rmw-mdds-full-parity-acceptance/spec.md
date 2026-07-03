## ADDED Requirements

### Requirement: Full parity acceptance matrix
The project SHALL maintain a final acceptance matrix for the broad `rmw_mdds_cpp` and MDDS "perfect/all ROS 2 middleware features" objective.

#### Scenario: Every required surface has an evidence classification
- **WHEN** the final completion audit is run
- **THEN** every ROS 2 RMW surface in scope MUST be classified as proven, accepted unsupported, or incomplete

#### Scenario: Incomplete rows block goal completion
- **WHEN** any required surface is classified as incomplete
- **THEN** the persistent goal MUST remain open and the row MUST identify the missing RED test, implementation, or runtime evidence

### Requirement: RED gates for remaining intended support
Every unproven feature that is intended to become supported SHALL start with a failing unit test, script contract, or board harness assertion before production behavior changes.

#### Scenario: Generalized zero-copy expansion starts red
- **WHEN** loaned-message support is expanded beyond the currently proven fixed-size raw shape
- **THEN** a failing test MUST first identify the unsupported shape and the forbidden copy, serialize, or decode fallback that the implementation must remove

#### Scenario: Security parity expansion starts red
- **WHEN** security behavior is expanded beyond local policy enforcement
- **THEN** a failing test or board harness MUST first identify the missing signed-artifact validation, governance semantics, or transport-protection behavior

### Requirement: Explicit unsupported boundaries
Any ROS 2 RMW surface that is not implemented for MDDS SHALL fail explicitly and reproducibly if it is accepted as out of scope.

#### Scenario: Unsupported loaned message shape is explicit
- **WHEN** an unbounded, sequence, dynamic, or otherwise unsafe message shape cannot be represented as an MDDS loaned sample
- **THEN** `rmw_mdds_cpp` MUST return explicit unsupported behavior instead of using a hidden copy path while advertising zero-copy support

#### Scenario: Unsupported security semantics are explicit
- **WHEN** signed SROS2 artifacts or transport cryptography are not implemented
- **THEN** the final audit MUST either classify that gap as incomplete or record explicit user acceptance that the behavior is out of scope

### Requirement: Board and host evidence remains required
The final parity claim SHALL require both host and board evidence for runtime-facing behavior.

#### Scenario: Host delivery contract remains green
- **WHEN** parity implementation or accepted-unsupported contracts change
- **THEN** `ohos/test_rmw_mdds_delivery_contracts.sh` MUST pass and include the relevant marker for each affected feature

#### Scenario: Affected board lanes remain green
- **WHEN** a change affects MDDS/DSoftBus transport, security, zero-copy, or cross-RMW interoperability
- **THEN** the affected RK3588/KaihongOS board harness MUST pass with explicit result markers

### Requirement: Upstream-ready delivery is defined before completion
The final completion audit SHALL define and verify the delivery endpoint before marking the persistent goal complete.

#### Scenario: Local-only delivery is not silently accepted
- **WHEN** the branch only contains local commits ahead of origin
- **THEN** the audit MUST either keep the goal open or record explicit user acceptance that local-only delivery satisfies the objective

#### Scenario: Clean handoff state is required
- **WHEN** the final completion audit is run
- **THEN** tracked status MUST be clean and generated/scratch artifacts MUST either be ignored or intentionally tracked

### Requirement: Final goal completion is evidence-gated
The persistent goal SHALL be marked complete only after the full parity matrix, scoped OpenSpec changes, runtime evidence, and delivery endpoint all agree that no required work remains.

#### Scenario: Scoped completion is not enough for universal completion
- **WHEN** only the `complete-rmw-mdds-feature-closure` and `complete-rmw-mdds-zero-copy-security` changes are complete
- **THEN** the persistent goal MUST remain open unless the full parity acceptance matrix also proves or explicitly scopes every remaining broad requirement
