# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

This is a **ROS 2 Jazzy workspace manifest** with an embedded **KaihongOS/OpenHarmony (OHOS) cross-build subsystem**. The two concerns live in separate layers:

- **Root layer** (`ros2.repos`, `pixi.toml`): vcstool manifest that defines the Jazzy package set; `pixi.toml` handles Windows dependency environment.
- **OHOS layer** (`ohos/`): a self-contained cross-build pipeline targeting `aarch64-linux-ohos` (RK3588S boards). It bypasses ament's normal host-side bootstrapping and drives the KaihongOS SDK's cmake/ninja directly.

Source packages live in `src/` (populated by vcs; gitignored). Build and install artifacts land under `build/` and `install/` (also gitignored).

---

## Manifest / Standard Workspace Commands

```bash
# Populate src/ from ros2.repos
vcs import src < ros2.repos

# Validate manifest entries
vcs validate --input ros2.repos

# YAML formatting check (same rule as CI)
yamllint ros2.repos -d "{extends: default, rules: {document-start: {present: false}, key-ordering: {}}}"

# Windows environment (pixi.toml)
pixi install
pixi shell   # then: colcon build / colcon test
```

---

## OHOS Cross-Build Commands

All scripts live in `ohos/` and default to SDK roots at `/home/kaihong/M-DDS_4.1` (falling back to `/home/kaihong/M-DDS`). Override via environment variables.

### Prerequisites

```bash
# Apply upstream source patches (idempotent – safe to re-run)
./ohos/apply_workspace_patches.sh

# Build host-side ament bootstrap into install/ohos-ros2
./ohos/build_ros2_bootstrap.sh

# Build standalone FastDDS stack into install/ohos-fastdds
./ohos/build_fastdds_stack.sh
```

### Smoke binary build (rcutils + rcpputils)

```bash
./ohos/build_ohos.sh

# With pub/sub smoke (requires install/ohos-ros2 underlay)
./ohos/build_ohos.sh -DROS2_OHOS_ENABLE_RMW_SMOKE=ON
```

Output: `install/ohos-arm64/`

### Per-package standalone ROS 2 prefix build

```bash
# Build one package into install/ohos-ros2
./ohos/build_ros2_package.sh src/ros2/rcutils
./ohos/build_ros2_package.sh src/ros2/rcl/rcl

# Specify a custom build directory
./ohos/build_ros2_package.sh src/ros2/libyaml_vendor build/ohos-ros2/libyaml_vendor_retry
```

### Colcon RK3588S selected-package build

```bash
# Build the validated 45-package CLI lane
./ohos/colcon_rk3588s.sh

# Clean rebuild
ROS2_OHOS_COLCON_CLEAN_INSTALL=1 ./ohos/colcon_rk3588s.sh

# Build specific packages only
./ohos/colcon_rk3588s.sh rcutils rclcpp rclpy

# After colcon build, stage the runtime closure (58 ament resources)
./ohos/stage_colcon_runtime_closure.sh
```

Output: `install/ohos-colcon-rk3588s/`

### Python 3.12 dynamic extension build

```bash
./ohos/build_python312_dynload.sh
./ohos/build_ros2_python_package.sh <package-source-dir>
```

---

## Key Environment Variables

All scripts honour these overrides (shown with their defaults):

| Variable | Default | Purpose |
|---|---|---|
| `ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT` | `/home/kaihong/M-DDS_4.1/command-line-tools` | KaihongOS SDK root |
| `ROS2_OHOS_OPENHARMONY_ROOT` | `…/OpenHarmony` | OpenHarmony source tree |
| `ROS2_OHOS_ARCH` | `arm64-v8a` | Target ABI |
| `ROS2_OHOS_STL` | `c++_static` | C++ STL linkage |
| `ROS2_OHOS_BUILD_TYPE` | `Release` | CMake build type |
| `ROS2_OHOS_RMW_PREFIX` | `install/ohos-ros2` | Standalone ROS 2 prefix |
| `ROS2_OHOS_TARGET_PYTHON_VERSION` | `3.12` | Target Python version |

