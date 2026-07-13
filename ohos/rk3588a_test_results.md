# RK3588A Test Results

## Approved Final Acceptance Profile (2026-07-13 host / 2026-07-14 board time)

This section supersedes every lower current-completion statement while retaining those sections as historical
checkpoints. The user approved Plan A: the required `rmw_mdds_cpp`/MDDS production profile is **COMPLETE** and
OpenSpec task 5.5 is closed. This conclusion is bounded by the explicit dispositions below; it is not a claim of
Fast DDS performance equivalence or of target-side LeakSanitizer coverage.

Both RK3588A boards ran the same production artifacts:

```text
librmw_mdds_cpp.so=f8f74a1b61d61c73e25e6ed9a64cb1a898880a22d52f2ada3b1d2ef7da948c90
rmw_mdds_broker=4437466b602db4c2f895b75b5d6e1697fbc72e17445d3f1d6040d6525600a009
libmdds_bridge_shared.z.so=12d87b68ac5e1b46b3e2eb37cb8a4892e6a321bcfb26ded91afab6c4d017ef05
libsoftbus_client.z.so=e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d
```

- A valid-domain two-board sequential service soak completed 36,000/36,000 client/server calls in
  7349.758/7362.085 seconds with zero timeout/error and zero residual process. An earlier diagnostic run used
  domain 241, which is outside the accepted ROS range 0..232; it was stopped, marked `REJECTED`, and is not
  acceptance evidence. The runner now rejects invalid domains and same-device cross-board input before launch.
- A current-hash, four-client, exact-wire 16 MiB service soak completed 1,789/1,789 validated requests. Every
  client ran at least 7208 seconds, received its DONE acknowledgement, and exited 0. The server recorded
  `valid=1789 invalid=0 duplicates=0 send_errors=0 done_clients=4`; both boards had zero worker, broker, and
  socket residual. A short-run parser false failure was traced to `valid` matching `invalid`; an exact-key parser,
  RED contract, and 21/21 smoke rerun preceded the formal soak.
- Both boards pass 16/16 upstream programs, 129 assertions, and exactly six capability-conditional skips. The
  three broker-mode subscription-loan conditions pass 3/3 in the required direct-loan mode, while publisher and
  subscription allocation plus serialized-size behavior pass 5/5 in the AArch64 capability supplement. No
  skipped capability is counted as proven without its positive run.
- Fresh host verification passes package CTest 24/24, the rmw_mdds upstream subset 16/16, all security,
  zero-copy, artifact, and action-bag contracts, and the complete CLI delivery umbrella. The broader functional
  `test_rmw_implementation` run passes 64/64. System XML lint passes; cppcheck follows its upstream slow-version
  skip policy. Uncrustify 0.83.0_f differs only in two unmodified upstream baseline macro files, so the aggregate
  lint invocation is not reported as 70/70 and those unrelated files were not reformatted.
- The accepted performance gate is the absolute 1 KiB production threshold: p95 at most 50 ms and throughput at
  least 100 msg/s. Current rmw_mdds measures 3.089 ms and 1774.141 msg/s, so this gate passes. Fast DDS remains
  faster at 0.399 ms and 2482.394 msg/s, and the relative gap stays an optimization backlog rather than a parity
  claim.
- OHOS reports `AddressSanitizer: detect_leaks is not supported on this platform`. Board ASAN therefore proves
  invalid-access behavior only; host `detect_leaks` evidence remains separate, and this result does not claim
  target leak cleanliness. The approved security boundary is signed governance/permissions validation plus
  authenticated and encrypted protected-transport activation, authorized delivery, and unauthorized denial.

The verified tooling handoff is ROS commit `3b08223d043a74c6ed0d2606e1763556746ac78f`. Board identifiers and lab
addresses remain intentionally absent from tracked evidence; host `hdc` status 139 is non-authoritative when the
board summary, hashes, return codes, and residual checks are complete.

## RELIABLE Backpressure Immediate-HEARTBEAT Refresh (2026-07-13 host time)

This section supersedes the current-artifact wording below. Scheme A is implemented and validated, but the
persistent `all ROS 2 features / production-ready` goal remains **NOT COMPLETE** and OpenSpec task 5.5 remains
unchecked.

Production hashes on both boards:

```text
librmw_mdds_cpp.so=f8f74a1b61d61c73e25e6ed9a64cb1a898880a22d52f2ada3b1d2ef7da948c90
rmw_mdds_broker=4437466b602db4c2f895b75b5d6e1697fbc72e17445d3f1d6040d6525600a009
libmdds_bridge_shared.z.so=12d87b68ac5e1b46b3e2eb37cb8a4892e6a321bcfb26ded91afab6c4d017ef05
libsoftbus_client.z.so=e1771298eeb820dac59f762ca586a96e2c538e70350bc1d021f4b3d3b76aff9d
```

- The broker requests the new optional bridge HEARTBEAT operation once when a RELIABLE unacknowledged window is
  first full. MDDS bypasses cadence only and preserves active/cache/proxy/backend checks plus send-after-unlock.
  A legacy bridge without the symbol still loads and uses periodic recovery.
- Host verification passed `test_ipc_broker` 36/36, `test_bridge_backend` 5/5, `test_broker_mode` 17/17 plus one
  disabled case, package CTest 24/24, upstream subset 16/16, OHOS cross-build, and all affected contracts.
  Both boards passed the complete MDDS reliability suite 49/49.
- P0 smoke passed. Native cross-board topic 40/40, service `sum=42`, and Fibonacci action produced
  `M2M_SUMMARY pass=3 fail=0`. The cross-RMW String/Pose/BEST_EFFORT/TF2 matrix passed 8/8 with gateway counters.
- The three 50-way service models each passed 10/10 rounds. All 1,500 requests were sent/taken/responded;
  `CLIENT_CREATED` matched each model at 50, 50, and 1 respectively. Timeout, error, and process-timeout counters
  stayed zero. The old 30/50 request-admission symptom is not current.
- Action-bag passed with one action, send-goal 2, cancel-goal 1, and get-result 2. Signed policy and protected SROS2
  passed signed-artifact validation, authenticated/encrypted activation on both boards, authorized 60/60 delivery,
  and unauthorized denial.
- Exact-total 16 MiB topic repeat10 passed 10/10, and exact-wire 16 MiB service repeat10 passed all five 10/10
  counters. Graph churn passed 1000/1000 node/topic/service rounds and 100/100 action rounds. Both boards passed
  AArch64 `test_rmw_implementation` 16/16 programs, 129 assertions, zero failure, and six classified skips.
- Current-source full-stack ASAN and TSAN each passed 16/16 programs on both boards, with no test/broker sanitizer
  report, a live instrumented broker, exact bridge SHA, and `BOARD_RC=0`. LeakSanitizer is unavailable on OHOS.

Current five-size full-stack comparison:

| payload | rmw_mdds p95 | rmw_mdds msg/s | Fast DDS p95 | Fast DDS msg/s |
|---|---:|---:|---:|---:|
| 128 B | 2.749 ms | 1723.065 | 0.355 ms | 2587.162 |
| 1 KiB | 3.089 ms | 1774.141 | 0.399 ms | 2482.394 |
| 64 KiB | 5.800 ms | 812.746 | 0.805 ms | 1399.034 |
| 1 MiB | 14.310 ms | 62.810 | 6.717 ms | 81.052 |
| 4 MiB | 56.767 ms | 16.447 | 37.292 ms | 16.838 |

All lanes had complete delivery, zero missing ACK, and zero publish error. The 1 KiB full-matrix throughput rose
from the prior 362.415 msg/s to 1774.141 msg/s, closing the periodic-heartbeat ceiling. The result still does not
prove Fast DDS parity: current-hash two-hour service/concurrent-large soak is not repeated, six upstream conditions
remain skipped, leak cleanliness cannot be tested, and the remaining latency/throughput gap lacks product
acceptance. Host `hdc` status 139 after complete board output remains non-authoritative.

## Current-Source Gateway ABI And Broad Exact-Artifact Refresh (2026-07-13 host time)

This refresh supersedes the narrower current-artifact statement in the next section. The persistent
`all ROS 2 features / production-ready` goal remains **NOT COMPLETE**, and OpenSpec task 5.5 remains unchecked.

Both RK3588A boards read back the same active artifacts:

```text
librmw_mdds_cpp.so=4eb87ee40ff80788444606757affe65b2fe69d9c6d8f487f07598f5af76ec65b
libmdds_bridge_shared.z.so=c3b614b227521ea6f8c29fdf77b3875336facaaf4eeb1b088dbeb6c4605d8d60
mdds_dds_gateway=a033582ef631619c05d71cd74775acf78240c78d4f58a4dd98ad52018fafed70
```

- The board CLI smoke script is now POSIX `/bin/sh` and performs bounded process-tree cleanup. Direct execution
  through its shebang passed topic list, topic, AddTwoInts, and Fibonacci on both boards (domains 223 and 224),
  with the final `RESULT|rmw_mdds_cli_smoke|PASS`, `BOARD_RC=0`, and `BROKER_COUNT=0` markers. The former
  workaround `sh script` is no longer required.
- The old gateway `eb195e16...` reproducibly failed before `main()` with allocator-aware generated-type relocation
  errors. Rebuilding against the current overlay plus the ROS 2 underlay produced `a033582e...`; it loaded all four
  mappings and emitted `gateway started`. The build also completed `ros2_to_mdds_cli` and `ros2_mdds_inproc` after
  the allocator-aware String payload was copied explicitly at the CLI boundary.
- The matrix runner now fails before any lane when the gateway exits during startup. An old-ABI negative control
  returned `RESULT|gateway_start|FAIL` and nonzero status in 4.2 seconds. The current gateway then passed 8/8
  String, PoseStamped, BEST_EFFORT, and TFMessage directions on domains 217/218, and another 8/8 after swapping
  board roles on domains 219/220. Every lane advanced the expected `toDds` or `toMdds` counter.
- Current `c3b614b2...` feature evidence also includes the 9/9 message/QoS matrix; all three 50-way service models,
  including 10/10 independent-process rounds with 50/50 server-visible requests; signed protected SROS2; three
  action-bag runs; 16 MiB topic and service repeat3; 1000 node/topic/service churn rounds; 100 action churn rounds;
  and 16/16 AArch64 `test_rmw_implementation` programs on each board.
- The current full-stack performance run delivered 631/631 rmw_mdds samples with no missing acknowledgement or
  publish error. At 1 KiB, rmw_mdds measured p95 2.321 ms and 354.222 messages/s; Fast DDS measured 0.390 ms and
  2537.671 messages/s. The technical 50 ms / 100 messages/s gate passes, but the approximately 5.95x latency and
  14% throughput comparison remains a product-acceptance issue.
- Fresh host regression after the fixes passed package CTest 24/24, the rmw_mdds upstream subset 16/16, every host
  CLI lane, the script/security/zero-copy/action-bag/artifact contracts, and the seven full-stack performance tests.
  Both OpenSpec strict validation commands also passed.
- The matrix cleanup now removes stale test brokers and the default test socket. Final inspection found no related
  gateway, broker, ROS CLI, service, or action process on either board. Host `hdc` exit 139 remains non-authoritative
  when board markers and hashes are complete.

Current-hash full-stack ASAN/TSAN and the two-hour soak were not repeated during this refresh. Their historical
accepted artifacts remain documented below but are not relabeled as `c3b614b2...` evidence. Together with final
P2/P3 audit and explicit disposition of the measured small-message performance gap, these keep task 5.5 open.

## Current-Source Rapid-Restart Refresh (2026-07-12 host time)

This refresh supersedes only the exact-artifact/current wording in the next `e2c6a99c...` checkpoint. The
persistent `all ROS 2 features / production-ready` goal remains **NOT COMPLETE**, and OpenSpec task 5.5 remains
unchecked.

