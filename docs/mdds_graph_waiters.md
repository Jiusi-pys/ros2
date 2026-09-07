# Two-observer graph notification probe

Design and test program: `scripts/mdds_e2e/graph_waiters/graph_waiters.cpp`.
It creates two actual rclcpp nodes in one context and obtains separate graph
events. Before mutation, both wait futures must remain pending. Unrelated
activity can cause admission to be retried before mutation; no failed
notification after mutation is retried or ignored. Each phase requires both
events and exact graph counts/membership after the change.

The ten phases cover creation and destruction of a publisher, subscription,
service, client and node. Before them, the program calls the opposite board's
ROS service with run-specific operands. An independent server callback record
and raw log bind the exact response to that board. The host checks native
PID/start, executable, RMW/MDDS/rclcpp mappings and hashes, and absence of owned
UDP sockets. Both boards complete initial queries before A runs; A's CLI
finishes before B starts so their mutations do not overlap.

Build from Git Bash in the ROS workspace:

```bash
export PYTHONPATH="$(pwd -W)/install_ohos/Lib/site-packages"
export AMENT_PREFIX_PATH="$(pwd -W)/install_ohos"
pixi run cmake -G Ninja -S scripts/mdds_e2e/graph_waiters \
  -B build_ohos/mdds_graph_waiters \
  -DCMAKE_TOOLCHAIN_FILE="$(pwd)/cmake/ohos-aarch64.toolchain.cmake" \
  -DCMAKE_PREFIX_PATH="$(pwd)/install_ohos" \
  -DPython3_EXECUTABLE="$(pwd)/.pixi/envs/default/python.exe"
pixi run cmake --build build_ohos/mdds_graph_waiters -j 6
MDDS_RUN_ID=<fresh_id> MDDS_ROS_PROFILE_MODE=implicit \
  MDDS_ROS_CLI_BATCH=graph_waiters bash scripts/run_mdds_broker_ros.sh
```

Verified run `graph_waiters_20260907_01`: both boards returned 0 and both
observers passed all ten phases. The enclosing DSoftBus broker data, service
and graph baseline passed, including 11 baseline receipt tests. Eleven new
probe adversaries reject a missing second wake, wrong snapshot/context,
missing entity phase, bad exit, UDP, foreign executable, stale barrier,
modified source and incorrect peer callback.

- A stdout SHA-256:
  `b724ca6a921e1656ae5ad462b710dee84030f3917d93aab5a24047e125df6ceb`.
- B stdout SHA-256:
  `a773401dee21636366763506ad27e2b4aaa116895a23f161ec4c2c12fa8e9772`.
- Production RMW SHA-256:
  `5ee0184e3b734c66c6d3be7e9a01ee1a48aeff02655d4e414e033c20449c8614`.

The RMW alias defect and native RED/GREEN evidence are documented in
`src/ros2/rmw_mdds/rmw_mdds/docs/graph_guard_aliases.md`. The initial invalid
NULL-placeholder test assumption is recorded there explicitly, including the
contract-corrected RED; the original failed archive was preserved.

## Formal local guard case

Design: preserve the ten-phase/two-observer checks and record the exact native
argv and `graph:local_guard` terminal marker only after the C++ process exits.
Bind that log to the original stdout/stderr, native PID/start, private library
and source hashes, barriers, and actual peer callback. A pre-shutdown program
banner alone is insufficient.

`test_local_graph_gate.py` was written first. All four checks failed RED under
the old verifier, which ignored the local graph case. They pass after adding
the receipt gate: missing terminal, single-board-only terminal and nonzero
terminal are rejected; a complete synthetic unit fixture is accepted. Original
board evidence is never edited by these unit tests.

Fresh HDC run `graph_local_guard_20260907_01` then passed both boards, including
all ten native phases, both observers, peer calls and the enclosing DSoftBus
baseline. All 15 real-receipt adversaries and 18 generic gate tests passed.

- Final manifest SHA-256:
  `ece2f0f328528b975bacd868e787bec8597dde132a26fae3675d469f2a68acad`.
- Local guard receipt SHA-256:
  `73b3c8f94d64482fddef851c3082951e592731800bb6400d6a4241d956fde99e`.

Aggregate case coverage is 84/98. Remote guard and the remaining graph matrix,
all-CLI-on-one-version validation and unified release remain outstanding.
