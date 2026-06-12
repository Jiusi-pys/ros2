<!-- codex-file-meta: begin
relative_path: "ohos/README.md"
language: "markdown"
summary: "Markdown document \"ROS 2 on KaihongOS\". This subtree cross-builds a standalone ROS 2 slice for KaihongOS/OpenHarmony and documents the selected-package RK3588S/OHOS colcon wrapper."
symbols: ["ROS 2 on KaihongOS"]
generated_by: "codebase-frontmatter-summary"
codex-file-meta: end -->

# ROS 2 on KaihongOS

This subtree cross-builds a standalone ROS 2 slice for KaihongOS/OpenHarmony
and carries a selected-package colcon migration wrapper for RK3588S/OHOS.

Scope:

- `ros2_rcutils`: direct build of `src/ros2/rcutils`
- `ros2_rcpputils`: direct build of `src/ros2/rcpputils`
- `ros2_ohos_smoke`: device-side smoke test
- `ros2_ohos_pubsub_smoke`: standalone `rcl` + `rmw_fastrtps_cpp` publish-subscribe smoke test
- `ros2_ohos_dummy`: shared library used to validate `dlopen` through `rcpputils::SharedLibrary`
- `build_ros2_bootstrap.sh`: installs the host-side `ament_*` and Python bootstrap into `install/ohos-ros2`
- `build_ros2_package.sh`: builds individual ROS 2 packages into the standalone OHOS prefix
- `colcon_rk3588s.sh`: builds selected ROS 2 packages through the RK3588S/OHOS colcon migration wrapper
- `build_fastdds_stack.sh`: builds `foonathan_memory`, `Fast-CDR`, `Fast-DDS`, and the upstream BasicConfiguration example as a standalone FastDDS smoke stack
- `deploy_fastdds_smoke.sh`: deploys the FastDDS example to RK3588S and runs subscriber/publisher end-to-end
- `deploy_ros2_prefix.sh`: syncs the full standalone ROS 2 prefix to `/data/local/tmp/ohos-prefix` on a board
- `deploy_ros2_prefix_chunked.sh`: deploys the standalone ROS 2 prefix as a chunked gzip bundle for unstable HDC links

The build uses both local toolchain bundles:

- `command-line-tools`: SDK sysroot, CMake toolchain files, target compiler
- `OpenHarmony/prebuilts`: host `cmake`, `ninja`, and `llvm-strip`

Current standalone prefix coverage in `install/ohos-ros2` has 194
ament-index package resource entries, plus related non-ament runtime
dependencies installed into the same prefix. Highlights include:

- `ament_*`, `ament_index_*`
- `rcutils`, `rcpputils`
- `rosidl_*`
- `builtin_interfaces`, `service_msgs`, `type_description_interfaces`, `rcl_interfaces`, `std_msgs`, `std_srvs`, `example_interfaces`
- `sensor_msgs`, `geometry_msgs`, `tf2_msgs`
- `tf2_geometry_msgs`
- `rmw`, `rmw_dds_common`, `rmw_implementation`, `rmw_fastrtps_shared_cpp`, `rmw_fastrtps_cpp`
- `rcl_logging_interface`, `rcl_logging_spdlog`, `rcl_yaml_param_parser`, `rcl`
- `message_filters`, `tf2`, `tf2_ros`, `tf2_py`, `tf2_ros_py`
- `composition`
- `demo_nodes_cpp_native`
- `demo_nodes_cpp`
- `action_tutorials_interfaces`, `action_tutorials_cpp`
- `examples_rclcpp_minimal_subscriber`
- `quality_of_service_demo_cpp`
- `logging_demo`
- `image_transport`
- `point_cloud_transport`
- `rosbag2_storage`, `rosbag2_cpp`, `rosbag2_storage_sqlite3`
- `mcap_vendor`, `rosbag2_storage_mcap`, `rosbag2_examples_cpp`
- `rosbag2_interfaces`, `rosbag2_transport`, `rosbag2_py`, `rosbag2_examples_py`
- `tinyxml2_vendor`, `pluginlib`, `ros2plugin`
- `urdfdom_headers`, `urdfdom`, `urdf_parser_plugin`, `urdf`, `kdl_parser`
- `robot_state_publisher`

Required nested-repo source fixes are bundled in `ohos/patches/` for a fresh
workspace checkout.

## Colcon RK3588S/OHOS Status

`ohos/colcon_rk3588s.sh` is validated for selected package builds, not for the
entire ROS 2 workspace. Current validation covers:

- `rcutils`
- `ament_index_python`
- `ros2cli`
- `ros2pkg`
- `ament_index_cpp`
- `rcl`
- `rcl_action`
- `rcl_lifecycle`
- `rclcpp`
- `rclcpp_action`
- `rclcpp_components`
- `rclcpp_lifecycle`
- `rpyutils`
- `rclpy`
- `rosidl_generator_py`
- `action_tutorials_interfaces`
- `ros2action`
- `ros2doctor`
- `ros2interface`
- `ros2node`
- `ros2param`
- `ros2run`
- `ros2service`
- `ros2topic`
- `ros2lifecycle`
- `ros2component`
- `ros2bag`
- `ros2cli_common_extensions`
- `std_msgs`
- `example_interfaces`
- `geometry_msgs`
- `sensor_msgs`
- `nav_msgs`
- `tf2`
- `tf2_py`
- `tf2_ros`
- `osrf_pycommon`
- `launch`
- `launch_xml`
- `launch_yaml`
- `launch_ros`
- `ros2launch`
- `class_loader`
- `pluginlib`
- `composition`

The default `install/ohos-colcon-rk3588s` overlay has been rebuilt with this
45-package ROS 2 CLI/runtime-capable set, including `rpyutils` and `tf2_py`. A
no-argument `ohos/colcon_rk3588s.sh` run now uses this same package set, so
`ROS2_OHOS_COLCON_CLEAN_INSTALL=1 ./ohos/colcon_rk3588s.sh` reproduces the
validated selected overlay instead of rebuilding only the old two-package sanity
lane. Run `ohos/stage_colcon_runtime_closure.sh` after that build to copy the
runtime interface packages, underlay shared libraries, vendor libraries,
Python helper packages, and OHOS stubs needed for a self-contained deployed
CLI overlay; the resulting staged overlay has 58 ament package resources.
The `rclpy` lane is fixed by forwarding the OHOS target Python and pybind11
CMake variables into the wrapper build. Generator packages also use the
underlay target `python3.12/site-packages` on the host `PYTHONPATH`, so
migrated generator entrypoints are importable during cross builds, and
generated CMake extras are rewritten away from host `python3.11` paths. The
expanded CLI lane covers common `ros2` verbs including `action`, `interface`,
`node`, `param`, `run`, `service`, `topic`, `lifecycle`, `component`, and
`bag`, plus launch, common message packages, tf2 runtime binaries, pluginlib,
composition package resources, and the `tf2_py` extension package. A full
workspace colcon build has not been
proven; the current workspace reports 366 colcon-visible packages with
`colcon list`. The per-package CMake builder remains the validated path for the
standalone 194-entry `install/ohos-ros2` prefix.

Latest board-side overlay deployment on the currently attached RK3588 boards
used direct HDC transfer because captured HDC output can report `Connect server
failed` even when direct commands work. The 58-resource overlay, target Python
3.12 runtime, `libintl` shim, and `tf2_py/_tf2_py.cpython-312-aarch64-linux-ohos.so`
were verified under `/data/local/tmp/ohos-colcon-rk3588s` and
`/data/local/release/usr`. `ros2 pkg prefix tf2_py`, `ros2 interface show
builtin_interfaces/msg/Time`, `ros2 topic list --no-daemon`, and a
`std_msgs/msg/String` pub/echo smoke all run on board with
`RMW_IMPLEMENTATION=rmw_fastrtps_cpp`. Cross-board ROS 2 FastDDS transport was
also verified over the live `eth1` link: board B published
`hello_cross_board_fastdds` and board A received it with `ros2 topic echo
--once`.

## Build Utility Smoke

```bash
./ohos/build_ohos.sh
```

Useful overrides:

```bash
ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT=/path/to/command-line-tools
ROS2_OHOS_OPENHARMONY_ROOT=/path/to/OpenHarmony
ROS2_OHOS_ARCH=arm64-v8a
ROS2_OHOS_STL=c++_static
ROS2_OHOS_BUILD_TYPE=Release
```

## Bootstrap ROS 2 Prefix

```bash
./ohos/apply_workspace_patches.sh
./ohos/build_ros2_bootstrap.sh
./ohos/build_ros2_package.sh src/ament/ament_index/ament_index_cpp
./ohos/build_ros2_package.sh src/ros2/rmw/rmw_implementation_cmake
./ohos/build_ros2_package.sh src/ros2/rmw_implementation/rmw_implementation
./ohos/build_ros2_package.sh src/ros2/rcl_interfaces/rcl_interfaces
./ohos/build_ros2_package.sh src/ros2/common_interfaces/std_msgs
./ohos/build_ros2_package.sh src/ros2/libyaml_vendor build/ohos-ros2/libyaml_vendor_retry
./ohos/build_ros2_package.sh src/ros2/spdlog_vendor build/ohos-ros2/spdlog_vendor_retry
./ohos/build_ros2_package.sh src/ros2/rcl_logging/rcl_logging_spdlog
./ohos/build_ros2_package.sh src/ros2/rcl/rcl_yaml_param_parser
./ohos/build_ros2_package.sh src/ros2/rcl/rcl
```

## Build rcl/rmw Pub-Sub Smoke

```bash
./ohos/build_ohos.sh \
  -DROS2_OHOS_ENABLE_RMW_SMOKE=ON \
  -DROS2_OHOS_RMW_PREFIX=/home/kaihong/ros2/install/ohos-ros2
```

## HDC

Use `hdc list targets -v` first and pick the exact device ID when more than one
board is attached:

```bash
hdc list targets -v
```