The current DSoftBus worktree rebuilt successfully and produced backend test sha `6030f35d...` plus production
bridge sha `c3b614b2...`. Both RK3588A boards read back the same RMW `4eb87ee4...`, broker `d754adba...`, and
bridge `c3b614b227521ea6f8c29fdf77b3875336facaaf4eeb1b088dbeb6c4605d8d60` from both bridge load paths.

- The complete current-hash DSoftBus backend suite passed 171/171 with `BOARD_RC=0` on each board, including the
  process-scoped identity and longest-valid 63-byte base-name regressions. The outgoing local buffer now follows
  DSoftBus's 256-byte session-name capacity, while listener and peer names remain unchanged.
- Each board completed 14/14 broker start/stop cycles. Every process created its IPC socket, mapped the exact
  `c3b614b2...` bridge, and exited without a SIGKILL fallback.
- Without restarting `softbus_server` or waiting for the 600-second protection interval, domain 126 passed A-to-B
  topic 40/40, AddTwoInts `sum=42`, and Fibonacci `SUCCEEDED` with four feedback messages. Domain 127 passed the
  same 3/3 matrix with board roles reversed. Both harness invocations returned 0.
- No related process or rapid-restart socket remained on either board. Host `hdc` still sometimes exited 139
  after valid output; the accepted result uses board hashes, XML/rc, `/proc/<pid>/maps`, exact counters, and final
  harness markers.
- A separate `dsoftbus_mdds` checkout later reused the same `out/arm64/targets` and briefly replaced board B's
  runtime alias with an `ac505e5b...` DDS-enabled artifact that did not contain this checkout's identity fix. It
  was retained as `libmdds_bridge_shared.z.so.external_ac505`; the active alias was restored from the verified
  colcon path, and both load paths again read back `c3b614b2...`. Future workspaces must use isolated out dirs.

The broader P0/P1, 50-way, SROS2, action-bag, 16 MiB, sanitizer, performance, graph-churn, and two-hour-soak
results below remain valid for their recorded `e2c6a99c...` artifact. They were not all rerun on `c3b614b2...`
and are not relabeled as current-hash evidence.

## Process-Scoped DSoftBus Client Identity Historical Checkpoint (2026-07-12 host time)

This historical section superseded the then-older checkpoints below. It closed the rapid broker
restart blocker, but it does not complete the broader `all ROS 2 features / production-ready` goal. OpenSpec task
progress is 33/34, and task 5.5 remains unchecked.

The old bridge reproduced the defect after 14 clean broker Init/TERM/Shutdown cycles per board: immediate
cross-board delivery was 0/5 even though both replacement brokers loaded the bridge. HILOG reported
`ClientBind open session failed, ret=-426114811`, which maps to `SOFTBUS_TRANS_BIND_REQUEST_DENIED`. DSoftBus
keys bind-request denial state by local socket name, peer socket name, and peer network id; ten failures within
60 seconds activate a 600-second protection interval. Reusing the same local identity across broker processes
therefore inherited the previous process's denial state. The state expires automatically after 600 seconds;
restarting `softbus_server` was an earlier workaround, not the only recovery path.

The fixed listener and peer socket names remain unchanged. Each connection-manager initialization now creates one
process-scoped outgoing local name, `<base>.client.<pid>.<monotonic_ns>`, and fails closed if that name cannot be
formed. A mock-based RED/GREEN test verifies that the local and peer names differ while the base prefix is
preserved. The existing `com.kaihong.mdds.*` permission rule covers the generated suffix.

Current production artifacts were read back identically from both boards:

```text
librmw_mdds_cpp.so=4eb87ee40ff80788444606757affe65b2fe69d9c6d8f487f07598f5af76ec65b
rmw_mdds_broker=d754adba1145a1f074ebd6261ae5193a44118fe3e936c499cc3a70be759c29fd
rmw_mdds_dynamic_loan_probe=58d4f75674391a12aad35de8da96e4d2c42a0c2264bd191709548eb93598c35a
libmdds_bridge_shared.z.so=e2c6a99c6e60ae111e0ccf8f2da75e0a10717349e53cd8d711d6593e11c70478
test_rmw_bundle=636559457defce16c76b1013f716a04c44c1fc3c028172ef70804e9c0abeba1d
```

Verified results on the current source/artifact line:

- `MddsDSoftBusBackendTest` passed 170/170 on RK3588A, including the process-scoped outgoing identity regression.
- Each board completed 14/14 fixed-listener broker restarts with no SIGKILL fallback. Without a SoftBus restart
  or a 600-second wait, immediate A-to-B and B-to-A delivery both passed.
- Dynamic broker loans passed seven local cases on each board and String/sequence/nested delivery in both remote
  directions; every loan-pool count returned to zero. Both boards then passed all 16 AArch64
  `test_rmw_implementation` programs, 129 assertions, six classified conditional skips, and zero failures.
- The exact production bridge passed RELIABLE topic 40/40, AddTwoInts `sum=42`, and Fibonacci action success with
  four feedback messages and result `0,1,1,2,3,5`, all through `rmw_mdds_cpp`.
- Current-source full-stack ASAN passed 16/16 programs per board with no test or broker finding. LeakSanitizer is
  unavailable on this OHOS runtime, so no leak-clean claim is made.
- Current-source full-stack TSAN used RMW `f4805f34...`, broker `8bc8d487...`, and bridge `598acea5...`. The clean
  control returned 0 and the deliberate race emitted `RMW_MDDS_TSAN_REPORT` and returned 66 on each board. Both
  boards then passed 16/16 programs with zero test/broker report, including after graceful broker shutdown. An
  initial ten-program `rc=127` run was rejected as invalid because old board interface libraries did not match the
  current test binaries; the accepted rerun used a manifest-verified, isolated 44-library current runtime closure.
- The production artifacts passed the two-board 50-process service gate for 10/10 consecutive rounds. Every round
  reported `CLIENT_CREATED=50 CLIENT_SENT=50 SERVER_REQ=50 CLIENT_OK=50`, zero timeout/error/process-timeout, and
  15.337--15.497 seconds elapsed. This is 500/500 accepted and answered requests, not client-output inference.
- The current P0 smoke passed 11/11 on each board and reported `RMW_IMPLEMENTATION=rmw_mdds_cpp`. The homogeneous
  P1 message/QoS matrix passed 9/9 for Twist, Imu, Odometry, PointCloud2, Image, 64 KiB, 256 KiB,
  transient-local, and best-effort. Strict late join returned exactly one retained sample, and liveliness delivered
  20/20.
- The current signed protected SROS2 run passed signed-policy validation, authenticated/encrypted activation on
  both boards, 60 authorized messages, and unauthorized-publisher denial. This certifies the approved signed and
  authenticated harness, not every possible SROS2 deployment combination.
- Native action record/info/play passed three consecutive runs. Each run recorded one action, two send-goal
  exchanges, one cancel-goal exchange, and two get-result exchanges; one goal succeeded and one was canceled.
  Ordinary rosbag2 record/play passed with two recorded files, 88 recorded messages, and 88 replayed messages.
- Exact-total 16 MiB topic repeat3 passed 3/3, and exact-wire 16 MiB service repeat3 passed 3/3 on request,
  server validation, response, and client validation. The current full-stack performance run completed 631/631
  rmw_mdds samples across 128 B through 4 MiB with zero missing acknowledgement or publish error. At 1 KiB,
  rmw_mdds measured p95 1.874 ms and 345.987 messages/s; Fast DDS measured 0.395 ms and 2520.369 messages/s.
- Node/topic/service graph churn passed 1000/1000 and action graph churn passed 100/100, with every create/destroy
  counter balanced and no failure. The current-artifact sequential service soak completed 36,000/36,000 calls at
  a 0.2-second pace: client sent/ok and server requests were all 36,000, timeout/error were zero, client/server
  elapsed time was 7355.501/7367.729 seconds, and the final board marker was
  `cross_board_rmw_mdds_service_soak_ok`. No trigger client/server process remained on either board.

The remaining completion boundary is explicit: the exact production hash has now repeated P0/P1, signed SROS2,
action-bag, rosbag2, 16 MiB repeat3, graph/action churn, full-stack performance, and the two-hour service soak, but
the complete P2/P3 evidence matrix still requires final audit. The measured small-message gap versus Fast DDS also
requires product acceptance. These rows keep the persistent goal **NOT COMPLETE** even though the rapid-restart,
current ASAN/TSAN, 50-way service, and newly listed feature gates are closed.

## Allocator-Aware Acceptance Checkpoint (2026-07-12)

This section supersedes older completion/blocker summaries below. The approved generator/typesupport, MDDS loan
memory-model, signed SROS2, and ROS 2 feature scope is implemented and verified. The broader persistent
"all ROS 2 features / production-ready" goal remains **NOT COMPLETE**: OpenSpec is `27/28`, and Task 5.5 stays
unchecked until the remaining limitations are either proven or explicitly accepted out of scope.

Current release artifacts were read back identically from both RK3588A boards:

```text
librmw_mdds_cpp.so=b1fe7838191cfd2486e450c8d719c1856fb21e6c8b5a467cbbbdd360ef4207aa
rmw_mdds_broker=b0054627cecf38637f63181c91fe8b828694de76338923b93baa527450ea4499
rmw_mdds_dynamic_loan_probe=63117f049bd4899b78858c121e635df091db59a78856dd3334ad0facf7793b43
libmdds_bridge_shared.z.so=58169fc319fdecec91d991f1a8a3da10eeeda33023406a41f1d9a59958b68405
test_rmw_bundle=c1422c3a79b5c7399e1fc577122280d058d463bf5f1bbb1367a4a096097b17fa
```

Implemented scope and current evidence:

- `rosidl_runtime_c` exposes a scoped message-memory-resource contract; `rosidl_runtime_cpp` provides allocator
  binding; generated C++ String, sequence, and nested dynamic members propagate the selected resource. Introspection
  and Fast RTPS typesupport preserve that allocator-aware representation instead of falling back to the process heap.
- Direct production-bridge loaning uses bridge-owned typed storage for unbounded String, sequence, and nested dynamic
  messages. Both boards passed all three shapes and verified that dynamic storage remained inside the same 64 KiB
  loan arena: `RESULT|rmw_mdds_dynamic_loan|PASS|rmw=rmw_mdds_cpp|broker=0|bridge=1|shapes=3`.
- Signed SROS2 policy validation rejects unsigned/tampered/mismatched artifacts and rejects protected policy without
  authenticated transport. Both boards passed signed policy validation, authenticated/encrypted activation,
  authorized 60-message delivery, and unauthorized-publisher denial.
- Host current-worktree gates pass: package CTest `24/24`; upstream `rmw_mdds_cpp` `16/16`; ASAN package `24/24`
  with `detect_leaks=1`; TSAN package `24/24` with `halt_on_error=1`; dynamic publisher-loan ASAN and TSAN sets each
  `13/13`; all security, zero-copy, action-bag, artifact, performance, full-parity, and delivery contracts PASS.
- Generator/runtime functional checks pass after rebuilding current sources: `rosidl_runtime_c` `9/9`,
  `rosidl_runtime_cpp` `2/2`, and Fast RTPS typesupport `3/3` (two gtests plus CLI pytest with third-party plugin
  autoload disabled). The clean AArch64 overlay rebuilt current `test_msgs`, all RMW vendors, rosbag2, and the probe.
- On each board, the current AArch64 upstream suite reports
  `PASS=16 FAIL=0 TOTAL=16 TEST_ASSERTIONS_PASSED=129 SKIP_TESTS=6 RMW=rmw_mdds_cpp`. The same manifest-verified
  bundle was used on both boards, with fresh domain and broker state for every program.
- Current two-board functional gates include P0 CLI `11/11`; exact topic `40/40`; service `sum=42`; Fibonacci action
  success/feedback; lifecycle, parameters, components, TF2, standard loaned-message example, rosbag2 record/play,
  action record/play, complex messages, QoS, transient-local, large messages, and protected SROS2.
