# RK3588A core ROS 2 delivery — 2026-09-08

## Delivered scope

The requested core stack is deployed and validated on both KaihongOS
6.1.0.04 / aarch64 boards. Scope: rcl, rclcpp, rclpy, rmw_implementation,
rmw_fastrtps/Fast DDS and rmw_cyclonedds/Cyclone DDS. MDDS, rmw_mdds and
mdds_gateway are excluded from this build, package and acceptance.

| Board | Serial | Wired IPv4 |
| --- | --- | --- |
| A | 3e01ff55454d202020104033bf453b00 | 192.168.77.201 |
| B | 3e01ff55454d202020104433991c3b00 | 192.168.77.202 |

The active prefix is `/data/local/tmp/ros2-generic`; `/usr/local/bin/ros2`
automatically loads its verified environment. Fresh-shell commands need no
manual `export` or `source`:

```sh
ros2 doctor
ros2 pkg prefix rclcpp
ros2 run demo_nodes_cpp talker
ros2 run demo_nodes_py listener
```

Fast DDS (`rmw_fastrtps_cpp`) is the compiled and CLI default. Cyclone DDS
(`rmw_cyclonedds_cpp`) was tested explicitly against the same archive. An
explicit `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp` selects that backend; MDDS is
intentionally rejected by the new global entry.

## Verification

| Gate | Result |
| --- | --- |
| Empty fixed-source import and patch replay | 111 repository trees matched; 33 patches |
| Clean cross-build | 364 packages; every colcon job returned zero |
| Target artifact audit | 953 AArch64 ELF files; target Python suffix and launcher checks passed |
| Fast DDS RTTI ownership | Provider definitions and consumer references passed |
| Fast DDS native RMW APIs | Both boards: 135 passed, no skipped cases, across 16 executables |
| Cyclone DDS native RMW APIs | Both boards: 131 passed, 4 skipped, across 16 executables |
| C++/Python loopback and cross-board communication | Both RMW end-to-end runs passed actual payload matching |
| Parameters and lifecycle | Both RMW runs passed |
| C++/Python services and actions | Both RMW runs passed responses and exact Fibonacci results |
| rosbag2 SQLite recording/playback | C++ and Python messages passed for both RMWs |
| LTTng tracing | Both runs produced and decoded actual ROS events in private mount namespaces |
| Shutdown and integrity | Owned processes drained; install and Python trees reverified |
| rclcpp signal-chain regression | Both boards: 6/6 |
| rclpy service-lifetime regression | Both boards, both RMWs: 2/2 each |
| Cyclone resource-limit regression | Both boards: 3/3 |
| Default CLI after old-prefix retirement | Both boards: doctor 5/5, exit 0; package paths point to the new prefix |

Cyclone's four skips are positive loaned-message tests gated by its current
`can_loan_messages` capability. Their unsupported-return paths were checked;
positive loan/zero-copy support is not claimed. This is the recorded acceptance
matrix, not a claim that the entire upstream ROS test suite or every optional
RMW feature was tested. GUI/SHM remain experimental; DDS Security/TLS remains
outside this profile. No reboot test was performed.

## Porting fixes and runtime policy

- rclpy explicitly uses the verified target SOABI rather than a host suffix.
- pybind11_vendor propagates a normal target suffix variable that survives
  pybind11's interpreter-discovery cache reset; this also fixes rosbag2_py and
  point_cloud_transport_py. Cold-configuration and native-board import tests
  passed. The audit rejects host-named Python ELF modules.
- Python console entrypoints use the available `/bin/env`. The audit checks
  generated entrypoints before the install is sealed.
- The deployment size check no longer requires the board's absent `tr` tool.
  Postcheck failures retain their actual diagnostics before rollback.
- A content-addressed, hash-verified ROS-only `sitecustomize.py` enables
  `RTLD_GLOBAL` for Python-loaded C++ libraries on OHOS. This provides the
  canonical RTTI required by class_loader plugin factories. It is bound in
  deployment provenance and does not modify shared CPython or system libraries.