---

## HDC (Device Control) Commands

```bash
# List attached boards
hdc list targets -v

# Set wrapper + device for all deploy scripts
export OHOS_HDC_WRAPPER=hdc
export OHOS_DEVICE_ID=<target-from-hdc-list>
```

HDC reliability knobs: `OHOS_HDC_TIMEOUT_SEND`, `OHOS_HDC_TIMEOUT_SHELL`, `OHOS_HDC_RETRIES`, `OHOS_HDC_READY_TIMEOUT_SECONDS`.

---

## Deploy Commands

```bash
# Deploy and run rcutils/rcpputils smoke binary
./ohos/deploy_smoke.sh

# Deploy and run rcl pub/sub smoke
runtime_libs="$(./ohos/print_pubsub_runtime_libs.sh)"
ROS2_OHOS_SMOKE_BINARY=ros2_ohos_pubsub_smoke \
  ROS2_OHOS_SMOKE_ARGS= \
  ROS2_OHOS_RUNTIME_LIBS="$runtime_libs" \
  ./ohos/deploy_smoke.sh

# Deploy full standalone ROS 2 prefix (gzip archive)
./ohos/deploy_ros2_prefix.sh

# Deploy via chunked gzip (for unstable HDC links)
./ohos/deploy_ros2_prefix_chunked.sh

# Deploy interface runtime packages only
./ohos/deploy_ros2_interface_runtime.sh

# FastDDS end-to-end (subscriber + publisher with SENT/RECEIVED check)
./ohos/deploy_fastdds_smoke.sh
```

---

## Cross-Board Roundtrip Tests

```bash
./ohos/test_cross_board_fastdds.sh    # FastDDS transport between two boards
./ohos/test_cross_board_pubsub.sh     # ROS 2 pub/sub over eth1

# CLI roundtrip tool probes (run on board via HDC)
./ohos/tools/run_rclcpp_service_roundtrip.sh
./ohos/tools/run_rclcpp_action_roundtrip.sh
./ohos/tools/run_rclcpp_action_binary_roundtrip.sh
./ohos/tools/run_component_cli_roundtrip.sh
./ohos/tools/run_composition_dlopen_roundtrip.sh
./ohos/tools/run_tf2_static_echo_roundtrip.sh
./ohos/tools/run_urdf_robot_state_publisher_probe.sh
```

---

## Architecture: OHOS Build Pipeline

### Two-prefix model

- **`install/ohos-ros2`** — standalone underlay; 194 ament-index packages built individually via `build_ros2_package.sh`. This is the stable reference prefix that `colcon_rk3588s.sh` overlays on top of.
- **`install/ohos-colcon-rk3588s`** — overlay; 45 selected packages (CLI tools, rclcpp, rclpy, tf2, launch, etc.) built on top of the underlay.
- **`install/ohos-arm64`** — rcutils/rcpputils smoke binaries built via `build_ohos.sh` (the CMake project at `ohos/CMakeLists.txt`).

### CMake toolchain

`ohos/cmake/kaihongos.toolchain.cmake` — auto-discovers the SDK root from `ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT`, sets `CMAKE_SYSTEM_NAME=OHOS`, configures `clang`/`clang++` from `llvm/bin`, sets sysroot and target triple (`aarch64-linux-ohos`). Always injected via `-DCMAKE_TOOLCHAIN_FILE`.

`ohos/cmake/ensure_python_targets.cmake` — injected via `-DCMAKE_PROJECT_INCLUDE_BEFORE` to fix up Python CMake targets so ament generators find the OHOS-side Python 3.12 instead of the host Python 3.11.

### Colcon wrapper strategy in `colcon_rk3588s.sh`

Packages are built in topological order. `ament_python` packages are installed via `setup.py` directly; `ament_cmake` packages use direct CMake (bypassing colcon overhead) unless `ROS2_OHOS_COLCON_DIRECT_CMAKE=0`. After each package, `postprocess_install_tree` rewrites all pythonpath hooks and CMake extras to use `python3.12/site-packages`, then generates OHOS-compatible console script wrappers that set `LD_PRELOAD`, `PYTHONHOME`, and `LD_LIBRARY_PATH` for the board runtime.