- Stress/stability evidence includes 50-process service `50/50` for 10/10 rounds, one-process/50-clients `50/50`,
  one-client/50-requests `50/50`, 10,000 sequential cross-board calls, node/topic/service graph churn `1000/1000`,
  and action graph churn `100/100`.
- The current full-stack performance run completed every 128 B through 4 MiB case with zero missing ack or publish
  error. The 1 KiB rmw_mdds result was p95 `1.857 ms` and `339.927 msg/s`, inside the 50 ms / 100 msg/s gate.
  Small-message rmw_mdds latency was still about 4.8--7.7 times Fast DDS and throughput about 12--25% of Fast DDS;
  this is a measured limitation, not a performance-lead claim.

Disposition of the six upstream board skips:

- Publisher-allocation and subscription-allocation each skip because the upstream test only verifies the
  `RMW_RET_UNSUPPORTED` branch; rmw_mdds returns a supported result. Positive allocator/resource tests cover the
  implemented branch.
- Serialized-size skips once because the upstream test similarly has no positive supported-API assertion; current
  valid-input size and generated-typesupport tests cover that branch.
- Three broker-mode subscription-loan tests skip because that queue advertises `can_loan_messages=false` for those
  shapes. Direct production-bridge dynamic loans pass, and fixed-scalar broker shared loans have separate proof, but
  String/sequence/nested/content-filtered broker subscription true-loan remains outside the proven slice.

Remaining production blockers are therefore explicit: complex broker-subscription true-loan breadth; current
allocator-aware exact-artifact board ASAN/TSAN and two-hour full-stack soak repetition; and acceptance of the measured
small-message performance gap. Host lint aggregation is also not fully runnable on this machine because `cppcheck`,
`uncrustify`, `flake8`, and `pydocstyle` are absent; this did not affect the functional, cross-build, board, ASAN, or
TSAN results above.

Reproduction from the ROS 2 workspace root:

```bash
ROS2_OHOS_BUILD_TESTING=ON \
ROS2_OHOS_COLCON_BUILD_BASE="$PWD/build/ohos-colcon-rk3588a-clean" \
ROS2_OHOS_COLCON_INSTALL_BASE="$PWD/install/ohos-colcon-rk3588a-clean" \
  ./ohos/colcon_rk3588a.sh test_rmw_implementation
./ohos/tools/run_rmw_mdds_test_rmw_board.sh <BOARD_A> <BOARD_B>
./ohos/tools/run_rmw_mdds_host_conformance.sh
bash ohos/test_rmw_mdds_delivery_contracts.sh
```

## Broker Subscription Shared-Loan Supplement (2026-07-12 board time)

The approved fixed-size scalar broker-subscription loan scope is implemented and proven on the current checkout.
This closes that specific zero-copy gap; it does **not** make the broader ROS 2 full-feature/production-ready goal
complete. CDR, dynamic, string, sequence, and content-filtered broker subscription shapes remain explicitly outside
this true-zero-copy slice.

- A broker-owned, versioned, file-backed pool now carries fixed-size raw subscription samples. The broker is the
  only writer, the client maps the pool read-only, IPC delivery contains a descriptor rather than payload bytes,
  and the exact loan id is returned over its owning Unix connection.
- Duplicate/foreign returns, stale generations, bad bounds, and wrong ownership are rejected. Disconnect removes
  the owner-only (`0600`) backing file. Ordinary typed, serialized, and sequence takes copy/decode into caller
  storage and immediately return the slot.
- The RELIABLE capacity RED gate reproduced a dropped 33rd sample while 32 loans were pinned. A bounded pending
  delivery queue now defers that sample and sends it when a slot is returned; KEEP_LAST replacement is constrained
  by the reader history depth.
- Host evidence: package CTest `24/24`, upstream `test_rmw_implementation` for `rmw_mdds_cpp` `16/16`, affected
  ASAN programs `4/4`, and the zero-copy contract PASS. Focused TSAN passed both broker-mode loan tests and the IPC
  ownership/reclaim test. The complete legacy `test_ipc_broker` TSAN binary still reports a race in the fake bridge
  test counter at `test/fake_mdds_bridge.cpp`; no production loan-path race was reported, but full historical test
  stub TSAN certification remains open.
- Both RK3588A boards were deployed with RMW
  `7f4184b08a8d89fad00787d693d341d5fd590cb22b01a1a6ef568682c1616823`, broker
  `e9e1f2f092fd943d1364e41dea25f308a0382c532b8daebb64e234a751582f4a`, and bridge
  `58169fc319fdecec91d991f1a8a3da10eeeda33023406a41f1d9a59958b68405`.
- Cross-board `std_msgs/msg/Int32` passed A-to-B `40/40` and B-to-A `40/40` with
  `RMW_IMPLEMENTATION=rmw_mdds_cpp` and broker mode enabled. Each board, while acting as subscriber, showed a
  `0600` pool and an `r--s` mapping in `/proc/<pid>/maps`; pool count returned to zero after subscriber exit.
  Board-side evidence is retained at `/data/local/tmp/rmw_mdds_loan_test_197/result.txt` with
  `RESULT|board_broker_subscription_loan|PASS` and exact artifact hashes.

Host reproduction from the repository root:

```bash
bash ohos/test_rmw_mdds_zero_copy_contracts.sh
ctest --test-dir build/rmw_mdds_cpp --output-on-failure
RMW_IMPLEMENTATION=rmw_mdds_cpp \
  ctest --test-dir build/test_rmw_implementation --output-on-failure -R 'rmw_mdds_cpp$'
./ohos/colcon_rk3588a.sh rmw_mdds_cpp
./ohos/tools/deploy_rmw_mdds_delta.sh <BOARD_A> <BOARD_B>
```

The OpenSpec change `complete-rmw-mdds-full-parity-acceptance` is valid and is now `27/28`; task 5.5 remains open
because the universal goal may be completed only after every remaining required row is proven or explicitly
accepted out of scope.

## Host Test-Stub And Loan-Allocator Supplement (2026-07-11)

This follow-up closes the fake-bridge race called out above and repairs two sanitizer-visible test/allocator
defects. It does not close the complex broker-subscription memory-model row or make the dynamic publisher arena
compatible with sanitizer allocator interposition.

- The fake MDDS bridge now serializes its endpoint registry, QoS, counters, loan queues, and payload state. Data
  and matched callbacks are snapshotted while locked and invoked after unlock. Payload accessors return
  thread-local snapshots so a broker thread can destroy its publisher without invalidating bytes being compared
  by the test thread.
- The original TSAN RED stopped at `FakeMddsBridgePublisherPublishCount`; the next RED found a publisher payload
  read racing destruction. After both fixes, the complete TSAN `test_ipc_broker` passed `34/34`. The focused TSAN
  set `test_pubsub_inproc|test_ipc_broker|test_loan_arena` passed `3/3` with `halt_on_error=1`.
- `MddsLoanArena` replaced throwing global `new/delete` but omitted the matching nothrow overloads. ASAN therefore
  reported `operator new vs free` before the arena tests ran. Matching nothrow scalar/array new/delete overloads
  and a dedicated RED/GREEN test now pass `test_loan_arena` `3/3` under ASAN.
- Five security-negative tests retained `rmw_get_error_string().str` from a temporary return object. They now copy
  the text immediately into `std::string`; the focused ASAN stack-use-after-scope RED is GREEN.
- `fastrtps__dynamic_data_deserialize()` allocated an owned `SerializedPayload_t` buffer and then overwrote its
  pointer with the caller's buffer. It now uses a non-allocating payload view with explicit length/max-size.
  `test_pubsub_inproc` passes `37/37` and `test_bridge_loaned_take_rmw` passes `10/10` under ASAN with
  `detect_leaks=1`; the previous 29/30-byte LeakSanitizer findings are gone.
- Current host gates pass: package CTest `24/24`, upstream `rmw_mdds_cpp` `16/16`, zero-copy/full-parity contracts,
  and the full delivery umbrella including all CLI markers. Full ASAN package CTest is `23/24` with leak detection
  enabled and no leak report; full TSAN package CTest is also `23/24`. Both fail only the dynamic publisher-loan
  address-range assertion because sanitizer allocators bypass the DSO-global allocation hook, not because of an
  ASAN invalid access, leak, or TSAN race report.
- Both boards now carry RMW
  `e2b9cbe14950e6bc04a6df025fe8bc918ab174dee51d892daa65a9d77d3f5c25`; broker
  `e9e1f2f092fd943d1364e41dea25f308a0382c532b8daebb64e234a751582f4a` and bridge
  `58169fc319fdecec91d991f1a8a3da10eeeda33023406a41f1d9a59958b68405` are unchanged. With
  `RMW_MDDS_BROKER=0`, the production bridge passed all three upstream subscription-loan cases on each board.
  Broker-mode cross-board String pub/sub then passed once in each direction on independent domains. Board-side
  result files report `RC=0 PASS=3 FAIL=0`; related processes and the default broker socket were removed.
- Both boards also carry dynamic typesupport
  `0a2f76d43bbe237747e87401c42b83357ba14d22f416eeda2a335f9e825a34cc`. The board package's five `Dynamic*`
  tests pass `5/5` on each board, including String payload dynamic take; result files retain the exact hash.

The earlier fixed-scalar broker-pool `40/40`, `0600`, and `r--s` evidence remains tied to RMW `7f4184b0...` and is
not relabeled as a rerun on `e2b9cbe1...`. Complex broker String/sequence/dynamic/filtered subscription loans,
sanitizer-compatible dynamic publisher-loan allocation, broader security combinations, and remaining stability
gates keep task 5.5 open.

## rmw_mdds Full-Stack RMW Performance Supplement (2026-07-11)

The previously open full-stack RMW performance gate is closed for the current checkout and the exact artifacts
below. This does not close the broader production/full-feature goal.

- A real two-board `rmw_mdds_cpp` baseline reproduced silent RELIABLE topic loss: latency samples completed, but
  the 128B, 1KiB, and 64KiB throughput cases stopped near 60 deliveries. The server request count stopped at the
  same value, while rclpy publish returned no error. The identical probe passed all 631 requests with
  `rmw_fastrtps_cpp`, isolating the defect to the rmw_mdds broker-to-MDDS admission path.
- The broker previously applied MDDS unacknowledged-sample backpressure only to service/client endpoints. Generic
  RELIABLE topic publishers could overrun MDDS writer history before transport acknowledgements reclaimed it.
  Backpressure now applies to generic RELIABLE topic bridge publishers with an independent default maximum of 32
  unacknowledged samples. Service behavior remains at its existing default of 1, and BEST_EFFORT bypasses the wait.
- The new RELIABLE topic regressions were RED before their fixes and GREEN afterward. The service, configured
  RELIABLE topic, KEEP_LAST-depth, and BEST_EFFORT focused tests pass 4/4; the complete broker suite passes 33/33;
  package CTest passes 23/23. The
  full-stack performance contract and its seven pure-Python correlation/statistics/workload tests also pass.
- Both RK3588A boards were hash-verified with RMW
  `6bba62f796b636ae71fb826747784ef64f6fdb13423cfe769a4133581b8e10c3`, broker
  `77a037c6565fc87e46ce50202761fac5a58b83583a870db72b774fbbdc810ad8`, and unchanged production bridge
  `58169fc319fdecec91d991f1a8a3da10eeeda33023406a41f1d9a59958b68405`.
- Three independent valid-domain runs covered 128B, 1KiB, 64KiB, 1MiB, and 4MiB RELIABLE payloads. Every
  rmw_mdds and Fast DDS server observed 631/631 requests with zero invalid request, missing acknowledgement, or
  publish error.

| Domain pair (MDDS/Fast DDS) | rmw_mdds 1KiB p95 | rmw_mdds 1KiB throughput | Result |
| --- | ---: | ---: | --- |
| 223 / 224 | 2.391 ms | 342.936 msg/s | PASS |
| 225 / 226 | 1.921 ms | 357.201 msg/s | PASS |
| 227 / 228 | 1.881 ms | 352.198 msg/s | PASS |