- Doctor-only, hash-checked pure-Python dependencies are isolated under
  `ros2-core-config`: rosdistro 1.1.0, rospkg 1.6.2, distro 1.9.0. They are
  appended only for `ros2 doctor`; existing core dependencies are not replaced.
  Fixed wheel hashes are in `scripts/runtime_config/doctor-requirements.txt`.
- Doctor uses an official offline Jazzy metadata snapshot at ros/rosdistro
  commit `2b0767951219199c62c8fb28a1a225a301a162f4`. Version notices refer to that
  snapshot, not a live latest-version check. Genuine version/missing-metadata
  warnings remain visible; checks are not suppressed.

## Immutable identities

| Item | SHA-256 |
| --- | --- |
| ROS archive | `2ab1a4a02c405ee98b5c27377f7f74cb355f5d4d56e80ecd6b4f49c64a807dfd` |
| Build source snapshot | `3a4b60dad8ee006bb33bf056211418e2ddf867dc79b7eafdd3d55e62950ab2f2` |
| SDK23 fingerprint | `c9ec5144bd19b806c1d72f6e80fdcadd223c8ab7ccc739286417310cfc11f6e7` |
| Build receipt | `f38f783233d876c605e3a2b2fc642b304da95c90ba5713f20255d6e1b6f8a0d5` |
| Deployment provenance | `c1a68918488fc85cb963ee31c6dbfe332f24bcd6f4cf7b8a4f37cdffbbdf30ef` |
| CPython 3.12.7 source-built archive | `c646982b346f5578b39dabb91cdd267305a6f4fc279a643d56e847b37bb69109` |
| ROS Python bootstrap | `16920f1f6460d75a65be821e3ddac34cdc5f2a01b65e427414c7f7118adc438d` |
| Final global CLI entry | `f9a8595287f8cdf467fab8c99d81de46744e4ae2d6954518bf0fa1641628477e` |
| Doctor dependency manifest | `0ff9eadde0e55db78bcbc08da1bf132b5dffc6393952344b73513d714fff3a07` |
| Doctor dependency archive | `c3c1c22ee8acfa40fc3736128a1244daa61eb5c599d7666f85dc689733c8bee5` |

The Python runtime was reproduced using fixed official CPython sources and
Jiusi-pys/python configuration; its source receipt and recipe hashes are retained
in the build/deployment records. Source reproducibility is not a promise of
bit-identical rebuilds on arbitrary hosts.

## Evidence and recovery

Host delivery bundle:
`C:/Users/17715/Documents/codes/M-DDS/ros2/ohos_test_logs/migration_20260908/release/`.
It includes the ROS archive, build receipt, deployment provenance, both RMW
acceptance records, final board logs, doctor dependency archive and a core meta
patch against public commit `f22a4f1754f967c73880554b19fdccb030b890d9`.
The meta patch SHA-256 is
`26c488978063da6e47f587a90e067a29eb25ecf02f625955819736293c96bfcb`.
Runtime deployment tools/configuration are supplied separately in that bundle;
do not substitute a build-source hash for their separately recorded hashes.

Full logs, rejected attempts and diagnostic-only runs remain under
`ohos_test_logs/migration_20260908/` and the build workspace's
`ohos_test_logs/clean_release/`. Only `final_fastdds` and `final_cyclone` result
records are formal end-to-end acceptance. Earlier diagnostic or rejected
packages must not be presented as accepted releases.

On each board, the retired sibling-repository deployment and prior global
entry are preserved at:

`/data/local/tmp/ros2-core-migration-backup-20260908/`

Old active prefixes were moved into its `retired/` directory, not irreversibly
deleted. Old ROS installer archives/parts were additionally moved into
`retired-installers/`. Shared Python, system DSoftBus and the independent MDDS development
prefix `/data/local/tmp/ros2` were preserved. Root filesystems were restored to
read-only after global-entry updates. Runtime evidence predates the subsequent
local source/documentation commit; it is not evidence of a GitHub publication.
Unrelated working-tree changes were preserved. For another machine, follow the
[reuse guide](rk3588a_core_reproduction.md); local artifact paths below are not
download URLs and must be transferred separately.
