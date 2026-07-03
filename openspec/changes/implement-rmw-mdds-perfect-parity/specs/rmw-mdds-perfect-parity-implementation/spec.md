## ADDED Requirements

### Requirement: Loan-aware generated dynamic message storage
`rmw_mdds_cpp` SHALL advertise and return generalized loaned-message support only when every dynamic generated-message storage region used by the borrowed message is backed by the MDDS bridge loan lifetime.

#### Scenario: Unbounded string loan storage is bridge backed
- **WHEN** a publisher borrows an unbounded string message that `rmw_mdds_cpp` advertises as loan-supported
- **THEN** the string character storage used before publish MUST be allocated from the MDDS bridge loan lifetime
- **AND** publish MUST NOT serialize, copy, or decode default-heap string storage into a separate bridge loan buffer

#### Scenario: Sequence loan storage is bridge backed
- **WHEN** a publisher borrows a generated message containing a dynamic sequence that `rmw_mdds_cpp` advertises as loan-supported
- **THEN** the sequence element storage used before publish MUST be allocated from the MDDS bridge loan lifetime
- **AND** publish MUST NOT copy default-heap sequence storage into a separate bridge loan buffer

#### Scenario: Nested dynamic loan storage is bridge backed
- **WHEN** a publisher borrows a generated message containing nested dynamic members that `rmw_mdds_cpp` advertises as loan-supported
- **THEN** every nested dynamic member MUST be allocated from the same MDDS bridge loan lifetime or an explicitly chained MDDS-owned segment

#### Scenario: Unsupported dynamic loan shape fails explicitly
- **WHEN** the generated message/type support cannot construct all dynamic storage inside MDDS bridge loan memory
- **THEN** `rmw_mdds_cpp` MUST NOT advertise loan support for that type
- **AND** loan attempts MUST fail with an explicit diagnostic that identifies the missing loan-aware generated storage support

### Requirement: MDDS loan arena lifetime
The MDDS bridge loan model SHALL expose a loan arena or equivalent segment ownership API for generated message headers, dynamic member bytes, and nested sequence elements.

#### Scenario: Loan return releases all arena storage
- **WHEN** a publisher loaned message is returned through `rmw_return_loaned_message_from_publisher()` or consumed through `rmw_publish_loaned_message()`
- **THEN** all arena-backed dynamic storage associated with that loan MUST be returned to the MDDS bridge loan lifetime exactly once

#### Scenario: Subscription loan uses the same ownership model
- **WHEN** a subscription takes a dynamic loaned message
- **THEN** the returned message view MUST reference MDDS-owned storage for dynamic members until the subscription loan return call completes

#### Scenario: Fixed-size raw loans do not regress
- **WHEN** a fixed-size scalar message uses the existing raw bridge loan path
- **THEN** the existing no encode, no decode, no copy zero-copy contracts MUST remain green

### Requirement: Signed SROS2 artifact validation
`rmw_mdds_cpp` SHALL validate signed SROS2 governance and permissions artifacts before accepting protected governance.

#### Scenario: Unsigned protected governance is rejected
- **WHEN** protected governance requires signed security semantics but the governance or permissions artifact is unsigned
- **THEN** initialization or endpoint creation MUST fail closed with an explicit signed-artifact diagnostic

#### Scenario: Tampered signed permissions are rejected
- **WHEN** permissions XML content or its signature is modified after signing
- **THEN** `rmw_mdds_cpp` MUST reject the artifact before any publisher or subscription is created from that policy

#### Scenario: Identity mismatch is rejected
- **WHEN** the signed permissions artifact does not bind to the configured identity/certificate material
- **THEN** `rmw_mdds_cpp` MUST reject the security policy before granting topic access

#### Scenario: Valid signed protected policy is accepted
- **WHEN** governance, permissions, identity, and certificate-chain validation succeed for a protected policy
- **THEN** local publish and subscribe authorization MUST use the signed policy grants rather than unsigned fallback XML

### Requirement: Authenticated protected MDDS transport
Protected SROS2 governance SHALL require authenticated and encrypted MDDS/DSoftBus transport when the policy protection kinds request transport protection.

#### Scenario: Protected governance activates protected transport
- **WHEN** a valid signed governance policy requests authenticated or encrypted transport
- **THEN** `rmw_mdds_cpp` and the MDDS bridge MUST activate an authenticated/encrypted DSoftBus transport lane before allowing protected pub/sub traffic

#### Scenario: Protected transport setup fails closed
- **WHEN** authenticated or encrypted MDDS/DSoftBus transport cannot be established for a protected policy
- **THEN** protected publishers and subscriptions MUST fail closed instead of falling back to unprotected local XML policy behavior

#### Scenario: Board harness proves protected lane
- **WHEN** protected SROS2 board validation is run on RK3588/KaihongOS targets
- **THEN** the harness MUST emit explicit PASS markers for signed policy validation, authorized protected traffic, denied unauthorized traffic, and authenticated/encrypted transport activation

### Requirement: Final parity evidence synchronization
The implementation SHALL update the full-parity acceptance matrix and final task state only after the new dynamic loan and protected security requirements are proven or explicitly accepted out of scope.

#### Scenario: Dynamic loan completion updates the matrix
- **WHEN** dynamic generated-message loan tests and contracts pass
- **THEN** `complete-rmw-mdds-full-parity-acceptance/parity-matrix.md` MUST move the generalized loaned-message row from incomplete to proven and cite the exact command evidence

#### Scenario: Protected security completion updates the matrix
- **WHEN** signed SROS2 artifact validation and authenticated/encrypted transport evidence pass
- **THEN** `complete-rmw-mdds-full-parity-acceptance/parity-matrix.md` MUST move the protected security row from incomplete to proven and cite host and board evidence

#### Scenario: Goal remains open until delivery endpoint is resolved
- **WHEN** dynamic loans and protected security pass but the delivery endpoint remains local-only without user acceptance
- **THEN** the final full-parity task MUST remain incomplete or record the explicit accepted delivery endpoint before the persistent goal is marked complete