The design gate is p95 no greater than 50 ms and throughput at least 100 msg/s for the 1KiB case. Two later
attempts with Fast DDS domains 234 and 236 are excluded because those domain IDs exceed Fast DDS's valid port
range; their rmw_mdds phases still completed 631/631. The runner now rejects non-numeric or out-of-range domains
before any HDC operation.

Manual reproduction from the repository root:

```bash
./ohos/colcon_rk3588a.sh rmw_mdds_cpp
./ohos/tools/deploy_rmw_mdds_delta.sh <CLIENT_DEVICE_ID> <SERVER_DEVICE_ID>
./ohos/tools/run_cross_board_rmw_mdds_fullstack_perf.sh \
  <CLIENT_DEVICE_ID> <SERVER_DEVICE_ID> 219 220
```

This closes the valid full-stack RMW performance evidence blocker. Task 5.5 remains open for broader security
combinations, dynamic/broker subscription zero-copy breadth, explicit remaining skip disposition, and remaining
long-stability/P2/P3 coverage; this state is not production-ready or full-feature complete.

## rmw_mdds Native Action Bag and Graph Guard Supplement (2026-07-11)

The dedicated native action-bag gap is closed for the current checkout and exact RK3588A artifacts. This result
does not close the broader production/full-feature goal.

- The initial cross-board recorder reproduced a real graph-notification defect: its broker reader refreshed the
  remote endpoint cache, but no node graph guard condition was triggered, so rosbag2 remained asleep in
  `wait_for_graph_change()` and never created the dynamic action-event subscriptions.
- Graph cache updates now compare endpoint content, excluding heartbeat-epoch-only changes, and trigger every live
  node graph guard only when content changes. Guard state is atomic and `rmw_wait()` consumes it with an atomic
  exchange. `GraphUpdateTriggersNodeGraphGuardCondition` was RED before the fix and GREEN afterwards.
- Package CTest passed 23/23. The host native action-bag gate passed 10/10 consecutive runs through
  `rmw_mdds_cpp`, including successful and canceled goals, feedback, exact action-service event counts, `ros2 bag
  info`, and `ros2 bag play --send-actions-as-client`.
- Both RK3588A boards carried production RMW `3927ec62...`, broker `5dbdd9d2...`, bridge `58169fc3...`, and action
  probe `cbdd236e...`. Three consecutive cross-board record/play runs passed on domain pairs 209/210, 211/212,
  and 213/214. Every run reported one action, send-goal request/response 2/2, cancel-goal 1/1, get-result 2/2,
  replay server goals=2, cancel=1, and `BOARD_RC=0`.
- The script contract then caught an obsolete explicit `librmw_implementation.so` preload in the new runner. After
  removing it, a runtime-selectable-only rerun on domains 215/216 produced the same exact counts and
  `RESULT|rmw_mdds_cross_board_action_bag|PASS|rmw=rmw_mdds_cpp`; both board phases again ended with `BOARD_RC=0`.
- A post-run audit found stale broker socket files despite zero live process. Cleanup now removes runtime sockets and
  PID files on both boards. The cleanup-fixed domains 217/218 rerun passed with the same action counts; afterwards
  both boards reported process=0, socket=0, PID file=0, and `BOARD_RC=0`.
- The changed graph/lifecycle RMW was rebuilt with the existing fail-closed OHOS TSAN model as `e7656568...`.
  With broker `55f94657...` and instrumented bridge `546c7aba...`, each board passed all 16 arm64 OHOS programs
  with zero functional failure, zero current test/broker TSAN report, a live broker, and `BOARD_RC=0`.
- Clean temporary worktrees accepted and applied the complete `rcl`, `rclcpp`, and `rosbag2` patch artifacts; the
  action-bag contract also verifies that `apply_workspace_patches.sh` includes those patches.
- The final host delivery umbrella passed after the no-preload correction: security, SROS2 policy, zero-copy,
  artifact, and native action-bag contracts; package CTest 23/23; the accepted upstream-derived RMW suite 16/16;
  type-description; and all pub/sub, service, action, parameters, lifecycle, graph, QoS, transient-local, and
  message-info CLI lanes.

Manual reproduction from the repository root:

```bash
./ohos/tools/deploy_rmw_mdds_delta.sh <CLIENT_DEVICE_ID> <SERVER_DEVICE_ID>
./ohos/tools/run_cross_board_rmw_mdds_action_bag.sh \
  <CLIENT_DEVICE_ID> <SERVER_DEVICE_ID> 219 220
```

The runner forces `RMW_IMPLEMENTATION=rmw_mdds_cpp`, uses SQLite explicitly, preserves board-side artifacts, and
checks exact record/play counts. In this lab `hdc` may return host rc=139 after valid output; the authoritative
result is the board-side summary and `BOARD_RC` marker.

This closes dedicated native action record/info/play and the affected graph wake-up/TSAN regression. The broad
goal remains **NOT COMPLETE** because valid full-stack RMW performance, broader security combinations,
dynamic/broker subscription zero-copy breadth, explicit remaining skip disposition, and remaining long-stability
evidence are still open.

## rmw_mdds Lifecycle Shutdown Supplement (2026-07-11)

An intermittent host `ros2 lifecycle get` abort was not a transient CLI failure. The old acceptance runner retried
the command and could later print PASS after `double free or corruption` or `corrupted size vs. prev_size`.

- A post-start GDB capture showed the main Python thread already in `_dl_fini` while two broker reader threads from
  separate rmw contexts were still processing graph updates. Both belonged to the CLI node's
  `get_type_description` services. Heap corruption was detected while one reader replaced the process graph cache;
  that was the detection site, while the lifecycle defect was that `rmw_shutdown()` had not quiesced live
  per-entity broker readers.
- `IpcClient` instances are now registered against their `rmw_context_t`. `rmw_shutdown()` stops only that
  context's clients using the established `shutdown()`, reader join, then descriptor-close sequence. Later entity
  destruction remains idempotent, and another live context is not stopped.
- The package regression was RED before the implementation because `rmw_send_response()` still succeeded after
  context shutdown. It is now GREEN and also proves two-context isolation. Package CTest passed 23/23 and the
  accepted upstream-derived suite passed 16/16.
- The lifecycle runner now treats any CLI exit at or above 128 as an immediate failure and dumps the attempt logs;
  a later retry can no longer hide `SIGABRT` or `SIGSEGV`.
- The original host lifecycle reproducer passed 100/100 Release iterations with glibc tcache disabled and
  `MALLOC_PERTURB_` enabled. The full host delivery umbrella then passed pub/sub, service, action, parameters,
  lifecycle, graph, QoS, transient-local, message-info, package CTest, and upstream conformance.
- Both RK3588A boards were deployed with production RMW `da23c6d7...`, broker `5dbdd9d2...`, and bridge
  `58169fc3...`. A real cross-board lifecycle client completed 20/20 state requests through `rmw_mdds_cpp` with
  `BOARD_RC=0`.
- The changed RMW was rebuilt under the fail-closed OHOS TSAN model as `6b04c69a...`; broker `55f94657...` and
  instrumented bridge `546c7aba...` were unchanged. Each board again passed all 16 arm64 OHOS programs with zero
  functional failure, zero test/broker TSAN report, a live broker, and `BOARD_RC=0`.

This closes the lifecycle CLI heap-corruption blocker and its false-positive runner behavior. It does not certify
the remaining full-stack performance, zero-copy breadth, security/action-bag breadth, or other open production
gates, so the broad goal remains **NOT COMPLETE**.

## rmw_mdds Full-Stack TSAN Supplement (2026-07-11)

Both RK3588A boards were hash-verified with the same instrumented artifacts:

```text
librmw_mdds_cpp.so=241d11b3621c5bc2591756bafc8bd6793ceb8f453b5c4cd65b8e4b34d19248b0
rmw_mdds_broker=55f94657d5a5e4e9a1c976242e1f87a47f84c75296d134062818506455e57f99
libmdds_bridge_shared.z.so=546c7abab6d660b74295cab5dbe9983b27a54b02671e008f9fa9530df8004665
test_client=e5c8f68e1d73830f12234d0e9eadef18cf832f6e7bf1ca2acdcbcf10e8d59b19
test_service=c01a86b777e1b15f9c008585d664be283afbff2ce3bfd955f1d25b245aba6fc3
```

- The OHOS clang 15 TSan runtime detects races but faults after both clean and reporting finalization. The
  test-only compatibility object exits after TSan's report decision: a clean minimal program returns 0, while a
  deliberate-race negative control emits `RMW_MDDS_TSAN_REPORT` and returns 66. This makes a silent detector bypass
  fail the gate instead of looking clean.
- The first focused matrix produced only 7/16 clean programs and nine race reports. An unhooked diagnostic build
  identified a real `IpcClient::Stop()` race: one thread reset/closed the IPC descriptor while the reader thread was
  in `recv()`. Stop now calls `shutdown()`, joins the reader, and only then resets the descriptor.
- The remaining two rc=139 results came from the upstream-derived `check_qos` fixtures destroying their node while
  a client or service child was still alive, contrary to the RMW lifecycle precondition. Patch
  `ohos/patches/0008-rmw-implementation-destroy-qos-test-entities.patch` adds explicit scoped destruction. The accepted
  test binaries include this disclosed fixture patch and are not described as an unmodified upstream suite.
- The repository runner `ohos/tools/run_rmw_mdds_fullstack_tsan_board.sh` then passed all 16 arm64 OHOS programs on
  each board with the instrumented MDDS/DSoftBus bridge loaded. Both summaries reported
  `total=16 functional_pass=16 functional_fail=0 test_tsan_fail=0 broker_tsan_files=0 broker_tsan_text=0
  broker_alive=1`, the expected bridge hash, and `BOARD_RC=0`.
- A complete default rebuild restored `out/arm64/targets` to production bridge
  `58169fc319fdecec91d991f1a8a3da10eeeda33023406a41f1d9a59958b68405`. It has no ASAN/TSAN dependency or
  dynamic symbol and retains `__cfi_check`; this does not relabel the accepted TSAN artifact.

This closes the exact-artifact full-stack TSAN conformance/bridge race gate. It does not certify every ROS 2 path or
make the implementation production-ready. Valid full-stack RMW performance, broader security/action-bag coverage,
dynamic/broker subscription zero-copy breadth, explicit remaining skip disposition, and other P2/P3 stability gates
remain open. Lower statements that call the TSAN gate blocked are historical checkpoints superseded by this section.

## rmw_mdds Full-Stack ASAN Supplement (2026-07-11)

Both RK3588A boards were verified with the same instrumented artifacts:

```text
librmw_mdds_cpp.so=78068556b97b169364ada37f55d35569051b70f536173eef812a1c10aa0944ea
rmw_mdds_broker=aa73a715a68d075390133316e456983e01bef44a63f114204d817d138b1c2278
libmdds_bridge_shared.z.so=2982e8c8ffdc36168304438dd615eaf0fd520d307563b154568fb651dc785826
test_rmw_bundle=f42676f918e7651d7da4bbfc88995bc3fec40663689c0a0a8f3fc83104da4eb3
```

- Dynamic-runtime diagnostics either conflicted with the static-runtime broker's ASAN shadow map or triggered an
  allocator-interposition bad-free in the board `libhisysevent` static-construction path. The accepted model uses
  one static ASAN runtime in the host executable and a fully instrumented bridge whose ASAN hooks resolve from that
  process runtime.
- The default-off GN argument is `mdds_bridge_asan_use_process_runtime=true` and requires `is_asan=true`. The
  resulting bridge has 48 unresolved ASAN hooks, no dynamic ASAN `DT_NEEDED`, and no hook missing from the broker's
  exported symbols.
- On each board, with `bridge_enabled=1` and cross-board graph sync active, all 16 arm64 OHOS
  `test_rmw_implementation` programs passed. Both accepted summaries reported
  `total=16 functional_pass=16 functional_fail=0 test_asan_fail=0 broker_asan_files=0 broker_asan_text=0
  broker_alive=1` and `BOARD_RC=0`.
