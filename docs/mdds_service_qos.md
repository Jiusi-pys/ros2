# Two-board service QoS readiness regression

Design and tests precede the RMW fix. A service appearing in graph counts is
not sufficient to establish client readiness: requests and replies must both
have compatible endpoints. The fixture deliberately discovers the server in
both negative cases so a missing provider cannot masquerade as QoS rejection.

Run with a fresh ID:

```bash
MDDS_RUN_ID=service_qos_20260907_01 MDDS_ROS_PROFILE_MODE=implicit \
MDDS_ROS_CLI_BATCH=service_qos bash scripts/run_mdds_broker_ros.sh
```

Each board creates two servers and four clients, verifies remote by-node
service/client types and counts, and confirms that beta/duplicate nodes do not
inherit alpha's service ownership. Reliable-client/best-effort-server and
best-effort-client/reliable-server readiness must both be false. Matched
reliable and best-effort controls must both be true and return the exact
run-specific sums. Separate server callback files and raw logs bind the
responses to the other board.

Verified `service_qos_20260907_01` on both RK3588A boards:

- Five readiness samples per board matched the complete matrix.
- A obtained 118992232 and 118992243; B obtained 118992233 and 118992244.
- Exact server/client ownership and counts passed; non-owner scoped views
  were empty. The DSoftBus broker data/service/graph baseline also passed.
- Seven contract tests and ten real-evidence adversaries passed, including
  a falsely available incompatible path, absent provider, wrong counts,
  incorrect ownership, failed positive control and missing peer callbacks.
- A result SHA-256:
  `246db6f0b1acab1463bc333218033e7be7cb92c3050040effc6e7dcceb2e284a`.
- B result SHA-256:
  `aadebbdbc7c6a8f3a1f4e4039b006460fd2293742a239c93286c4281ff1eb710`.
- Production RMW SHA-256:
  `b1cd34b89e7bba0bd3500720587ca0d9acfac4fa9ea00f88455841a65502b214`.

The native regression program is
`src/ros2/rmw_mdds/rmw_mdds/test/test_service_availability.inc`; its observed
RED and GREEN provenance is documented in that package's
`docs/service_availability.md`. Board runners archive original failures rather
than changing their acceptance criteria.

That initial run proved the specific fix and peer controls but did not issue
a complete non-CLI graph-case receipt. The formal gate added below uses a fresh
run; the initial artifacts were preserved without relabelling them.

## Formal service ownership graph case

Design: declare `graph:service_client_ownership` in the frozen board inputs.
The ROS supervisor records actual argv and the case-specific terminal marker
only after its real ROS child exits. The host requires both board identities,
PID/start records, normal exits, raw markers, service ownership/count/readiness
results, and exact peer response callbacks before issuing the case receipt.

`test_service_graph_gate.py` was supplied first. Its four checks failed RED
because the old verifier ignored the declared graph case. They pass after the
implementation: no terminal, one board only and nonzero terminal are rejected;
the complete synthetic unit fixture is accepted. The unit fixtures are copied
test data and are not counted as board acceptance.

Actual acceptance run `graph_services_20260907_01` subsequently passed on both
RK3588A boards using DSoftBus Socket/Bytes. Its case covers `ownership_exact`,
`counts_exact` and `availability_exact`. All 18 real-receipt adversaries and
18 general CLI gate tests passed.

- Final manifest SHA-256:
  `1ec28621517f4c0902eae043d7ffd58af19a1ffcc9f723ec8cdaf1a90ff60add`.
- Graph case receipt SHA-256:
  `01fd770494a87e758e9dd4b6d45e60b61365ec9e475126073a25463c562af564`.
- RMW SHA-256, including the separately tested null-argument code fix:
  `e2c3e1d2143cdd2b50de77c9b243f6dd692a253be352fcce3b2770a3e0b0c2ba`.

Aggregate case coverage is now 83/98. Full graph failure/lifetime/isolation
coverage, advanced-functionality audit and single-release provenance remain
outstanding. Gateway is out of scope.
