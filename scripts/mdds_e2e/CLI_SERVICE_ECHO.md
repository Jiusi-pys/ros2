# Cross-board service introspection and controlled CLI stop

```sh
MDDS_RUN_ID=cli_service_echo_fresh_01 MDDS_ROS_PROFILE_MODE=implicit \
  MDDS_ROS_CLI_BATCH=introspection bash scripts/run_mdds_broker_ros.sh
```

Each board creates an AddTwoInts server on alpha and a client on beta for the
opposite board's server. Both enable CONTENTS introspection with reliable QoS.
The actual `ros2 service echo` subscribes to that transaction's service-event
topic. After discovery readiness and a short settling interval, the fixture
performs exactly one nonce-derived cross-board call.

All four event kinds must appear once: REQUEST_SENT, REQUEST_RECEIVED,
RESPONSE_SENT and RESPONSE_RECEIVED. Requests and responses must contain the
exact operands/sum, one identical nonzero 16-byte client GID, and the same
positive request sequence. Timestamps are validated individually; clocks on
the two boards are not assumed synchronized. The expected GID is taken from
the actual request-writer endpoint. The request sequence comes from rclpy's
pending-request mapping immediately after call_async and before another spin.
The host also binds the event stream to the client result and the opposite
server's one callback, using run/nonce/board identities and hashed raw logs.

Jazzy service echo is continuous and has no once option. After observing the
complete matching stream and completed client call, the runner records the
stdout hash and exact child PID/start, sends SIGINT to that child, and waits.
The receipt preserves the real exit code. Only this continuous case permits
code 2 (the CLI's KeyboardInterrupt result), or a clean code 0, and requires
the exact controlled-stop record even for code 0. Timeouts, SIGKILL, missing
stop markers, mismatched identity/output and borrowing this exception for
another CLI case remain failures. No return code is rewritten as zero.

Nine controlled-stop tests changed from six rejected-negative assertion
failures and one unsupported-positive error to all passes. The original 17
acceptance tests still pass. Ten event-oracle tests changed from two failures
to zero; they reject missing/duplicate events, wrong identities, wrong
content and invalid timestamps. These unit results alone are not a physical
service-echo pass or a complete graph gate.

`cli_service_echo_20260907_02` passes on both boards. Each echo receives the
four exact events, is signaled after matching the completed transaction, and
returns the actual CLI code 2. The complete daemon/ROS fixture passes, with
eleven ROS receipt tests and nine service-echo receipt mutation tests. The
10 event-oracle, 9 controlled-stop and 17 original acceptance tests pass;
these are separate test suites, not additional CLI operations. The earlier `_01` run stopped before
launching echo because the executor attempted to read a host-only nonce file;
passing the already-authorized run nonce fixes that fixture dependency.
Unique accepted CLI coverage is 42/98.