- The accepted launcher includes the test bundle, release ROS 2, and colcon runtime library directories. An earlier
  attempt missing `test_msgs` and `memory_tools` produced 13 rc=127 results and is launcher-invalid.
- A complete default production rebuild passed. Bridge
  `1ab5cb31383c2195ad41441d7418c658c862e039cf9bef224aab86fa03411791` has no ASAN dependency or symbol and
  retains CFI cross-DSO, `__cfi_check`, and `--no-undefined`.

This closes the exact-artifact full-stack ASAN functional/invalid-access gate. OHOS LeakSanitizer is unavailable,
so leak cleanliness is not certified. The broad goal remains **NOT COMPLETE** because valid full-stack RMW
performance, broader security/action-bag, dynamic/broker subscription zero-copy breadth, explicit upstream skips,
and remaining P2/P3 stability gates are still open. Lower statements that call full-stack ASAN incomplete are
historical checkpoints superseded by this section.

## MDDS Independent Reliability Control-Channel Supplement (2026-07-11)

The approved DSoftBus OpenSpec `separate-reliability-control-channel` is implemented at 21/21 tasks. It decodes
ACKNACK, HEARTBEAT, and GAP into a dedicated `DATA_TYPE_MESSAGE`/`SendMessage` path while DATA remains on
`DATA_TYPE_BYTES`/`SendBytes`.

- Retained bridge `d8386ad3861c992ffa93f10ac6fa99ce13621beb3f0a94f9e4ccdef24e2e23f0` passed the
  approved 900-second four-client near-cap gate at 265/265 requests, with zero functional errors and zero
  unmatched/duplicate ACK. ACK wait p95/max was 9.380/15.844 seconds and no wait reached 30 seconds, satisfying
  the p95 threshold of 11.771 seconds.
- Both load paths on both RK3588A boards currently read back bridge
  `9b6a5f2de6981f9df331ef7592dc189c72a119060f9859a1e4c9c57c0bba0597`.
- Current-worktree test hashes were
  `MddsDSoftBusBackendTest=70bd5d7f4486af736c226941c409119acd9317e969a20e2d8ee76143656bf7c6` and
  `MddsLaneManagerTest=ad573e4670170a825b60db0bf7b1be354b6d8b33be06ce44a5681591ed63cbc4`.
  On each board, focused control passed 7/7, pinned-slot lifecycle passed 4/4, full backend passed 168/168, and
  lane-manager passed 36/36; every accepted run ended with `BOARD_RC=0`.
- An initial focused invocation from read-only `/` ran all seven assertions successfully but aborted while writing
  GoogleTest XML and returned `BOARD_RC=134`. It is launcher-invalid. The accepted rerun used `/data/local/tmp`.
- The 900-second latency values remain exact-artifact evidence for `d8386ad3...`; they are not claimed as a fresh
  `9b6a5f2d...` latency run. The current tests prove the implementation remains present and regression-green.

This closes the earlier shared-TCP-stream ACK-tail design blocker. The broad goal remains **NOT COMPLETE** because
valid full-stack RMW performance, broader security/action-bag, dynamic/broker subscription zero-copy breadth,
explicit upstream skips, and remaining P2/P3 stability gates are still open.

## rmw_mdds Loan/Conformance/Matched-Event Supplement (2026-07-11)

Both boards were read back with
`librmw_mdds_cpp.so=e5e6459eaeed076506d86e394415fee0aad0def36b25d72e38fdfa66df41d903`,
`rmw_mdds_broker=5dbdd9d2d8408bb405af94fb8ae77e0940c49721cc2cccc76c2f13740fc1494d`, and
`libmdds_bridge_shared.z.so=9b6a5f2de6981f9df331ef7592dc189c72a119060f9859a1e4c9c57c0bba0597`.
No board identifier or lab address is retained here. Host HDC continued to exit 139 after valid output; every result
below is based on the board-side test rc and summary marker.

- A foreign subscription-loan return previously reached `free()` and aborted on the host; the pre-fix board run
  terminated with signal 11. `rmw_return_loaned_message_from_subscription()` now rejects a pointer absent from the
  subscription's active bridge-loan registry. Host package CTest passed 23/23, the focused ASAN test passed, and
  `ohos/test_rmw_mdds_zero_copy_contracts.sh` remained PASS.
- A full current-artifact `test_rmw_implementation` run initially exposed stale broker matched-event totals on one
  board: publisher/subscription callbacks reached 2 while `rmw_take_event()` returned 1. The deterministic host RED
  reproduced both directions. Endpoint-registration ACK now invalidates graph-cache freshness, causing the next
  status query to obtain the completed graph without changing the 100 ms broadcast coalescer.
- After the fix, both boards passed upstream `test_event` 4/4 and all 16 upstream test programs with fresh broker
  sockets (`TEST_RMW_GREEN_SUMMARY PASS=16 FAIL=0 TOTAL=16`, `BOARD_RC=0`). Broker-mode
  `test_subscription` was 28 PASS / 3 SKIP / 0 FAIL; the three skipped cases are conditional on
  `can_loan_messages=false` in the non-zero-copy broker queue.
- Clean direct mode with the production DSoftBus bridge passed those three subscription-loan cases 3/3 on both
  boards; the cross-built fake bridge independently passed 3/3. A preliminary non-isolated run with two external
  test brokers still active produced SIGBUS/SIGSEGV. After broker cleanup the same binary passed, so the failed run
  is retained as test-environment/resource-conflict evidence and does not certify multi-broker coexistence.
- Valid-input capability tests passed 4/4 on each board: publisher/subscription allocation init/fini, fixed scalar
  serialized size, bounded-sequence maximum size, and bounded-string maximum size. The corresponding upstream
  tests skip when these APIs return supported rather than `RMW_RET_UNSUPPORTED`; those skips are not failures.
- The default parameter/type-description-enabled N=50 regression passed all create/wait/send/server/response and
  destroy/shutdown markers at 50/50 in 6.326 seconds, with zero timeout/error. The formal two-board N=50 gate passed
  50/50 in 15.974 seconds with zero client/process timeout or error. No relevant process remained, and the test
  socket was removed.

This closes the current foreign-loan crash, audited skip classification, board conformance failure, and matched-event
cache race. The control-channel supplement above separately closes the prior ACK-tail blocker. It does **not** make
the broad goal complete: valid full-stack RMW performance, broader security/action-bag, dynamic/broker subscription
zero-copy breadth, explicit upstream skips, and remaining P2/P3 stability gates are still open.

## rmw_mdds Default-Node Graph-Burst Supplement (2026-07-11)

Both boards were read back with
`librmw_mdds_cpp.so=b2ebceda4594842c120c686b53d4c12e910fccfd9ce1da818d490cdc60e322ac`,
`rmw_mdds_broker=5dbdd9d2d8408bb405af94fb8ae77e0940c49721cc2cccc76c2f13740fc1494d`, and
unchanged `libmdds_bridge_shared.z.so=9b6a5f2de6981f9df331ef7592dc189c72a119060f9859a1e4c9c57c0bba0597`.

- The previous default-node N=50 probe exceeded 139 seconds at 41 created clients, 16 sent/server requests, and
  14 responses. N=16 graph debug already generated 571 full broadcasts, 23,096 bridge match queries, and more
  than 3.3 million endpoint-target fan-out units.
- A host RED preserved 24/24 final graph convergence but counted 602 graph frames. The broker now coalesces dirty
  endpoint/match/sync events for 100 ms through one worker, emits a full snapshot, and retains the 1.5-second
  bridge heartbeat. The same test emits one 24-endpoint/24-target broadcast.
- ASAN exposed a shutdown race after removing the old heartbeat sleep: endpoint cleanup and node/graph-sync
  cleanup concurrently unsubscribed bridge entities. Stop now joins graph, accept, and client threads first.
  The complete 30-test `test_ipc_broker` gtest suite passed 10/10 ordinary repetitions and 20/20 ASAN
  repetitions; the ordinary package CTest passed 23/23. The ASAN evidence is scoped to `test_ipc_broker`, not
  all 23 package CTest targets.
- Current default parameter/type-description-enabled N=50 passed 50/50 in 9 seconds with zero timeout/error.
  Client broker work fell to 71 broadcasts and 3,222 match queries. Ten additional rounds each passed client and
  server 50/50 in 9--10 seconds.
- The formal minimal-node 50-process gate passed 10/10 fresh-domain rounds with zero timeout/error/process-timeout
  in 11.696--14.068 seconds. The 50-client single-process and one-client/50-request models also passed 50/50.
  Both boards ended with no relevant process or RMW socket, and no new broker/Python faultlog was created.

This closes the default-node graph/registration throughput blocker recorded below. It does not close explicit
upstream skips, valid full-stack RMW performance, broader security/action-bag, or all remaining P2/P3 evidence, so
Task 5.5 and production/full-feature completion remain open.

## rmw_mdds Endpoint-Capacity Supplement (2026-07-11)

Both boards were read back with
`librmw_mdds_cpp.so=7c5e5e36670ede9fd1be31fa303c620fad64132ccd358e318ec88d98aa4f4ee4`,
`rmw_mdds_broker=0679e2190ed73d064b66761f27441b47efd5481f209fbccd14128e735ac63145`, and
`libmdds_bridge_shared.z.so=9b6a5f2de6981f9df331ef7592dc189c72a119060f9859a1e4c9c57c0bba0597`.
No board identifier or lab address is retained in this report.

- RED board tests exhausted publisher/subscriber capacity at 64/64 and remote endpoint capacity at 128. The
  broker host RED also returned ACK and retained a request publisher after its required response subscription
  failed to create.
- MDDS capacity is now 512 publishers, 512 subscribers, matching reliability writer/reader slots, and 1024
  remote endpoints. Broker registration returns an explicit error and rolls back partial bridge state unless all
  required directions exist.
- Host `test_ipc_broker` passed 29/29, package CTest passed 23/23, and script contracts passed. Exact board
  binaries passed `MddsPubSubTest` 199/199 and `MddsEndpointDbTest` 24/24 with `BOARD_RC=0`.
- A no-keeper N=16 cross-board run passed 16/16. The formal N=50 independent-process small-service gate passed
  10/10 rounds with 50 create/send/server/response markers in every round, zero timeout/error/process-timeout,
  and elapsed time from 13.779 to 14.176 seconds. The 50-client single-process and one-client/50-request models
  also passed 50/50. Both boards had zero relevant processes and no RMW socket afterward.
- A separate default-node 50-process probe also enabled parameter and type-description services. It exceeded its
  139-second outer cap at 41 created clients, 16 sent/server-visible requests, and 14 responses. At this capacity
  checkpoint that remained a concurrent graph-churn/registration risk; the newer graph-burst supplement above
  records its subsequent RED/GREEN closure.

This supplement closes the small-service capacity/admission blocker only. Task 5.5 remains open and the
full-feature/production-ready decision remains **NOT COMPLETE** because explicit upstream skips, valid full-stack
RMW performance, broader security/action-bag, and other independent P2/P3 gates remain unresolved.

## rmw_mdds Connection-Recovery Supplement (2026-07-11)

This supplement is the current status for `RMW_IMPLEMENTATION=rmw_mdds_cpp`; the older Fast DDS and Cyclone DDS
matrices below remain evidence for those middleware artifacts and do not certify the current rmw_mdds bridge.
Both RK3588A boards were read back with RMW `0397797e...`, broker `810a97f5...`, bridge `db5b9d93...`, protected
probe `6aa06d13...`, overlay SoftBus client `e1771298...`, and native benchmark demo `7054cec3...`.

- DSoftBus connection-recovery RED/GREEN coverage now includes initial-BINDING recovery-window backpressure,
  full-window retry drain, single-sender flush gating, and active-flush LRU protection. The final RK3588A backend
  suite passed 158/158 with `BOARD_RC=0`.
