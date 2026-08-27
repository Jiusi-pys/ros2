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

## Cross-compiling for OpenHarmony (RK3588A / KaihongOS, aarch64-linux-ohos)

The workspace cross-builds the full ROS 2 stack (CycloneDDS and Fast-DDS RMWs,
rclcpp + rclpy + ros2cli, demos — 275 packages) for OpenHarmony boards using
the OHOS SDK NDK clang (musl libc) from the command-line tools package.

```bash
# Git Bash, from the repository root:
./scripts/pull_python_target.sh          # one-time: pull board CPython headers/libs
./scripts/build_target_deps.sh           # one-time: tinyxml2/console_bridge/Eigen
./scripts/build_ohos.sh                  # colcon cross build -> install_ohos/
./scripts/install_board_python_deps.sh   # one-time per board: numpy/pyyaml/psutil/...
./scripts/deploy_ohos.sh                 # pack install_ohos/ and push to both boards
./scripts/smoke_loopback.sh [board_id]   # same-board talker/listener check
./scripts/run_bidirectional_test.sh 20   # board A <-> board B, both directions
```

Key facts:

- Toolchain: `cmake/ohos-aarch64.toolchain.cmake` (override the SDK location
  with `OHOS_NATIVE_SDK`). It sets `OHOS_CROSS_BUILD`, defines `__MUSL__`,
  restricts `CMAKE_FIND_ROOT_PATH` to the OHOS sysroot and `install_ohos/`,
  and links executables with `-Wl,--export-dynamic`. The export-dynamic flag
  is required: class_loader/pluginlib use cross-DSO `dynamic_cast` on weak
  template typeinfo, which fails unless executables export their weak symbols
  (otherwise "Could not create instance of type ...").
- `build_ohos.sh` passes `--base-paths src` to colcon: the default scan root
  is the workspace root, which would otherwise pick up `target_deps_src/*` as
  plain cmake packages and install a non-PIC static tinyxml2 that breaks
  rosbag2_storage/urdfdom. `target_deps_src/COLCON_IGNORE` is a fallback.
- Skipped packages: Connext RMW, iceoryx, Rust generator, mimick_vendor
  (aarch64 trampoline asm), all Qt/rqt/rviz GUI, gazebo vendors, sros2 (needs
  Rust cryptography), tracetools Python parts, tf2_bullet, OpenCV demos, lint
  and test-only packages. Packages that are only `test_depend`ed on by
  in-scope packages must NOT be skipped (colcon needs their environment
  hooks): rosbag2_test_common, rosbag2_test_msgdefs, rosbag2_tests,
  ament_clang_format, ament_cmake_clang_format.
- Fast-DDS (2.14.x) is built with `-DTHIRDPARTY=ON` (bundled asio/tinyxml2 from
  git submodules - run `git submodule update --init thirdparty/asio
  thirdparty/tinyxml2` in `src/eProsima/Fast-DDS` after `vcs import`) and
  `-DENABLE_SSL=NO`. `foonathan_memory_vendor` propagates the toolchain file to
  its ExternalProject.
- The RMW is selected at runtime via `RMW_IMPLEMENTATION`: the build uses
  `-DRMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=OFF`, so both
  `rmw_cyclonedds_cpp` and `rmw_fastrtps_cpp` are always available.
  `deploy_ohos.sh` does NOT pin the RMW in `env.sh` by default; use
  `RMW=rmw_cyclonedds_cpp ./scripts/deploy_ohos.sh` to pin one. Never let
  rmw_implementation's CMake cache decide this on its own: when only one RMW
  is present at configure time the option defaults ON and bakes that RMW into
  every binary - if in doubt, delete `build_ohos/rmw_implementation` and
  rebuild.
- Python stack: the boards run the CPython 3.12 port from
  https://github.com/Jiusi-pys/python at `/data/python312-rk3588a`.
  `pull_python_target.sh` copies its headers/`libpython3.12.so` into
  `python_target/usr/` as the link-time sysroot; `build_ohos.sh` points
  `Python3_INCLUDE_DIR`/`Python3_LIBRARY`/`Python3_SOABI` there while keeping
  `Python3_EXECUTABLE` = host pixi python, and sets
  `PYTHON_MODULE_EXTENSION=.cpython-312-aarch64-linux-ohos.so` because
  pybind11 queries the HOST interpreter for EXT_SUFFIX.
- Board-side Python deps (`install_board_python_deps.sh`, staging in
  `python_target/sitepkgs/`): musllinux aarch64 wheels (numpy, PyYAML) work
  after renaming the bundled `*.so` suffix to `-linux-ohos.so`; the board's
  python launcher dlopen()s libpython with RTLD_LOCAL, so `env.sh` sets
  `LD_PRELOAD=libpython3.12.so.1.0` to make Py* symbols global. psutil has no
  musllinux wheel and is compiled by hand with the NDK clang.
- Runtime on the board: `. /data/local/tmp/ros2/env.sh`, then `$ROS2_TALKER` /
  `$ROS2_LISTENER` (C++) or `$ROS2_PY_TALKER` / `$ROS2_PY_LISTENER`
  (demo_nodes_py; colcon on a Windows host writes setuptools `*-script.py`
  entry scripts into `lib/<pkg>/`, the `.exe` launchers are unusable). `ros2`
  is an env.sh shell function wrapping `ros2cli.cli:main`. Board-to-board
  discovery works over multicast on eth1 (192.168.77.0/24); if multicast is
  ever blocked, fallback configs exist in `config/` (`cyclonedds_board_*.xml`
  via `CYCLONEDDS_URI`, `fastdds_board_*.xml` via
  `FASTRTPS_DEFAULT_PROFILES_FILE`).
- On the board: Windows host FS is case-insensitive, so the tar only contains
  `Lib/`; the deploy script creates a `lib -> Lib` symlink for ament-index
  plugin paths. `libc++_shared.so` from the NDK is shipped alongside.
  `FASTDDS_BUILTIN_TRANSPORTS=UDPv4` is set because OHOS has no `/dev/shm`
  (Fast-DDS then logs harmless SHM warnings and falls back to UDP).
- Several upstream sources carry small OHOS-marked patches (musl fixes in
  rcutils / osrf_testing_tools_cpp / rttest / cbg_executor, vendor-package
  extras for libcurl/yaml-cpp, `colcon.pkg` BUILD_IDLC=OFF in cyclonedds,
  dependency unrolls in package.xml files, export-order fix in
  rosbag2_storage); `git -C src/<repo> diff` shows them.
- `hdc` caveats (Windows host): pass local paths via `cygpath -w`, set
  `MSYS2_ARG_CONV_EXCL='*'` so Git Bash does not rewrite remote `/data/...`
  paths, never rely on `hdc shell` exit codes or stdin - verify results
  explicitly on the device, and never `pkill -f` a pattern that appears in
  the invoking `hdc shell` command line itself.

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

## Useful References

- [ROS 2 Documentation](https://docs.ros.org/)
- [ROS 2 Installation from Source](https://docs.ros.org/en/jazzy/Installation/Alternatives/Windows-Development-Setup.html) (Windows via Pixi)
- [colcon documentation](https://colcon.readthedocs.io/)
- [vcstool documentation](https://github.com/dirk-thomas/vcstool)
- [Pixi documentation](https://pixi.sh/)
- [REP-2000](https://ros.org/reps/rep-2000.html): ROS 2 Releases and Target Platforms
