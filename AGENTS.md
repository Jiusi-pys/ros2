# Agent Guide for `ros2/ros2`

This repository is the **source distribution workspace for ROS 2**. It does not contain the actual ROS 2 package source code. Instead, it provides the manifest and dependency declarations needed to fetch, build, and validate a complete ROS 2 distribution from source.

## Project Overview

- **Repository**: `https://github.com/ros2/ros2.git`
- **Default upstream branch**: `rolling`
- **Current workspace branch**: `jazzy` (ROS 2 Jazzy Jalisco)
- **Platform focus**: Cross-platform, with the pinned dependency workspace (`pixi.toml`) targeting Windows (`win-64`)
- **Language mix**: C++, Python, and small amounts of CMake, interface definition, and Rust tooling
- **Build system**: CMake packages are built with [colcon](https://colcon.readthedocs.io/); package metadata is handled by [ament](https://github.com/ament)
- **Dependency manager**: [Pixi](https://pixi.sh/) via `pixi.toml`; on Windows this is the supported way to obtain compilers, libraries, Python, and build tools
- **Source manifest**: `ros2.repos` is consumed by the [vcstool](https://github.com/dirk-thomas/vcstool) `vcs import` command to populate `src/`

## Repository Layout

```text
.
├── .github/
│   ├── ISSUE_TEMPLATE.md            # Issue template used for the upstream repo
│   └── workflows/
│       ├── mirror-rolling-to-master.yaml  # Mirrors `rolling` push to `master`
│       └── pr.yaml                  # Validates ros2.repos formatting and repository entries
├── src/
│   └── .gitkeep                     # Placeholder; actual packages are imported here by vcs
├── .gitignore                       # Ignores colcon output and everything under src/ except .gitkeep
├── pixi.toml                        # Pixi workspace with pinned build/runtime dependencies
├── README.md                        # Upstream ROS project landing page
└── ros2.repos                       # vcs-tool manifest listing all core ROS 2 repositories
```

## Technology Stack

- **DDS middleware**: eProsima Fast-DDS (`rmw_fastrtps`), Eclipse Cyclone DDS (`rmw_cyclonedds`), RTI Connext DDS (`rmw_connextdds`)
- **Core client libraries**: `rclcpp` (C++), `rclpy` (Python)
- **Build tooling**: `colcon-core`, `colcon-cmake`, `colcon-ros`, `colcon-python-setup-py`, `colcon-test-result`
- **Lint/format tooling** (used by ament packages): `uncrustify`, `flake8` + plugins, `yamllint`, `mypy`, `pydocstyle`, `pytest-cov`
- **Testing**: `pytest`, `pytest-repeat`, `pytest-rerunfailures`, `pytest-timeout`, `pytest-mock`, GoogleTest, `benchmark`
- **Python**: `3.12.3` (pinned in `pixi.toml`)
- **CMake**: `3.28.3` (pinned)

## Build and Test Commands

### 1. Fetch the source code

```bash
# Install vcstool if it is not already available (it is declared in pixi.toml)
python3 -m pip install vcstool

# Import all repositories listed in ros2.repos into src/
vcs import --input ros2.repos src/

# (Optional) Update already-imported repositories
vcs pull src/
```

### 2. Install dependencies (Windows with Pixi)

```bash
# Install pixi from https://pixi.sh/ if needed, then:
pixi install

# Activate the environment
pixi shell
```

Inside the activated shell the `QT_QPA_PLATFORM_PLUGIN_PATH` environment variable is already set to the local Qt platform plugin directory.

### 3. Build the workspace

```bash
# From the repository root, after src/ is populated
colcon build --merge-install
```

Common useful options:

```bash
# Build only packages you changed
colcon build --merge-install --packages-up-to <package-name>

# Build with debug symbols
colcon build --merge-install --cmake-args -DCMAKE_BUILD_TYPE=Debug

# Continue building even if one package fails
colcon build --merge-install --continue-on-error
```

### 4. Source the environment

```bash
# Bash (Git Bash, WSL, Linux)
source install/setup.bash

# Windows PowerShell
.\install\setup.ps1

# Windows cmd
install\setup.bat
```

### 5. Run tests

```bash
# Build and run tests for all packages
colcon test --merge-install

# Run tests and show the summary
colcon test --merge-install && colcon test-result --verbose

# Run tests for a single package
colcon test --merge-install --packages-select <package-name>
```

### 6. Validate the repository manifest (what CI does)

```bash
# Validate YAML formatting
python3 -m pip install yamllint
yamllint ros2.repos \
  -f github \
  -d "{extends: default, rules: {document-start: {present: false}, key-ordering: {}}}"

# Validate that every repository entry is reachable
python3 -m pip install vcstool
vcs validate --input ros2.repos
```

## Code Style Guidelines

This meta repository itself contains very little code. Style expectations apply to the downstream ROS 2 packages imported into `src/`:

- **C++**: Format with `uncrustify` using the `ament_uncrustify` configuration. The pinned version in `pixi.toml` is `0.78.1`.
- **Python**: Lint with `flake8` plus the plugins listed in `pixi.toml` (`flake8-blind-except`, `flake8-builtins`, `flake8-class-newline`, `flake8-comprehensions`, `flake8-deprecated`, `flake8-docstrings`, `flake8-import-order`, `flake8-quotes`).
- **YAML**: Keep `ros2.repos` valid YAML. CI enforces no `---` document-start marker and does not require key ordering.
- **Repository entries**: Each entry in `ros2.repos` must have `type`, `url`, and `version` keys. Branches are typically named after the ROS distribution (`jazzy`, `rolling`, etc.) or a pinned upstream branch/version.

When modifying `pixi.toml`:

- Keep tooling dependencies (the `colcon-*` packages) pinned loosely with `>=x,<y`.
- Keep library dependencies that affect ABI/API pinned to exact versions so builds are reproducible.
- Add a comment when a pin differs from the Ubuntu counterpart because of conda-forge availability.

## Testing Instructions

- **For this repository**: The only automated tests are the GitHub Actions in `.github/workflows/pr.yaml`. Open a pull request to run them.
- **For the full ROS 2 workspace**:
  1. Run `vcs import --input ros2.repos src/`.
  2. Run `colcon build --merge-install`.
  3. Run `colcon test --merge-install`.
  4. Inspect results with `colcon test-result --verbose`.

If a downstream test fails, use `colcon test --packages-select <package> --event-handlers console_direct+` to see live output.

## Cross-compiling for OpenHarmony (RK3588 / KaihongOS, aarch64)

The workspace can cross-build the ROS 2 core C++ stack (both CycloneDDS and
Fast-DDS RMWs) plus `demo_nodes_cpp` for OpenHarmony boards using the OHOS SDK
NDK clang (`aarch64-linux-ohos`, musl libc). Prerequisites: `pixi install`
done, OHOS command-line tools unpacked (NDK under
`<cmdline-tools>/sdk/default/openharmony/native`).

```bash
# Git Bash, from the repository root:
./scripts/build_ohos.sh     # colcon cross build -> install_ohos/
./scripts/deploy_ohos.sh    # push to both boards (/data/local/tmp/ros2)
./scripts/smoke_loopback.sh [board_id]   # same-board talker/listener check
./scripts/run_bidirectional_test.sh 20   # board A <-> board B, both directions
```

- Toolchain: `cmake/ohos-aarch64.toolchain.cmake`. Override the NDK path with
  `OHOS_NATIVE_SDK=<.../native>`. It defines `OHOS_CROSS_BUILD=TRUE` for
  packages needing OHOS/musl workarounds, and pins `CMAKE_FIND_ROOT_PATH` to
  the sysroot + `install_ohos` so host conda/pixi libraries are never linked.
- The build skips: Connext RMW, iceoryx, rclpy and Python/Rust generators,
  launch_ros, mimick_vendor (aarch64 trampoline asm does not assemble with the
  NDK). Several upstream `package.xml` files carry small OHOS-marked patches
  (bloom-unrolled deps to skipped packages, musl fixes in rcutils and
  osrf_testing_tools_cpp, `colcon.pkg` BUILD_IDLC=OFF in cyclonedds);
  `git -C src/<repo> diff` shows them.
- Runtime on the board: `. /data/local/tmp/ros2/env.sh`, then
  `$ROS2_TALKER` / `$ROS2_LISTENER`. `RMW_IMPLEMENTATION` defaults to
  `rmw_cyclonedds_cpp`; set it to `rmw_fastrtps_cpp` to use Fast-DDS
  (Fast-DDS logs SHM warnings because OHOS lacks /dev/shm; harmless, it falls
  back to UDP). Board-to-board discovery works over multicast on eth1
  (192.168.77.0/24); if multicast is ever blocked, deploy
  `config/cyclonedds_board_[ab].xml` and set `CYCLONEDDS_URI` accordingly.
- `hdc` caveats (Windows host): pass local paths via `cygpath -w`, set
  `MSYS2_ARG_CONV_EXCL='*'` so Git Bash does not rewrite remote `/data/...`
  paths, and never rely on `hdc shell` exit codes or stdin - verify results
  explicitly on the device.

## Continuous Integration

- `.github/workflows/pr.yaml` runs on every pull request.
  - Installs `vcs2l` and `yamllint`.
  - Lints `ros2.repos` with `yamllint`.
  - Validates all repository URLs with `vcs validate --input ros2.repos`.
- `.github/workflows/mirror-rolling-to-master.yaml` runs on every push to `rolling` and mirrors the branch to `master`.

There is no per-package build/test CI inside this repository; that is handled by [ci.ros2.org](https://ci.ros2.org).

## Security Considerations

- This repository references external Git repositories in `ros2.repos`. Changing a URL or branch can redirect the build to arbitrary source code. Review every change to `ros2.repos` carefully.
- The `pixi.toml` pins package versions from `conda-forge`. Do not loosen pins without checking that the resulting packages match the versions used by the upstream Ubuntu/Debian builds.
- Credentials or local environment files are not committed. `.gitignore` already excludes `build/`, `install/`, `log/`, and the contents of `src/`.
- Do not run `colcon` or build steps with elevated privileges; the build is user-local by design.

## Common Tasks

### Add a new ROS 2 repository to the workspace

Edit `ros2.repos` and add an entry under `repositories:`:

```yaml
  org_name/repo_name:
    type: git
    url: https://github.com/org_name/repo_name.git
    version: jazzy
```

Then run `vcs validate --input ros2.repos` before committing.

### Update a pinned dependency

Edit `pixi.toml`, change the version constraint, and run `pixi install` to verify the environment still resolves. If a newer version is forced by conda-forge constraints, document the reason in a comment.

### Switch ROS distributions

Check out the corresponding branch (`jazzy`, `rolling`, `humble`, etc.) and use that branch's `ros2.repos`. Do not mix distribution branches in the same workspace.

## Cross-compiling for OpenHarmony (RK3588, aarch64-linux-ohos)

The workspace can cross-build the ROS 2 core C++ stack (CycloneDDS and Fast-DDS
RMWs) plus `demo_nodes_cpp` for OpenHarmony boards, using the OHOS SDK NDK
clang from the command-line tools package.

```bash
./scripts/build_ohos.sh      # colcon cross build into build_ohos/ + install_ohos/
./scripts/deploy_ohos.sh     # pack install_ohos/ and push to both boards
./scripts/smoke_loopback.sh  # same-board talker/listener smoke test
./scripts/run_bidirectional_test.sh  # board-to-board talker/listener, both directions
```

Key facts:

- Toolchain: `cmake/ohos-aarch64.toolchain.cmake` (override the SDK location
  with `-DOHOS_NATIVE_SDK=` / `OHOS_NATIVE_SDK`). It sets `OHOS_CROSS_BUILD`,
  defines `__MUSL__`, restricts `CMAKE_FIND_ROOT_PATH` to the OHOS sysroot and
  `install_ohos/`, and links executables with `-Wl,--export-dynamic`.
  The export-dynamic flag is required: class_loader/pluginlib use cross-DSO
  `dynamic_cast` on weak template typeinfo, which fails unless executables
  export their weak symbols (otherwise "Could not create instance of type ...").
- Fast-DDS (2.14.x) is built with `-DTHIRDPARTY=ON` (bundled asio/tinyxml2 from
  git submodules - run `git submodule update --init thirdparty/asio
  thirdparty/tinyxml2` in `src/eProsima/Fast-DDS` after `vcs import`) and
  `-DENABLE_SSL=NO`. `foonathan_memory_vendor` propagates the toolchain file to
  its ExternalProject.
- The RMW used on the boards is chosen at deploy time:
  `RMW=rmw_fastrtps_cpp ./scripts/deploy_ohos.sh` (default `rmw_cyclonedds_cpp`).
  `rmw_implementation` bakes the default RMW in at build time, so after adding
  an RMW implementation, delete `build_ohos/rmw_implementation` (and the
  interface packages' build dirs so they generate the new typesupport) and
  rebuild.
- On the board: Windows host FS is case-insensitive, so the tar only contains
  `Lib/`; the deploy script creates a `lib -> Lib` symlink for ament-index
  plugin paths. `libc++_shared.so` from the NDK is shipped alongside.
  `FASTDDS_BUILTIN_TRANSPORTS=UDPv4` is set because OHOS has no `/dev/shm`.
- If multicast discovery between boards ever fails, fallback configs exist in
  `config/` (`cyclonedds_board_*.xml` via `CYCLONEDDS_URI`,
  `fastdds_board_*.xml` via `FASTRTPS_DEFAULT_PROFILES_FILE`).

## Useful References

- [ROS 2 Documentation](https://docs.ros.org/)
- [ROS 2 Installation from Source](https://docs.ros.org/en/jazzy/Installation/Alternatives/Windows-Development-Setup.html) (Windows via Pixi)
- [colcon documentation](https://colcon.readthedocs.io/)
- [vcstool documentation](https://github.com/dirk-thomas/vcstool)
- [Pixi documentation](https://pixi.sh/)
- [REP-2000](https://ros.org/reps/rep-2000.html): ROS 2 Releases and Target Platforms