- Exact-wire 16MiB service concurrent4x10 passed on three independent domains, each at client/server 40/40 with
  all process rc values 0 and no timeout/error; aggregate delivery was 120/120.
- Sequential exact-wire 16MiB long60 passed 60/60 with full request and response payload validation.
- Topic, service-request, and service-response exact-cap/`+1` cases passed 6/6. Exact-cap samples were delivered;
  each `+1` case produced an explicit `RCLError` at the correct API boundary and no unintended peer delivery.
- The final bridge passed the affected OpenSpec Task 6.3 lanes: native M2M 3/3; gateway pub/sub 8/8 plus
  service, action, parameters, and lifecycle; protected SROS2 signed-policy and authenticated/encrypted activation
  on both boards, authorized delivery of 60 messages, and unauthorized publish denial.
- A two-board, four-client, time-driven 2h concurrent large-message soak passed with 1,791/1,791 validated
  16MiB-class near-cap service round trips. All four DONE acknowledgements and all client/server return codes were
  successful; timeout, error, invalid, duplicate, and send-error counts were zero.
- A separate 15-minute graph-debug sample passed 121/121 and paired all 238 broker backpressure waits with unique
  resumes. Aggregate wait latency was min/p50/p95/p99/max 25/1,356/23,542/37,294/46,042 ms. Four 30-second
  diagnostic timeouts subsequently resumed and no wait reached 60 seconds. This closes the old stuck symptom but
  retains a material production tail-latency risk.
- A strict lane-level reliability-control priority experiment was rejected and reverted. Its instrumented
  15-minute run completed only 70/100 required round trips versus 121/121 on `c59c351a...`; aggregate p50/mean
  wait regressed from 1.356/5.763s to 8.551/10.457s. Although max decreased from 46.042s to 37.181s, the 42%
  throughput loss and worse central latency make the change unacceptable. Both boards were restored to
  `c59c351a...`, and the restored backend suite passed 158/158. Source tracing shows the remaining head-of-line
  boundary is the shared TCP_DIRECT byte stream after the lane, so a separate control channel or feedback-based
  pacing requires a new RED gate and design.
- One preliminary domain was excluded as launcher-invalid after host HDC processes crashed and cleanup stopped the
  board helpers. It is not counted as a middleware pass or failure. Board-side markers remain authoritative when
  host HDC exits 139.

At this connection-recovery checkpoint, the measured ACK tail remained a production risk. The later independent
control-channel supplement above closes that specific blocker. The rmw_mdds production/full-feature goal is still
**NOT COMPLETE** because explicit upstream skips, valid full-stack RMW performance comparison, broader security
combinations, dedicated action-bag CLI, and remaining P2/P3 stability gates are incomplete.

### Plan A shutdown and benchmark-integrity refresh (2026-07-10 EDT / 2026-07-11 CST)

This refresh uses exact DSoftBus artifacts `libmdds_bridge_shared.z.so=454bb992...` and
`mdds_demo=2d9e1b7a...` on both RK3588A boards. It supplements, rather than replaces, artifact-specific evidence
above.

- Backend quiesce now stops admission and joins asynchronous receive dispatch before upper MDDS managers are
  destroyed. The focused blocked-callback regression passed 1/1 and the full RK3588A DSoftBus backend suite
  passed 168/168.
- Protected bridge probes exited normally for 10/10 repetitions on each board. The signed protected SROS2 run
  passed policy validation and authenticated/encrypted activation on both boards, delivered 60/60 authorized
  messages, denied the unauthorized publisher, and passed the tampered/missing/identity/enforce/permissive
  negative matrix. No board-side shutdown SIGSEGV remained on this exact bridge.
- The old latency runner was captured returning PASS on partial RELIABLE echoes. The replacement accepts only the
  exact in-flight timestamp and requires every RELIABLE echo. Seven payload sizes from 128B through 4MiB passed
  10/10; forced partial RELIABLE delivery returned rc=1, while forced partial BEST_EFFORT retained observational
  rc=0 behavior.
- `MddsNodeManagerTest` is not green: two initialization fixtures still return `-7` before their expected path
  because they do not register the active DSoftBus mode operations now required by runtime initialization. This
  remains an explicit independent test blocker and was not hidden by changing expected values.

Therefore, at that Plan A checkpoint, only the protected shutdown crash and benchmark false-green were closed.
It did not certify full rmw_mdds parity or production readiness; the NodeManager test and other independent
P2/P3, conformance, sanitizer, and performance gates were still open.

### Lane worker ownership follow-up (2026-07-11)

The Plan A artifacts above exposed one further real shutdown failure at 4MiB. The board faultlog reported
`LaneWorkerMain+284` reading a recycled `ctx->lane` while the main thread was joining workers. Data and control
lane managers shared one global worker-context array/count, so control shutdown could free a context still used
by a data worker. The implementation now stores contexts by lane slot inside each manager; stopping control first
leaves the data manager dispatchable.

Exact final artifacts are bridge `db5b9d93aac4e9984145893d799cb03867f76c0d8478cbd3a8753e234aef3983`,
demo `7054cec3e03c95bf533d0662622f8b1a619e8d8472c42042fb09f6f7cc7ec0c0`, lane test
`ad573e4670170a825b60db0bf7b1be354b6d8b33be06ce44a5681591ed63cbc4`, and backend test
`3d5d656efb01975abb9fcf88822769d47361065c3dd1bbbf29b2476c67ff8dbf`.

- The deterministic RED test returned `-7` when a second manager tried to create its first worker after the first
  manager filled 12 slots. The final two-manager capacity/control-first-stop regression passed, and the complete
  RK3588A suites passed lane manager 36/36 and DSoftBus backend 168/168 with board rc 0.
- Previously corrected manager/pubsub binaries remain independently green at NodeManager 30/30 and PubSub
  198/198 with board rc 0.
- Cross-device RELIABLE latency passed 10/10 for all seven payloads from 128B through 4MiB. Forced partial 4MiB
  RELIABLE produced 160/1000, completeness FAIL, rc 1; BEST_EFFORT produced 109/1000 and retained rc 0.
- Signed protected SROS2 passed policy and authenticated/encrypted activation on both boards, delivered 60/60
  authorized samples, denied unauthorized publish, and emitted `cross_board_rmw_mdds_sros2_protected_ok`.
- Native final-bridge `rmw_mdds_cpp` M2M passed 3/3: RELIABLE topic 40/40, AddTwoInts `sum=42`, and Fibonacci
  `SUCCEEDED` with exact sequence `0,1,1,2,3,5` plus four feedback messages.
- No new `mdds_demo` faultlog was created by the final seven-size or forced-partial runs.

This closes the lane-context UAF and the affected 4MiB shutdown regression. It also supersedes the historical
NodeManager failure above. It does not close allocator/serialized-size/loaned-subscription skips, full-stack RMW
performance, broader security/action-bag, or every remaining P2/P3 gate, so the full-feature/production-ready
decision remains **NOT COMPLETE**.

**Date:** 2026-06-12 (supersedes 2026-06-11 preliminary run)
**Branch:** jazzy
**Re-verification (2026-06-12, fresh device check):** deployment integrity confirmed on both boards (overlay, underlay, in-underlay FastDDS libs, native numpy, zstd plugin all present), then the full matrix was re-run end-to-end — **67/67 PASS on each board** (core 25 + extended 42, domain segments A 60-80/160-199, B 20-40/100-139 to avoid cross-talk) and **bidirectional cross-board FastDDS transport PASS** (B→A and A→B, `ROS_DOMAIN_ID=55`, eth1). `ros2 pkg list` now reports 195 packages (+1 from the deployed `rosbag2_compression_zstd`).