The deploy scripts resolve HDC in this order:

1. `OHOS_HDC_WRAPPER` if set
2. the bundled `device-control.sh` wrapper
3. raw `hdc`

If your host `hdc` works directly, use:

```bash
OHOS_HDC_WRAPPER=hdc
OHOS_DEVICE_ID=<target-from-hdc-list>
```

Useful knobs:

```bash
OHOS_HDC_TIMEOUT_SEND=45s
OHOS_HDC_TIMEOUT_SHELL=60s
OHOS_HDC_RETRIES=3
OHOS_HDC_READY_TIMEOUT_SECONDS=30
```

Those four HDC knobs apply to both `deploy_smoke.sh` and
`deploy_fastdds_smoke.sh`.

## Deploy

```bash
OHOS_HDC_WRAPPER=hdc \
OHOS_DEVICE_ID=<target-from-hdc-list> \
./ohos/deploy_smoke.sh
```

Optional overrides:

```bash
OHOS_HDC_WRAPPER=/path/to/device-control.sh
OHOS_REMOTE_DIR=/data/local/tmp
```

To deploy the `rcl` pub-sub smoke:

```bash
runtime_libs="$(./ohos/print_pubsub_runtime_libs.sh)"

OHOS_HDC_WRAPPER=hdc \
OHOS_DEVICE_ID=<target-from-hdc-list> \
ROS2_OHOS_SMOKE_BINARY=ros2_ohos_pubsub_smoke \
ROS2_OHOS_SMOKE_ARGS= \
ROS2_OHOS_RUNTIME_LIBS="$runtime_libs" \
./ohos/deploy_smoke.sh
```

Expected output includes:

```text
smoke_ok
pid=...
executable=ros2_ohos_smoke
dummy_value=3588
```

`ros2_ohos_pubsub_smoke` prints:

```text
pubsub_smoke_ok
topic=/ros2_ohos_pubsub_smoke
payload=kaihongos pubsub smoke
```

The deploy script retries transient HDC disconnects between transfer and shell
execution, then waits for the target to report `Connected` again before running
the binary.

## Deploy Standalone ROS 2 Prefix

Push the full standalone ROS 2 prefix, including Python packages, generated type
support libraries, and wrapper scripts:

```bash
OHOS_HDC_WRAPPER=hdc \
OHOS_DEVICE_ID=<target-from-hdc-list> \
./ohos/deploy_ros2_prefix.sh
```

Useful overrides:

```bash
ROS2_OHOS_PREFIX_DIR=/home/kaihong/ros2/install/ohos-ros2
ROS2_OHOS_REMOTE_PREFIX=/data/local/tmp/ohos-prefix
ROS2_OHOS_REMOTE_PREFIX_CLEAN=1
OHOS_REMOTE_BUNDLE=/data/local/tmp/ohos-ros2-prefix.tar
OHOS_STRICT_DEVICE_READY=1
```

This avoids partial runtime syncs where newly built generated libraries such as
`libstd_msgs__rosidl_generator_py.so` are missing on the board.

If the host `hdc` link cannot reliably send the full prefix tar in one shot, use
the chunked gzip fallback:

```bash
OHOS_HDC_WRAPPER=hdc \
OHOS_DEVICE_ID=<target-from-hdc-list> \
./ohos/deploy_ros2_prefix_chunked.sh
```

Useful overrides:

```bash
OHOS_CHUNK_SIZE=4m
OHOS_CHUNK_SEND_DELAY_SECONDS=5
OHOS_REMOTE_BASE=/data/local/tmp/ohos-prefix-chunked
```

The chunked helper has completed a real board-B single-command deploy of
`install/ohos-ros2`, including reassembly and extraction. The normal
`deploy_ros2_prefix.sh` helper now uses a gzip archive, marker-based remote
success checks, byte-count verification, and expected package-count validation,
but full-prefix unchunked deploy is still not the preferred path on unstable
HDC links. On this host, HDC can still return timeout or dumped-core status
after valid stdout, so judge scripted deploy checks by their completion markers
and board-side output.

## FastDDS End-To-End

Build the standalone FastDDS stack:

```bash
./ohos/build_fastdds_stack.sh
```

This installs:

- `install/ohos-fastdds`: `foonathan_memory`, `fastcdr`, `fastrtps`
- `install/ohos-fastdds-smoke/examples/cpp/dds/BasicConfigurationExample/BasicConfigurationExample`

Run the on-device end-to-end test:

```bash
OHOS_HDC_WRAPPER=hdc \
OHOS_DEVICE_ID=<target-from-hdc-list> \
./ohos/deploy_fastdds_smoke.sh
```

The script:

1. pushes `BasicConfigurationExample`, `libfastrtps.so.2.14.6`, and `libfastcdr.so.2.2.7`
2. launches the subscriber on the board
3. runs the publisher with `--samples=1 --wait=1`
4. validates that publisher output contains `SENT` and subscriber output contains `RECEIVED`

Expected final marker:

```text
fastdds_e2e_ok
```
