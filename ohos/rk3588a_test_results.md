# RK3588A Test Results

**Date:** 2026-06-12 (supersedes 2026-06-11 preliminary run)
**Branch:** jazzy
**Build:** `install/ohos-colcon-rk3588a` (45-package colcon overlay, 58 resources after runtime closure) over `install/ohos-ros2` (194-resource standalone underlay)

## Devices

| ID | Role | Deployment |
|---|---|---|
| `3e01ff55454d202020104033bf453b00` | Device A | underlay `/data/local/tmp/ohos-prefix` (194 resources) + overlay `/data/local/tmp/ohos-colcon-rk3588a` (58 resources) + FastDDS `/data/local/tmp/ohos-fastdds` |
| `3e01ff55454d202020104433991c3b00` | Device B | identical layout to Device A |

Both devices: Linux 6.6.101, aarch64, OHOS clang 15.0.4, Python 3.12 at `/data/local/release/usr/bin/python3.12` (34 lib-dynload modules), real numpy 1.25.2 (Alpine musl aarch64, extensions renamed to `-ohos` suffix).

Cross-board link: `eth1` — A `192.168.77.10` / B `192.168.77.11`.

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
| `ros2 action send_goal` SIGSEGV (was "Known Issue: mixed-lib ABI conflict") | **numpy stub**: pure-python fake `ndarray(list)`; Release-built `*_s.c` typesupport calls `PyArray_DATA` on it for fixed arrays (action goal UUID `uint8[16]`) → wild memcpy. Never an overlay/underlay mixing problem. | Real numpy 1.25.2 from Alpine musl aarch64 (`py3-numpy` + `openblas` + `libgfortran` + `libgcc` apks); extensions renamed `*-musl.so` → `*-ohos.so`; `libc.musl-aarch64.so.1 → /lib/ld-musl-aarch64.so.1` symlink; staged into underlay site-packages + `lib/`. `stage_colcon_runtime_closure.sh` now prefers real numpy over the stub. |
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

## Conclusion

ROS 2 Jazzy on KaihongOS RK3588A (`aarch64-linux-ohos`, musl) is **fully migrated and feature-complete** on both boards: rclcpp + rclpy pub/sub, services, actions (C++ and Python clients/servers), lifecycle (C++/Python), composition + component CLI, tf2, robot_state_publisher, pluginlib, rosbag2 (sqlite3 + mcap, record/info/play), launch, the full ros2 CLI verb set, and bidirectional cross-board DDS communication. All three previously-known issues are root-caused and fixed rather than worked around.
