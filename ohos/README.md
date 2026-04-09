# ROS 2 on KaihongOS

This subtree cross-builds a standalone ROS 2 slice for KaihongOS/OpenHarmony without `colcon`.

Scope:

- `ros2_rcutils`: direct build of `src/ros2/rcutils`
- `ros2_rcpputils`: direct build of `src/ros2/rcpputils`
- `ros2_ohos_smoke`: device-side smoke test
- `ros2_ohos_pubsub_smoke`: standalone `rcl` + `rmw_fastrtps_cpp` publish-subscribe smoke test
- `ros2_ohos_dummy`: shared library used to validate `dlopen` through `rcpputils::SharedLibrary`
- `build_ros2_bootstrap.sh`: installs the host-side `ament_*` and Python bootstrap into `install/ohos-ros2`
- `build_ros2_package.sh`: builds individual ROS 2 packages into the standalone OHOS prefix
- `build_fastdds_stack.sh`: builds `foonathan_memory`, `Fast-CDR`, `Fast-DDS`, and the upstream BasicConfiguration example as a standalone FastDDS smoke stack
- `deploy_fastdds_smoke.sh`: deploys the FastDDS example to RK3588S and runs subscriber/publisher end-to-end

The build uses both local toolchain bundles:

- `command-line-tools`: SDK sysroot, CMake toolchain files, target compiler
- `OpenHarmony/prebuilts`: host `cmake`, `ninja`, and `llvm-strip`

Current standalone prefix coverage in `install/ohos-ros2` includes:

- `ament_*`, `ament_index_*`
- `rcutils`, `rcpputils`
- `rosidl_*`
- `builtin_interfaces`, `service_msgs`, `type_description_interfaces`, `rcl_interfaces`, `std_msgs`
- `rmw`, `rmw_dds_common`, `rmw_implementation`, `rmw_fastrtps_shared_cpp`, `rmw_fastrtps_cpp`
- `rcl_logging_interface`, `rcl_logging_spdlog`, `rcl_yaml_param_parser`, `rcl`

Required nested-repo source fixes are bundled in `ohos/patches/` for a fresh
workspace checkout.

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
