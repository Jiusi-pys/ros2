# Actual component standalone CLI acceptance

```sh
MDDS_RUN_ID=cli_standalone_fresh_01 MDDS_ROS_PROFILE_MODE=implicit \
  MDDS_ROS_CLI_BATCH=standalone bash scripts/run_mdds_broker_ros.sh
```

The CLI itself starts its native component container through the installed
standalone implementation. The runner supplies explicit run-specific container,
node and topic names and the private component prefix already used by the
container-load fixture. It does not replace standalone with a direct container
launch. The native process must be the CLI's child and belong to its newly
created process group, with exact executable/arguments, PID/start, library
mappings/hashes and no UDP sockets.

Both initial node-list checks must finish before either standalone command
starts. The host records a nonce-bound ready/start barrier, preventing one
board's newly created component from racing the other's baseline graph check.

Each board observes the opposite standalone Talker. Two or more consecutive
Hello World payloads, node/container presence, endpoint ownership, nonzero GID
and generated String type hash are required. Only after both receiver records
exist does the host authorize stopping. The runner validates the nonce barrier
and process identities, then sends SIGINT to its own CLI process group.

The upstream CLI waits for its native child and returns that child's return
code. Acceptance requires the real CLI code 0, the native process gone, no
emergency kill, and complete peer graph withdrawal. The host ties every peer
payload to a matching native publisher log entry. The standard daemon and base
ROS/DSoftBus fixture still complete afterward.

Six ownership tests went from one failure to zero. Separate receipt mutation
tests use a real completed batch to reject false parentage, wrong libraries,
failed cleanup, missing data, wrong barrier/endpoint and retained graph state.

The first physical run exposed the missing start barrier: A had already created
its standalone nodes while B was still checking the original graph. B correctly
rejected the extra nodes, and the batch was excluded despite observed A-to-B
data. Its processes were cleaned up; the corrected runner repeats the full run.

`cli_standalone_20260907_02` passes on both boards: peer data and endpoint
metadata match, both actual CLI exits are 0, native children disappear without
emergency cleanup, and graph withdrawal completes. Twelve standalone receipt
tests, six ownership tests and eleven surrounding ROS receipt tests pass.
Ready/start/stop markers are retained as hashed receipt artifacts. Unique CLI
coverage is 58/98; the full graph and single-release gates remain open.
