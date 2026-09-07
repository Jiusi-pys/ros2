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

This mode proves the specific fix and its peer controls. It does not issue a
complete non-CLI graph-case receipt yet, so aggregate coverage remains 82/98.
Full graph failure/lifetime/isolation coverage, advanced-functionality audit
and single-release provenance remain outstanding. Gateway is out of scope.
