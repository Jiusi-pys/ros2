## ADDED Requirements

### Requirement: Host conformance evidence
The workspace SHALL provide a reproducible host conformance gate for `rmw_mdds_cpp` that runs both package-level tests and upstream `test_rmw_implementation` tests with `RMW_IMPLEMENTATION=rmw_mdds_cpp`.

#### Scenario: Host conformance passes
- **WHEN** the host conformance script is executed from `/home/kaihong/ros2`
- **THEN** it MUST report package conformance PASS and upstream conformance PASS with zero failed tests

### Requirement: Board doctor evidence
The workspace SHALL provide a board-side doctor gate that proves `ros2 doctor --report` and `ros2 topic list` run on RK3588/KaihongOS with `RMW_IMPLEMENTATION=rmw_mdds_cpp`.

#### Scenario: Doctor report is complete
- **WHEN** the board doctor script runs against a selected device
- **THEN** it MUST fail on plugin load failures, NetworkReport failures, PackageReport failures, RosdistroReport failures, wrong RMW implementation selection, or missing `/parameter_events`

### Requirement: Native MDDS dual-board evidence
The workspace SHALL provide a native dual-board gate for `rmw_mdds_cpp` over MDDS/DSoftBus without relying on FastDDS gateway delivery.

#### Scenario: Native lanes pass
- **WHEN** the native MDDS dual-board harness runs against two distinct devices
- **THEN** it MUST verify pub/sub reliable delivery, service response delivery, action result delivery, and exit nonzero if any lane fails

### Requirement: Cross-RMW gateway evidence
The workspace SHALL provide cross-RMW gateway gates that prove `rmw_mdds_cpp` interoperation with `rmw_fastrtps_cpp` through `mdds_dds_gateway`.

#### Scenario: Gateway pub/sub matrix proves bridge direction
- **WHEN** the gateway matrix harness runs across MDDS and FastDDS devices
- **THEN** each lane MUST verify received payload and the expected `toDds` or `toMdds` gateway counter movement before reporting PASS

#### Scenario: Gateway service proves typed request and reply
- **WHEN** the gateway service harness calls a FastDDS AddTwoInts server from an `rmw_mdds_cpp` client
- **THEN** it MUST verify the typed response and gateway service request/reply counter movement before reporting PASS

#### Scenario: Gateway action proves full action protocol
- **WHEN** the gateway action harness drives a FastDDS Fibonacci action server from an `rmw_mdds_cpp` client
- **THEN** it MUST verify goal acceptance, feedback/status counter movement, send_goal request/reply counters, get_result request/reply counters, exact result sequence, and terminal SUCCEEDED status before reporting PASS

#### Scenario: Gateway lifecycle and parameter APIs pass
- **WHEN** lifecycle and parameter gateway harnesses run
- **THEN** they MUST verify successful lifecycle state transition and parameter set/get behavior from the `rmw_mdds_cpp` side

### Requirement: TDD enforcement for future closure work
Every future behavior change under this change SHALL start with a failing unit test, script contract, or board harness assertion before production behavior is modified.

#### Scenario: Script harness behavior changes
- **WHEN** a board harness is changed to strengthen or add a runtime behavior gate
- **THEN** a contract test MUST fail for the missing harness behavior before the harness is changed, and MUST pass after the change

#### Scenario: RMW implementation changes
- **WHEN** `src/ros2/rmw_mdds/rmw_mdds_cpp/**` behavior is changed
- **THEN** a package-level or upstream conformance test MUST fail for the missing behavior before the implementation is changed, and MUST pass after the change

### Requirement: Remaining feature gap audit
The final completion audit SHALL explicitly verify or close every remaining feature gap identified by the active objective.

#### Scenario: Zero-copy closure
- **WHEN** the final audit evaluates loaned-message support
- **THEN** it MUST include evidence for true end-to-end zero-copy behavior or an explicit non-completion finding

#### Scenario: Dynamic message take closure
- **WHEN** the final audit evaluates dynamic type support
- **THEN** it MUST include evidence that dynamic message take works through `rmw_mdds_cpp` or an explicit non-completion finding

#### Scenario: Event closure
- **WHEN** the final audit evaluates event APIs
- **THEN** it MUST include evidence for deadline, liveliness, incompatible QoS, incompatible type, and related ROS 2 event surfaces, or explicit non-completion findings for each missing event

#### Scenario: LD_PRELOAD closure
- **WHEN** the final audit evaluates runtime selection
- **THEN** it MUST prove that `rmw_mdds_cpp` works without `LD_PRELOAD` or document the remaining platform constraint as an unresolved completion blocker

#### Scenario: Security and cross-RMW identity closure
- **WHEN** the final audit evaluates security and cross-RMW identity behavior
- **THEN** it MUST include evidence for supported security/RIHS/type identity behavior or explicit non-completion findings

### Requirement: Delivery readiness evidence
The workspace SHALL not be declared complete while required source, scripts, specs, or test files remain unintentionally untracked or while generated artifacts pollute the working tree.

#### Scenario: Delivery state is clean enough to hand off
- **WHEN** the final audit checks delivery readiness
- **THEN** it MUST list all intended changed files, exclude generated cache/log artifacts, and identify whether the state is a local-only handoff, local commit, Gerrit patch, or PR