**Clean-slate reproducible redeploy (2026-06-12):** to prove the migration is reproducible from committed artifacts rather than an accumulation of hand-patches, both boards' ROS 2 deployment was **wiped and re-created from scratch** via a single new script `ohos/deploy_all_rk3588a.sh <dev> --wipe` (underlay chunked + FastDDS + colcon overlay + global launcher + validation scripts, all from the host `install/` prefixes). This surfaced **one real reproducibility gap**: the underlay carried `lib/libc.musl-aarch64.so.1` as an *absolute* symlink to the device's `/lib/ld-musl-aarch64.so.1` (numpy's musl-libc dep), which device toybox tar refuses to extract (symlink escapes the prefix → `tar: had errors` → whole extraction fails). Fix: drop the symlink from the host prefix and recreate it on-device after extraction (in the deploy script). After the fix, both boards deployed cleanly and the **full 130-lane matrix (25 core + 42 extended + 63 CLI) re-ran 130/130 PASS on each board from the clean deployment**, plus cross-board FastDDS B↔A PASS. The migration is now scripted-reproducible end to end.

**Latest-jazzy re-pull re-verification (2026-06-15):** the core ROS 2 layer was pulled to current jazzy HEAD (DDS vendors kept pinned), OHOS deltas re-applied, affected packages rebuilt, and both boards clean-redeployed (see `MIGRATION_GUIDE_zh.md` §9). Re-running the full matrix surfaced **one regression — `cli_topic_delay` FAIL on both boards** ("topic [/ps] does not appear to be published yet"). Root cause was **not** a discovery/test-fixture issue: the fixture's `geometry_msgs/msg/PoseStamped` publisher was *crashing* at startup with `UnsupportedTypeSupport: Could not import 'rosidl_typesupport_c' for package 'geometry_msgs'`. Latest jazzy added `VelocityStamped` + `VelocityWithCovarianceStamped` to `geometry_msgs` (`common_interfaces`, commit `ac9fc9b`), but **geometry_msgs was pulled-but-never-rebuilt** — it lived in the `PULL_ONLY` set, not the OHOS-delta rebuild list — so the freshly-staged Python typesupport extension required `rosidl_typesupport_c__get_message_type_support_handle__geometry_msgs__msg__VelocityWithCovarianceStamped`, a symbol the stale April-built `libgeometry_msgs__rosidl_typesupport_c.so` never exported (relocation error at first publish). An on-device sweep of all 27 interface packages confirmed **geometry_msgs was the only casualty** (additive-message ABI skew). Fix: rebuild geometry_msgs from jazzy HEAD (all artifacts now export the new symbols, verified ARM aarch64) and redeploy the consistent set to both underlay + overlay on both boards. After the fix, the **full 130-lane matrix re-ran 130/130 PASS on each board** (`cli_topic_delay` → `average delay: …`), plus cross-board FastDDS std_msgs B↔A PASS and a cross-board `geometry_msgs/PoseStamped` B→A transport PASS (received `frame_id: cross_geo, x: 7.0`). **Lesson:** on a jazzy re-pull, the rebuild set must include any *interface* repo whose message set changed (e.g. `common_interfaces`), not just OHOS-delta repos — jazzy is ABI-stable so stale C++-only libs keep working, but additive `.msg` changes force a typesupport rebuild.
**CycloneDDS second-middleware migration (2026-06-15):** Eclipse **CycloneDDS** (`rmw_cyclonedds_cpp`) was cross-compiled and migrated alongside FastDDS (full write-up: `ohos/CYCLONEDDS_MIGRATION_zh.md`). `libddsc.so` 0.10.5 builds via new `ohos/build_cyclonedds_stack.sh` (aarch64/musl, `ENABLE_SECURITY=NO`, no iceoryx, only `libc.so` needed); `rmw_cyclonedds_cpp` builds against it and reuses the generic introspection typesupport (no per-message rebuild). **Key platform finding:** ROS 2 runtime RMW selection via the `rmw_implementation` dlopen "poco" **breaks C++ (rclcpp) typesupport on OHOS/musl** (`Registered Type must have a name` at /rosout creation; CycloneDDS segfaults) — musl's stricter `RTLD_LOCAL` scoping defeats C++ typesupport-identifier dedup across the dlopen boundary (the C/rclpy path tolerates it; `RTLD_GLOBAL` did not fix it). This is why the stock build direct-links the RMW. The platform-correct multi-DDS architecture is therefore **separate direct-linked deployments per DDS**: FastDDS stays the primary `ohos-prefix` (restored to 130/130, no regression), CycloneDDS ships as a direct-linked overlay `ohos-cyc` + `ros2-cyclone` launcher. **CycloneDDS validated on both boards:** C++ talker→listener 12/12, C++ service `add_two_ints` → 5, **cross-board C++ A→B over eth1** (9 msgs), and `ros2-cyclone doctor` reports `middleware name: rmw_cyclonedds_cpp`. Other RMWs: `rmw_connextdds` (RTI Connext) is proprietary/license-blocked; `rmw_gurumdds` not in tree — the migratable open-source DDS set (FastDDS + CycloneDDS) is complete.

**Two-board full-matrix — FastDDS + CycloneDDS + interop (2026-06-15):** new host-orchestrated harness `ohos/tools/run_cross_board_full_matrix.sh <devA> <devB>` (lane libraries `crossboard_lanes.sh` + `local_features.sh`; usage in `ohos/CROSS_BOARD_TEST_zh.md`) runs the full ROS 2 feature set across the **two physical RK3588A boards** in three DDS modes plus per-board local coverage of non-distributable features under both DDS. Live result on both boards: **TOTAL 99 PASS / 0 FAIL / 1 NA.** Breakdown: cross-board **FastDDS 19P/1NA**, cross-board **CycloneDDS 20P**, cross-vendor **interop (FastDDS↔CycloneDDS) 16P** (pub/sub-class both directions), **local FastDDS 22P** (2 boards), **local CycloneDDS 22P** (2 boards). The single NA is `fastdds_content_filter` — content-filtered topics are not enabled in the minimal FastDDS build (the same lane **PASSES on CycloneDDS**, which does support CFT). Cross-board lanes proven over eth1: rclcpp + rclpy pub/sub, geometry_msgs/PoseStamped, QoS (best-effort/reliable), serialized messages, tf2, services, actions, lifecycle, parameters, CLI graph introspection (node info / topic type·find·echo·hz·bw), and rosbag2 record→play. Per-board local lanes (both DDS): in-process composition, component container (class_loader/pluginlib), wait sets, logging, topic statistics, rclpy loopback, image/point-cloud transport, interface/pkg introspection, and `doctor` confirming the per-board RMW identity. **Key cross-vendor finding:** FastDDS↔CycloneDDS pub/sub interoperates (RTPS) both directions, but request/reply RPC (services/actions) does **not** interoperate cross-vendor on this platform (the call hangs) — so interop mode runs pub/sub-class lanes only, with a device-side timeout guard.

**Build:** `install/ohos-colcon-rk3588a` (45-package colcon overlay, 58 resources after runtime closure) over `install/ohos-ros2` (194-resource standalone underlay)

## Devices

| ID | Role | Deployment |
|---|---|---|
| `<RK3588A_A>` | Device A | underlay `/data/local/tmp/ohos-prefix` (194 resources) + overlay `/data/local/tmp/ohos-colcon-rk3588a` (58 resources) + FastDDS `/data/local/tmp/ohos-fastdds` |
| `<RK3588A_B>` | Device B | identical layout to Device A |

Both devices: Linux 6.6.101, aarch64, OHOS clang 15.0.4, Python 3.12 at `/data/local/release/usr/bin/python3.12` (34 lib-dynload modules), real numpy **1.26.4 cross-compiled for `aarch64-linux-ohos`** (crossenv + OHOS clang + meson cross file; native `.cpython-312-aarch64-linux-ohos.so` extensions, DT_NEEDED only `libpython3.12.so.1.0` + `libc.so`; staged in both prefixes' site-packages). An earlier Alpine 1.25.2 port validated the approach and was superseded by this native build.

Cross-board link: `eth1` - A `<DEVICE_A_IP>` / B `<DEVICE_B_IP>`.

Validation harness: `ohos/tools/rk3588a_validate_all.sh` (25 lanes, board-side) + `ohos/tools/rk3588a_bag_lanes.sh` (rosbag2 lanes, foreground-recorder variant) + `ohos/tools/run_cross_board_cli_pubsub.sh` (host-orchestrated). Each lane emits `RESULT|<lane>|PASS/FAIL|<evidence>`.

---

## Full Feature Matrix (both boards)

| # | Lane | What it proves | Device A | Device B | Evidence |
|---|---|---|---|---|---|
| 1 | cli_pkg_list | overlay+underlay ament index | ✅ | ✅ | 194 packages |
| 2 | cli_topic_list | graph visibility (`--no-daemon`) | ✅ | ✅ | `/parameter_events`, `/rosout` |
| 3 | cli_interface_show | msg/action definitions ×6 | ✅ | ✅ | String/Twist/Image/Odometry/TFMessage/Fibonacci |
| 4 | rclcpp_pubsub | C++ talker→listener | ✅ | ✅ | 7 msgs received |
| 5 | rclcpp_service | C++ service roundtrip | ✅ | ✅ | `result of 41 + 1 = 42` |
| 6 | rclcpp_action | C++ action client+server | ✅ | ✅ | Fibonacci sequence to 55 |
| 7 | lifecycle_cpp | full lifecycle state machine | ✅ | ✅ | configure→activate→deactivate→cleanup→shutdown |
| 8 | composition_dlopen | class_loader in-process composition | ✅ | ✅ | talker+listener in one process |
| 9 | component_cli | component_container + `ros2 component load/list` | ✅ | ✅ | composition::Talker loaded |
| 10 | tf2_roundtrip | static_transform_publisher + tf2_echo | ✅ | ✅ | `Translation: [1.000, 2.000, 3.000]` |
| 11 | robot_state_publisher | URDF → /tf chain (urdfdom/kdl_parser) | ✅ | ✅ | fixed transform `[0, 0, 1]` resolved |
| 12 | pluginlib | plugin discovery | ✅ | ✅ | `urdf_xml_parser/URDFXMLParser` |
| 13 | rclpy_node_cli | rclpy node + `ros2 node/param/topic echo` | ✅ | ✅ | param set/get + live echo |
| 14 | rclpy_service | rclpy Trigger server + `ros2 service call` | ✅ | ✅ | `success=True` |
| 15 | rclpy_action | rclpy Fibonacci server + `ros2 action send_goal` | ✅ | ✅ | SUCCEEDED (post-numpy fix) |
| 16 | demo_py_pubsub | demo_nodes_py talker + CLI echo | ✅ | ✅ | `Hello World` echoed |
| 17 | demo_py_service | demo_nodes_py AddTwoInts | ✅ | ✅ | `2 + 3 = 5` |
| 18 | lifecycle_py | Python lifecycle node + C++ driver | ✅ | ✅ | `on_activate()` reached |
| 19 | bag_sqlite3 | `ros2 bag record/info` (sqlite3) | ✅ | ✅ | 11 messages |
| 20 | bag_mcap | `ros2 bag record/info` (mcap) | ✅ | ✅ | 11 messages |
| 21 | bag_play | `ros2 bag play` → listener | ✅ | ✅ | replay received |
| 22 | ros2_launch | `ros2 launch` talker_listener | ✅ | ✅ | launched exchange observed |
| 23 | mixed_cli_cpp_action | overlay CLI goal vs underlay C++ server | ✅ | ✅ | SUCCEEDED, **no SIGSEGV** |
| 24 | ros2_run | `ros2 run demo_nodes_cpp talker` | ✅ | ✅ | publishing via run |
| 25 | ros2_doctor | doctor CLI loads | ✅ | ✅ | usage banner |

**Score: 25/25 on both boards.**

### Cross-board (ROS_DOMAIN_ID=55, eth1, rmw_fastrtps_cpp)

| Direction | Result | Payload |
|---|---|---|
| B → A (`ros2 topic pub` → `ros2 topic echo --once`) | ✅ PASS | `hello_from_B_55` |
| A → B | ✅ PASS | `hello_from_A_55` |

---

## Issues Found and Fixed During This Run

| Issue | Root cause | Fix (host + both boards) |
|---|---|---|
| `ros2 action send_goal` SIGSEGV (was "Known Issue: mixed-lib ABI conflict") | **numpy stub**: pure-python fake `ndarray(list)`; Release-built `*_s.c` typesupport calls `PyArray_GETPTR1` on it for fixed arrays (action goal UUID `uint8[16]`) → wild pointer arithmetic. Never an overlay/underlay mixing problem. | Real numpy **1.26.4 cross-compiled natively** for `aarch64-linux-ohos`: host CPython 3.12.7 + crossenv against `build/ohos-python-runtime/usr`, enriched `_sysconfigdata__linux_aarch64-linux-ohos.py`, clang wrapper injecting `-L<runtime>/lib` only on link calls (compile-only `-L` breaks meson's `cc.sizeof` probes via `-Werror=unused-command-line-argument`), meson cross file (`longdouble_format='IEEE_QUAD_LE'`), host-side `build-pip install meson meson-python ninja patchelf Cython`, then `cross-pip install --no-build-isolation --no-deps --config-settings=setup-args=--cross-file=…` from the local sdist. Deployed into both prefixes on both boards (replacing the stub); `stage_colcon_runtime_closure.sh` prefers real numpy over the stub. An interim Alpine 1.25.2 port (renamed musl extensions) validated the approach first. |
| `HOME` unset → log dir failure | wrappers didn't guard | All overlay+underlay wrappers now export `HOME=/data/local/tmp` and `ROS_LOG_DIR` when unset/unwritable (template fixed in `colcon_rk3588a.sh`); verified with `env -i ros2 pkg prefix` |
| FastDDS libs not on underlay path | `libfastrtps`/`libfastcdr` lived only in `install/ohos-fastdds` | merged into `install/ohos-ros2/lib` (self-contained underlay) |
| vendor `.so` only under `opt/*/lib` (libyaml, spdlog, sqlite3, lz4, zstd, yaml-cpp, orocos-kdl) | every consumer needed bespoke `LD_LIBRARY_PATH` | merged into `prefix/lib` |
| `import yaml` ModuleNotFoundError | host underlay `site-packages/yaml` was a **symlink to `/usr/lib/python3/dist-packages/yaml`** → dangling on device | replaced with real copy |
| underlay CLI `No module named 'packaging'` | helper pure-python pkgs (`packaging`, `pyparsing`, `argcomplete`, `catkin_pkg`, `em`, `psutil`) only staged in overlay | copied into underlay site-packages |
| `ros2 bag record` never finalized (`metadata.yaml` missing) | POSIX sh sets SIGINT to SIG_IGN for background jobs; CPython does not re-enable ignored signals → graceful stop impossible | recorder runs **foreground** with background watchdog sending INT (`rk3588a_bag_lanes.sh` pattern) |

## Remaining Notes

- `ros2 bag record` must not be started as a sh background job anywhere (signal disposition trap above). Use the foreground+watchdog pattern.
- `install/ohos-colcon-rk3588a` does **not** need `khd_rk3588_a` build output: `colcon_rk3588a.sh` only requires the underlay, FastDDS prefix, and `build/ohos-python-runtime/usr` (the old `RELEASE_SITE_PACKAGES_ROOT` note was wrong).
- HDC host quirk persists: status `-1`/segfault after valid stdout; board-side markers remain the reliable signal.
- Validation leftovers (daemons, stray nodes) are cleaned by the harness on exit.

---

## Phase 2 — Extended Feature Matrix (`rk3588a_validate_ext.sh`)

A second 42-lane matrix covering the feature surface beyond the core 25 lanes.
**Score: 42/42 on both boards** (Device A `DOM_BASE=160`, Device B `DOM_BASE=100`).

| # | Lane | Feature | Evidence |
|---|---|---|---|
| 1 | ext_qos_message_lost | QoS message-lost event | latency reported |
| 2 | ext_qos_incompatible | QoS incompatibility event (reliability) | event fired |
| 3 | ext_qos_deadline | Deadline QoS | demo ran |
| 4 | ext_qos_lifespan | Lifespan QoS | demo ran |
| 5 | ext_qos_liveliness | Liveliness QoS (assert/lease) | demo ran |
| 6 | ext_qos_overrides | QoS overrides from params | received |
| 7 | ext_qos_best_effort | Best-effort reliability | received |
| 8 | ext_param_set_get | set_and_get_parameters | ran |
| 9 | ext_param_list | list_parameters | ran |
| 10 | ext_param_event_handler | ParameterEventHandler callback (node `this_node`, `an_int_param`) | `cb1: Received an update…` |
| 11 | ext_param_blackboard_cli | parameter_blackboard + `ros2 param set/get/list/dump` | value round-trips |
| 12 | ext_param_callback | on-set-parameter callback side effect | `param2=4.0` |
| 13 | ext_timer_oneoff | one-off + reuse timers | ran |
| 14 | ext_serialized_msg | serialized-message pub/sub | received |
| 15 | ext_loaned_msg | loaned-message publish | published |
| 16 | ext_content_filter | content-filtered subscription | received |
| 17 | ext_service_introspection | service introspection (`service_configure_introspection=metadata` → `/add_two_ints/_service_event`) | `event_type`, `client_gid` captured |
| 18 | ext_matched_event | pub/sub matched events | event seen |
| 19 | ext_logging_demo | logger severity output | severity output |
| 20 | ext_logger_service | runtime logger-level service | DEBUG/WARN/ERROR transitions |
| 21 | ext_wait_set | wait-set talker/listener | received |
| 22 | ext_wait_set_sub | minimal-subscriber wait-set (`/topic`) | received |
| 23 | ext_topic_statistics | topic statistics (`message_age`/`message_period`) | metrics published |
| 24 | ext_rclpy_executors | rclpy executors talker/listener | received |
| 25 | ext_rclpy_callback_group | rclpy callback groups | ran |
| 26 | ext_rclpy_guard | rclpy guard condition | triggered |
| 27 | ext_rclpy_action_cancel | rclpy action cancel | canceled |
| 28 | ext_action_tutorials_py | action_tutorials_py roundtrip | Fibonacci result |
| 29 | ext_rclpy_qos | rclpy incompatible-QoS event | event fired |
| 30 | ext_topic_monitor | topic_monitor reception-rate | monitoring |
| 31 | ext_image_transport | image_transport raw plugin | raw declared |
| 32 | ext_point_cloud_transport | point_cloud_transport raw plugin | raw declared |
| 33 | ext_cli_node_info | `ros2 node info` | publishers listed |
| 34 | ext_cli_topic_hz | `ros2 topic hz` | rate measured |
| 35 | ext_cli_topic_bw | `ros2 topic bw` | bandwidth measured |
| 36 | ext_cli_topic_type_find | `ros2 topic type` + `find` | type + find |
| 37 | ext_cli_service_type | `ros2 service list` + `type` | list + type |
| 38 | ext_cli_multicast | `ros2 multicast send/receive` | send/receive |
| 39 | ext_bag_reindex | `ros2 bag reindex` | metadata rebuilt |
| 40 | ext_bag_burst | `ros2 bag burst -s sqlite3 -n 10` | bursted |
| 41 | ext_bag_convert | `ros2 bag convert` sqlite3→mcap | converted |
| 42 | ext_bag_compression | `ros2 bag record --compression-format zstd` | `compression_format: zstd` in metadata |

### Phase-2 Bugs Found and Fixed

| Issue | Type | Root cause | Fix |
|---|---|---|---|
| zstd bag compression rejected (`--compression-format: invalid choice (choose from )`) | **Real missing component** | `rosbag2_compression_zstd` plugin never built into the prefix; the compression-format choice list was empty | Cross-built `rosbag2_compression_zstd` against `zstd_vendor` (`-Dzstd_LIBRARY/-Dzstd_INCLUDE_DIR` hints), deployed `librosbag2_compression_zstd.so` + pluginlib resource index + ament package index to both boards; zstd now a valid format and `compression_format: zstd` is recorded |
| ext_param_event_handler false-positive then failure | Test bug (masking) | wrong node name (`node_with_parameter_event_handler` vs actual `this_node`); loose grep `\|5` matched timestamps so it "passed" by luck | corrected to `/this_node an_int_param`, tightened grep to the real callback string `cb1: Received an update…` |
| ext_service_introspection no `_service_event` publisher | Test bug | demo defaults to introspection `disabled`; must be enabled via param | set `service_configure_introspection=metadata` before echoing the event topic |
| ext_bag_burst "no plugin found that could open URI" | Test bug | `ros2 bag burst` doesn't auto-detect storage | pass `-s sqlite3` |
| QoS deadline/lifespan/liveliness `stoul: no conversion`; incompatible_qos usage | Test bug | wrong CLI args (binaries take a positional duration + specific flags) | corrected per each demo's `--help` |
| ext_wait_set_sub no message | Test bug | subscriber listens on `/topic`, driver published `/chatter` | publish on `/topic` |
| ext_topic_statistics usage banner | Test bug | needs `string --publish-period` positional/flag | corrected args |
| Device B every lane failed (`domainId is over 232`) | Harness bug (mine) | launched Device B with `DOM_BASE=260`; ROS 2 domain IDs are 0–232 | use `DOM_BASE=100`; script now guards `DOM_BASE>193` and aborts early |

Iteration history: round 1 = 32/42, round 2 = 40/42, round 3 (Device A) = 41/42, final = **42/42 on both boards** after the node-name fix. The only non-test defect was the missing zstd compression plugin, which was built and deployed rather than waived.

## Phase 3 — CLI Command Matrix (`rk3588a_validate_cli.sh`)

A command-centric pass that exercises **every `ros2 <command> <subcommand>`** enumerated
from `ros2 <cmd> --help`, against a live fixture graph (talker, introspection
service+client, parameter_blackboard, Fibonacci action server, lifecycle node,
component container, recorded bag). **63/63 PASS on both boards.**

| Command group | Subcommands tested (all PASS, 2/2 boards) |
|---|---|
| `ros2 pkg` | list, prefix, executables, xml, **create** |
| `ros2 interface` | list, show, package, packages, proto |
| `ros2 node` | list, info |
| `ros2 topic` | list, info, type, find, echo, hz, bw, delay, pub |
| `ros2 service` | list, type, find, info, call, echo |
| `ros2 param` | list, set, get, describe, dump, load, delete |
| `ros2 action` | list, info, type, send_goal |
| `ros2 lifecycle` | nodes, list, get, set |
| `ros2 component` | types, load, list, unload, standalone |
| `ros2 bag` | record, info, list (storage), reindex, burst, convert, play |
| `ros2 daemon` | start, status, stop |
| `ros2 multicast` | receive + send |
| `ros2 plugin` | list |
| `ros2 doctor` / `wtf` | --report |
| `ros2 run` / `ros2 launch` | executable / launch file |

### Phase-3 Bugs Found and Fixed

| Issue | Type | Root cause | Fix |
|---|---|---|---|
| `ros2 pkg create` → `No module named 'ament_copyright'` | **Real missing component** | the `create` entry point imports `ament_copyright`, which was never staged into the prefix | staged the pure-python (stdlib-only) `ament_copyright` module from the workspace into both prefixes' site-packages on both boards; `stage_colcon_runtime_closure.sh` now stages it automatically. `ros2 pkg create` then scaffolds `package.xml` + `CMakeLists.txt` + `src/` + `include/` (EXIT 0) |
| `ros2 service echo` test → "no publishers on `_service_event`" | Test bug | the plain `add_two_ints_server` fixture doesn't enable service introspection | switched the service fixture to `introspection_service` + `introspection_client` and set `service_configure_introspection=metadata`; echo then captures `event_type: REQUEST_RECEIVED` |
| `ros2 bag reindex` test → "no metadata" | Test bug | the record fixture used the build's default storage (mcap), and `cp -r clibag clibag_ri` into a stale dir from a prior run nested instead of replacing, feeding reindex the wrong storage file | record explicitly with `--storage sqlite3` and `rm -rf` the reindex/convert target dirs before copying |
| `ros2 bag burst` test hung the whole matrix | Test bug | `ros2 bag burst -n N` bursts N messages then **stays paused** (never exits); the lane ran it in the foreground with no watchdog | run burst backgrounded and kill it after the burst lands (it published exactly 5 messages, the listener heard 5) |

Only `ros2 pkg create`'s missing `ament_copyright` was a real on-device defect; the other three were test-harness bugs. Iteration: run 1 = 59/62 (3 fails), run 2 fixed pkg-create + service-echo + bag-storage, run 3 caught the burst hang, final = **63/63 on both boards** (the suite grew to 63 lanes after adding `ros2 service info`).

## Conclusion

For the recorded Fast DDS artifacts, ROS 2 Jazzy on KaihongOS RK3588A (`aarch64-linux-ohos`, musl) passed **130 validated lanes** (25 core + 42 extended + 63 CLI-command, each 2/2 boards): rclcpp + rclpy pub/sub, services, actions (incl. cancel/async, C++ and Python), lifecycle (C++/Python), composition + component CLI, the full QoS policy set (reliability/durability/deadline/lifespan/liveliness/overrides), the complete parameter API (set/get/list/dump, event handler, on-set callback), timers, serialized + loaned messages, content filtering, service introspection, wait sets, topic statistics, logging + runtime logger levels, executors/callback-groups/guard-conditions, image/point-cloud transports, tf2, robot_state_publisher, pluginlib, rosbag2 (sqlite3 + mcap; record/info/play/burst/reindex/convert/zstd-compression), launch, and bidirectional cross-board DDS. **Every `ros2` CLI command and subcommand** is
exercised by the Phase-3 command matrix (`pkg` incl. `create`, `node`, `topic` incl.
hz/bw/delay/find/pub, `service` incl. echo/info, `param` full, `action` incl. type, `interface`
incl. proto, `lifecycle`, `component` incl. standalone, `bag` incl. burst/convert/reindex,
`daemon`, `multicast`, `plugin`, `doctor`/`wtf`, `run`, `launch`). Every issue surfaced during
testing was root-caused and fixed (cross-building the missing zstd compression plugin, staging the
missing `ament_copyright` for `ros2 pkg create`) rather than worked around.

This historical Fast DDS matrix does not certify the current `rmw_mdds_cpp` artifact as full-feature or
production-ready.

## rmw_mdds Current Checkpoint (2026-07-12)

- Exact artifacts on both boards: RMW `4eb87ee4...`, broker `d754adba...`, broker dynamic probe `58d4f756...`,
  dynamic runner `db694210...`, and non-sanitized MDDS bridge `6e08033e...`.
- Dynamic broker subscription loans pass seven local cases per board and String/sequence/nested remote delivery in
  both directions. Every lane reports `BOARD_RC=0`; all loan-pool counts return to zero.
- A persistent broker per board passes all 16 AArch64 `test_rmw_implementation` programs, 129 assertions, six
  classified conditional skips, and zero failures. Without a SoftBus restart afterward, bidirectional String,
  AddTwoInts, and Fibonacci action pass through `rmw_mdds_cpp`; the strict topic lane receives 40/40.
- Current host source passes package CTest 24/24, upstream RMW tests 16/16, ASAN 24/24 with leak detection, TSAN
  24/24, and script/artifact contracts.
- Historical blocker at this checkpoint: after 14 rapid broker process Init/TERM/Shutdown cycles per board using
  the same fixed MDDS DSoftBus identity, immediate remote graph matches stayed at zero. Later diagnosis established
  a 10-failures/60-second bind-denial threshold with a 600-second protection interval that expires automatically;
  restarting `softbus_server` was only a workaround. The process-scoped outgoing identity checkpoint above closes
  this blocker with 14/14 restart and immediate bidirectional evidence.
- Current exact-artifact board ASAN/TSAN, two-hour full-stack soak, and the broad P0-P3 feature matrix still require
  repetition. Therefore the overall `rmw_mdds_cpp` production/full-feature goal remains **not complete**.