### Source patches (`ohos/patches/`)

Seven numbered patches applied by `apply_workspace_patches.sh` (idempotent):
1. `ament_cmake` — forward `CMAKE_MAKE_PROGRAM`
2. `rcutils` — OHOS musl `strerror` fix
3. `rosidl_typesupport_fastrtps` — standalone FastDDS prefix
4. `rmw_fastrtps` — shared topic cleanup fix
5. `foonathan_memory_vendor` — forward `CMAKE_MAKE_PROGRAM`
6. `geometry2` — static transform publisher shutdown
7. `geometry2` — OHOS Python targets

### `ohos/src/` binaries

- `smoke_test.cpp` — `ros2_ohos_smoke`: validates rcpputils `SharedLibrary` / `dlopen` and reads `dummy_value` from `libros2_ohos_dummy.so`. Prints `smoke_ok`.
- `pubsub_smoke.cpp` — `ros2_ohos_pubsub_smoke`: standalone rcl + rmw_fastrtps pub/sub on `/ros2_ohos_pubsub_smoke`. Prints `pubsub_smoke_ok`.
- `libintl_shim.c` — stub `libintl.so.8` providing no-op `gettext`/`dgettext` for OHOS musl (which lacks GNU gettext).

---

## RK3588A Board Support

The pipeline supports both RK3588S (`khs_3588s_sbc`) and RK3588A (`khd_rk3588_a`). The boards share the same SDK, toolchain, and on-device Python path. The only difference is the OpenHarmony build output directory used to locate pre-built Python dependencies.

### RK3588A Build Workflow

```bash
# Set once in your shell (or prefix each command)
export OHOS_BOARD_SITE_PKG="${ROS2_OHOS_OPENHARMONY_ROOT:-/home/kaihong/M-DDS_4.1/OpenHarmony}/out/arm64/khd_rk3588_a/packages/phone/data/local/release/usr"

# 1. Bootstrap underlay (board-specific Python deps path)
ROS2_OHOS_RELEASE_SITE_PACKAGES="${OHOS_BOARD_SITE_PKG}/lib/python3.12/site-packages" \
  ./ohos/build_ros2_bootstrap.sh

# 2. Per-package underlay builds (same override)
ROS2_OHOS_RELEASE_SITE_PACKAGES="${OHOS_BOARD_SITE_PKG}/lib/python3.12/site-packages" \
  ./ohos/build_ros2_package.sh src/ros2/rcutils

# 3. FastDDS stack
ROS2_OHOS_RELEASE_USR_ROOT="${OHOS_BOARD_SITE_PKG}" \
  ./ohos/build_fastdds_stack.sh

# 4. Colcon overlay (defaults to install/ohos-colcon-rk3588a)
ROS2_OHOS_RELEASE_SITE_PACKAGES="${OHOS_BOARD_SITE_PKG}/lib/python3.12/site-packages" \
  ./ohos/colcon_rk3588a.sh

# 5. Stage runtime closure
ROS2_OHOS_COLCON_INSTALL_BASE="$(pwd)/install/ohos-colcon-rk3588a" \
  ./ohos/stage_colcon_runtime_closure.sh
```

Deploy scripts (`deploy_ros2_prefix.sh`, `deploy_smoke.sh`, etc.) are board-agnostic — use `OHOS_DEVICE_ID` and `OHOS_HDC_WRAPPER` as usual, pointing at the rk3588a device.

---

## PR / Commit Conventions

- Short imperative subject; include distro context when relevant, e.g. `[jazzy] Fix urdfdom branch`.
- `ros2.repos` changes: explain why the manifest entry changed, link the upstream issue, and mention which validation commands you ran (`vcs validate`, `yamllint`).
- OHOS script changes: note whether the change was tested on a real board and what the observed output was.
