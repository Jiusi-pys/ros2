# Agent Guide for `ros2/ros2`

This repository is the **source distribution workspace for ROS 2**. It does not contain the actual ROS 2 package source code. Instead, it provides the manifest and dependency declarations needed to fetch, build, and validate a complete ROS 2 distribution from source.

## Project Overview

- **Repository**: `https://github.com/ros2/ros2.git`
- **Default upstream branch**: `rolling`
- **Current workspace branch**: `stand` (based on the ROS 2 Jazzy Jalisco + OpenHarmony `jazzy_ohos` port)
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

### Porting workflow (no push access to upstream repos)

OHOS modifications to the `src/` subrepos may be unpublished commits and/or
uncommitted work.  `scripts/export_patches.sh` mirrors both without modifying
the real index: format-patch series record a fetchable commit base, while a
private-index binary snapshot records dirty tracked and untracked files plus
the exact base and result tree IDs.  `scripts/freeze_ros2_repos.py` then writes
`ros2.ohos.lock.repos`, pinning every repository to a fetchable commit (the
series base for repositories whose HEAD is not published):

- `./scripts/export_patches.sh` — regenerate commit series and exact dirty
  snapshots from every imported repository listed in the manifest.
- `python scripts/freeze_ros2_repos.py` — regenerate the exact, fetchable lock
  manifest after exporting patches.
- `./scripts/apply_patches.sh` — apply series and then snapshots onto a clean
  lock-manifest checkout, verifying the exported trees.

Moving to another machine:

```bash
git clone <your-fork-of-ros2/ros2> && cd ros2
vcs import --input ros2.ohos.lock.repos src/
./scripts/apply_patches.sh        # replay the OHOS port commits
# then the one-time dep scripts and ./scripts/build_ohos.sh as below
```

Deliberately syncing/rebasing with upstream ROS 2:

```bash
vcs pull src/                     # update/rebase each development port branch
./scripts/export_patches.sh       # re-export the (possibly rebased) series
python scripts/freeze_ros2_repos.py
```

This meta repository lives at `git@github.com:Jiusi-pys/ros2.git` (`origin`,
branch `stand`); `https://github.com/ros2/ros2.git` is `upstream` for
syncing. If a subrepo's port grows too large for a patch series (many
commits, heavy divergence), fork that one repo and point its `ros2.repos`
entry at your fork instead - the rest stays patch-based.

### Build

The workspace cross-builds the full ROS 2 stack (CycloneDDS and Fast-DDS RMWs,
rclcpp + rclpy + ros2cli, demos, iceoryx, LTTng tracing, Qt5/rqt/turtlesim,
rviz — 364 packages) for OpenHarmony boards using the OHOS SDK NDK clang
(musl libc) from the command-line tools package. Qt5/OGRE/qtsvg and other
non-colcon dependencies are cross-built by the scripts in `target_deps_src/`
(build_ogre_ohos.sh, build_assimp_ohos.sh, build_qtsvg_ohos.sh, pyqt/).

```bash
# Git Bash, from the repository root:
./scripts/pull_python_target.sh          # one-time: pull board CPython headers/libs
./scripts/build_target_deps.sh           # one-time: tinyxml2/console_bridge/Eigen
./scripts/build_ohos.sh                  # colcon cross build -> install_ohos/
./scripts/install_board_python_deps.sh   # one-time per board: numpy/pyyaml/psutil/...
./scripts/deploy_ohos.sh                 # pack install_ohos/ and push to both boards
./scripts/smoke_loopback.sh [board_id]   # same-board talker/listener check
./scripts/run_board_tests.sh [board_id] [pkg...]  # run package ctest suites on the board
```

Key facts:

- Toolchain: `cmake/ohos-aarch64.toolchain.cmake` (override the SDK location
  with `OHOS_NATIVE_SDK`). It sets `OHOS_CROSS_BUILD`, defines `__MUSL__`,
  restricts `CMAKE_FIND_ROOT_PATH` to the OHOS sysroot and `install_ohos/`,
  and links executables with `-Wl,--export-dynamic`. The export-dynamic flag
  is required: class_loader/pluginlib use cross-DSO `dynamic_cast` on weak
  template typeinfo, which fails unless executables export their weak symbols
  (otherwise "Could not create instance of type ...").
- `build_ohos.sh` obtains explicit repository roots from
  `scripts/manifest_source_roots.py --manifest <manifest> --source-root <src>`
  and passes those roots as colcon `--base-paths`. Discovery therefore follows
  the selected manifest rather than scanning unrelated directories under
  `src/` or `target_deps_src/`. `target_deps_src/COLCON_IGNORE` is a fallback.
- Skipped packages: Connext RMW, Rust generator, mimick_vendor
  (aarch64 trampoline asm); rviz_* ported in Phase 6f (prebuilt GLES2 OGRE,
  see the rviz/OGRE section below); all other
  Qt/rqt/turtlesim GUI packages are ported, see below), gazebo vendors,
  lint-only packages. Packages that are only `test_depend`ed
  on by in-scope packages must NOT be skipped (colcon needs their environment
  hooks): rosbag2_test_common, rosbag2_test_msgdefs, rosbag2_tests,
  ament_clang_format, ament_cmake_clang_format.
- `BUILD_TESTING=ON` and the tests run on the board:
  `scripts/run_board_tests.sh [board] [pkg...]` pushes each package's ctest
  executables + helper libs and runs them under a tmpfs `/tmp` (gtest's vendor
  copy hardcodes `/tmp` for death-test capture). Known permanent SKIP:
  rcutils/test_shared_library_in_run_paths. Test-binary detection must accept
  PIE executables (`file` reports
  them as "shared object ... interpreter /lib/ld-musl-..." - match on
  `interpreter`, not only on `executable`); the generated driver puts the
  test dir on LD_LIBRARY_PATH (helper libs like librviz_rendering_test_utils.so
  are found via DT_RPATH on the build host only) and symlinks the compiled-in
  package-source-dir macros (e.g. `_TEST_PLUGIN_DESCRIPTIONS`) at the mirrored
  `C:/...` path to the pushed test/ fixtures. rviz display tests are registered
  with `--skip-test` unless the configure ran with `-DEnableDisplayTests=True`
  (upstream logic: no DISPLAY on the Windows host); the
  `*_visual_test` screenshot comparisons need `-DEnableVisualTests=True` and
  reference images captured on the same GPU - left SKIPPED on the Mali board.
  Foreign ROS processes on the board (auto-spawned ros2 daemon, leftover rqt /
  demo nodes) hold participants on domain 0 and make graph/timing tests flaky
  or hanging. Hardened runners fail closed when they detect such processes;
  they terminate only PIDs whose run record and `/proc` start identity match.
- iceoryx is ported: musl patches under `src/eclipse-iceoryx/iceoryx/`
  (mqueue ENOSYS stubs, no-op access_control, PTHREAD_MUTEX_RECURSIVE for
  _NP, no libacl/libatomic/librt link). env.sh mounts a tmpfs over `/dev/shm`
  for POSIX shm; start RouDi with `iox-roudi &`, then use CycloneDDS with
  `CYCLONEDDS_URI=file://$ROS2_HOME/config/cyclonedds_shm.xml` for zero-copy.
- LTTng tracing is ported: userspace-rcu / lttng-ust / popt / libxml2 /
  lttng-tools are cross-built by `build_target_deps.sh` (autotools via the
  `ohos-cc`/`ohos-cxx` wrapper scripts in `target_deps_src/ohos-autotools-bin/`).
  musl has no pthread_cancel: `__MUSL__` stubs live in lttng-ust's
  `ust-cancelstate.c` and `lttng-ust-comm.c`, and the tracepoint destructors'
  `dlclose()`+`abort()` is compiled out in `include/lttng/tracepoint.h`
  (musl dlclose fails on still-referenced handles at exit; the lib stays
  mapped anyway). Re-extracting the tarballs loses these patches - `extract()`
  skips existing dirs.
- The host's WDAC policy blocks every pkg-config/pkgconf binary, so
  `cmake/FindPkgConfig.cmake` is a shadow module (passed first via
  `-DCMAKE_MODULE_PATH`) that answers lttng-ust/lttng-ctl/liburcu lookups
  from `install_ohos/` and reports everything else not-found. For autotools,
  set `*_CFLAGS`/`*_LIBS` env vars to bypass `PKG_CHECK_MODULES`.
  `MSYS2_ARG_CONV_EXCL='*'` is required for `hdc` calls but must NEVER be set
  for autotools builds (MSYS path conversion of `-I/c/...` flags breaks).
- lttng-tools bakes build-host paths into the binaries; env.sh overrides them
  with `LTTNG_SESSION_CONFIG_XSD_PATH` (without it sessiond exits 1s after
  start: XSD parse failure in the unconditional autoload validation),
  `LTTNG_CONSUMERD64_BIN`/`LTTNG_CONSUMERD64_LIBDIR` (otherwise
  `lttng enable-event` hangs spawning consumerd), and auto-starts
  `lttng-sessiond --daemonize` because the lttng CLI's auto-spawn also uses a
  baked path. A daemonized sessiond ignores SIGTERM on this musl; use
  `kill -9`. `ros2 trace` works non-interactively (`ros2 trace start/stop`);
  the interactive form needs a stdin that `hdc shell` does not forward.
- `scripts/env.sh` is a fail-closed retired legacy stub. `deploy_ohos.sh`
  generates the only usable board environment from
  `scripts/env_ohos.template.sh` inside its hash-bound staged prefix. Edit the
  template, never revive/manual-push the legacy stub. Deploy restores
  executable bits across installed `bin/` and `Lib/`, verifies every
  regular-file digest, and atomically swaps the prefix.
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
  pybind11 queries the HOST interpreter for EXT_SUFFIX. lttngpy additionally
  needs an explicit `set_target_properties(... SUFFIX)` patch (its build hits
  pybind11's classic FindPythonLibsNew path, which force-caches the host
  `.pyd` suffix as INTERNAL). Stale `*.cp312-win_amd64.pyd` duplicates in
  `install_ohos/Lib/site-packages/` are leftovers from before the flag was
  added and have been removed; only the `*-linux-ohos.so` files matter.
- Board-side Python deps (`install_board_python_deps.sh`, staging in
  `python_target/sitepkgs/`): musllinux aarch64 wheels (numpy, PyYAML) work
  after renaming the bundled `*.so` suffix to `-linux-ohos.so`; the board's
  python launcher dlopen()s libpython with RTLD_LOCAL, so `env.sh` sets
  `LD_PRELOAD=libpython3.12.so.1.0` to make Py* symbols global. psutil has no
  musllinux wheel and is compiled by hand with the NDK clang.
- Runtime on the board: first require
  `. /data/local/tmp/ros2/env.sh || exit 70` in automation (or put subsequent
  interactive commands inside `if . /data/local/tmp/ros2/env.sh; then ...; fi`), then
  `"$ROS2_TALKER_RAW"` / `"$ROS2_LISTENER_RAW"` (C++) or
  `python3.12 "$ROS2_PY_TALKER_RAW"` /
  `python3.12 "$ROS2_PY_LISTENER_RAW"`
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
- Qt5 / rqt / turtlesim are ported (Phase 6):
  - qtbase 5.15.8 cross build lives in
    `target_deps_src/qtbase-everywhere-src-5.15.8/build-ohos`. Recipe:
    Git Bash `./configure -xplatform oh-clang -platform oh-clang
    -external-hostbindir <pixi>/Library/bin` (pixi's prebuilt moc/rcc/qmake;
    the qtbase `configure` is patched to skip the qmake bootstrap) with
    `-opengl es2 -egl -eglfs -no-gbm -no-kms -no-dbus -no-glib -no-icu
    -no-openssl -no-cups -qt-*` bundled 3rdparty libs. `OSTYPE=msys` is
    MANDATORY (this Git Bash reports `cygwin` and then qmake path conversion
    asserts). The oh-clang mkspecs come from the openharmony-sig qt patch
    (apply with `patch -p1`, NOT git apply which silently skips), with local
    fixes: `OHOS_ARCH=arm64-v8a` default in oh-base-head.conf, no
    `-msoft-float` for aarch64, no `-isystem=` (with `=`) lines. musl patches
    for qsystemdetection.h/qthread_unix.cpp come from the same SIG patch.
    WDAC blocks pixi's uic.exe: it was replaced by the uic.exe 5.15.2 from the
    `qt5_applications` PyPI wheel (plus its Qt5 DLLs) in
    `.pixi/envs/default/Library/bin/` - a pixi reinstall reverts this.
  - `make install` from that build fails on two harmless items (fix manually):
    the eglfs_emu plugin qinstall chokes on the 6-level `../` relative path
    (copy with an absolute path) and `bin/qmake` was never bootstrapped.
    Qt5Core's cmake config REQUIRES `bin/qmake.exe|moc.exe|uic.exe|rcc.exe`
    to exist - copy the pixi host tools into `install_ohos/bin/`.
  - Cross qmake for downstream Qt builds:
    `target_deps_src/qt-host-tools/cross-qmake.bat` wraps pixi qmake.exe with
    `-qtconf build-ohos/bin/qt.conf` appended LAST (qmake rejects `-qtconf`
    before `-query`) and sets OHOS_SDK_PATH (the oh-clang mkspec reads the
    environment directly).
  - PyQt5 5.15.11 is cross-built by `target_deps_src/pyqt/build_pyqt5.sh`:
    sip-build 6.8.6 (PyQt5 5.15.11 requires >=6.8.6; sip 6.16's default ABI is
    rejected by the PyQt5 sip files) with `--no-make --confirm-license`
    (its license prompt needs the literal string `yes`, not `y`), then sed the
    generated Makefiles (host python include -> `python_target/usr/include/
    python3.12`, link `libpython3.12.so` because the oh-clang mkspec uses
    `-Wl,--no-undefined`), then pixi make. Qt is built `-no-openssl` and
    GLES2-only, so pass `--disabled-feature PyQt_SSL
    --disabled-feature PyQt_Desktop_OpenGL` and skip the QtOpenGL module.
    The install step also copies the sdist's `sip/` as `PyQt5/bindings` and
    `pyuic/uic` (python_qt_binding's loadUi needs `PyQt5.uic`). The PyQt5.sip
    runtime is built by `target_deps_src/pyqt/build_sip_runtime.sh` from the
    pyqt5_sip sdist (plain C, cross clang).
  - qt_gui_cpp's sip binding: `python_qt_binding/cmake/sip_configure.py` is
    host-bound end to end (imports sip4's sipconfig, probes host qmake,
    generates a host Makefile). For OHOS it is bypassed by
    `qt_gui_cpp/src/qt_gui_cpp_sip/qt_gui_cpp_sip_ohos.cmake`: a standalone
    sip4 binary (`target_deps_src/qt-host-tools/sip4`, the conda-forge win-64
    sip-4.19.25 package - conda-forge has no py312 build, but sip.exe is a
    pure C binary and works) generates the C++ at configure time and CMake
    cross-compiles it. Driven by `-DOHOS_SIP4_EXECUTABLE/-DOHOS_SIP4_INCLUDE_DIR/
    -DOHOS_PYQT5_SIP_DIR` from build_ohos.sh. Note sip4 needs the
    `py_ssize_t_clean=True` directive stripped from QtCoremod.sip (same
    workaround as upstream sip_configure.py).
  - Do not independently upgrade setuptools inside the Pixi prefix. Both
    vcstool and pytest-runner's `ptr` entry point still import
    `pkg_resources`; `pixi.toml`/`pixi.lock` retain setuptools 68.1.2 for that
    compatibility.
  - ament-index resource files written by the Windows host build have CRLF;
    pluginlib's getline does not strip `\r` and then fails to load plugin.xml
    ("has no Root Element"). `deploy_ohos.sh` strips `\r` from
    `share/ament_index/**` before packing. qt_gui_cpp additionally CACHES
    discovered plugin paths in QSettings (`$ROS2_HOME/.config/ros.org/
    rqt_gui.ini`) - a stale cache resurrects already-fixed bad paths, so
    deploy also wipes `.config`/`.cache` (the `rm -rf $DEVICE_DIR/*` glob
    misses dotfiles).
  - Board runtime: env.sh sets `QT_PLUGIN_PATH=$ROS2_HOME/plugins` (the
    prefix baked into QtCore is the Windows build path) and defaults
    `QT_QPA_PLATFORM=offscreen` (the board has no display server; eglfs/vnc/
    linuxfb plugins exist but no /dev/fb0 or connected display was found).
    DejaVu fonts live in `install_ohos/lib/fonts` (Qt without fontconfig
    needs `<prefix>/lib/fonts`). On-board Qt widgets smoke test:
    `target_deps_src/qt_smoke/` (built by build_smoke.sh).
  - `rqt` is an env.sh shell function that LD_PRELOADs
    `Lib/site-packages/qt_gui_cpp/libqt_gui_cpp_sip.so`. Required because
    libc++abi (unlike libstdc++) compares typeinfo BY POINTER in
    dynamic_cast, so class_loader's cross-DSO cast on the weak
    `AbstractMetaObject<Base>` typeinfo fails when the sip module (dlopened
    RTLD_LOCAL by CPython) and the plugin lib each bind their own copy;
    preloading puts one copy in the global scope. The same symptom is
    "Could not create instance of type ..." (cf. --export-dynamic for C++
    executables above). The rqt wrapper also ends the process with
    `os._exit(rc)` after main() returns: musl's dlclose of the Qt stack
    during CPython interpreter teardown segfaults (139) after the output is
    already complete; skipping teardown preserves the real exit code.
  - rqt plugins verified on board: `--list-plugins` discovers all rqt_*
    plugins; `rqt -s rqt_console.Console` / `rqt_topic.Topic` run under
    offscreen. turtlesim_node runs offscreen and publishes /turtle1/*.
- rviz / OGRE (Phase 6f):
  - OGRE 1.12.10 GLES2 + freetype 2.13.2 + zziplib 0.13.72 are cross-built by
    `target_deps_src/build_ogre_ohos.sh` (idempotent) straight into
    `install_ohos/`. The OHOS patch `target_deps_src/ogre-1.12.10-ohos.patch`
    adds a pbuffer-EGL GLSupport layer (`RenderSystems/GLSupport/*/EGL/OHOS/`),
    makes X11 non-required and adds an `OGRE_EXTRA_MODULE_PATH` hook (apply
    with GNU `patch`, NOT git apply which silently skips hunks). Config:
    `OGRE_BUILD_RENDERSYSTEM_GLES2=ON`, `OGRE_GLSUPPORT_USE_EGL=ON`, no X11;
    zlib comes from the sysroot. `share/OGRE/plugins.cfg` PluginFolder is
    rewritten to the on-device `/data/local/tmp/ros2/lib/OGRE`.
    assimp 5.3.1 likewise: `target_deps_src/build_assimp_ohos.sh`.
  - NB: those scripts' PATH export must use the POSIX form (`$(pwd)`), a
    Windows-style `C:/...` PATH entry is listed by Git Bash but silently
    never searched ("cmake: command not found" even though cmake.exe exists).
  - rviz_ogre_vendor / rviz_assimp_vendor skip their `ament_vendor`
    ExternalProject entirely under `if(NOT OHOS_CROSS_BUILD)` (build_ohos.sh
    passes `-DFORCE_BUILD_VENDOR_PKG=ON`, which overrides SATISFIED) and their
    extras point OGRE_DIR/assimp_DIR at the prebuilt prefix install; the
    extras' `find_package(OpenGL REQUIRED)` / `find_package(X11 REQUIRED)`
    are replaced by direct EGL/GLESv2 find_library on OHOS. build_ohos.sh now
    passes `-DCMAKE_LIBRARY_ARCHITECTURE=aarch64-linux-ohos` so find_library
    searches `lib/<triple>` in the sysroot (needed for libEGL/libGLESv2).
  - If pixi's `vcs` fails with `ModuleNotFoundError: pkg_resources`, the local
    prefix has drifted from the checked-in lock (typically via a pip setuptools
    upgrade). Restore the locked environment/setuptools 68.1.2 before import;
    do not paper over it with an unrecorded release dependency.
  - rviz_rendering OHOS patches: X11/GLX dummy-context code is `#if __linux__
    && !defined(__OHOS__)`, `loadOgrePlugins` loads `RenderSystem_GLES2`,
    `setPluginDirectory` uses `<prefix>/lib/OGRE` (no opt/ subdir),
    `detectGlVersion` claims GLSL 120 on GLES2 so scripts120 materials load.
  - The glsl120 shaders use desktop-only built-ins (gl_Vertex/gl_Color/
    gl_TexCoord/gl_FrontColor/ftransform/sampler1D/float[] ctors); ported
    GLSL ES 1.00 variants live in `ogre_media/materials/glsles/` with
    UNCHANGED program names (rviz/glsl120/...) so the .material scripts are
    shared (Ogre binds the fixed attribute names vertex/colour/uv0;
    fragment shaders declare `precision highp float` - needed for the depth
    packing math). Ogre 1.12's GLSLES script translator does NOT support
    `attach`, so every shared include file is inlined into each shader.
    setupResources() loads glsles/ (and glsles/nogp/) instead of glsl120/
    on OHOS.
  - rviz_rendering integrates Ogre's RTSS for the fixed-function materials:
    a local `RVizRTSSListener` (replica of Bites' SGTechniqueResolverListener)
    plus `ShaderGenerator::initialize()` AFTER the first dummy render window
    is created (the GLES2 RS only creates GpuProgramManager in
    initialiseFromRenderSystemCapabilities; initializing earlier segfaults),
    and the RTSS shader generator's `#include <OgreUnifiedShader.h>` needs
    `<prefix>/share/OGRE/Media/RTShaderLib/{materials,GLSL}` and
    `Media/ShadowVolume` registered in the INTERNAL_RESOURCE_GROUP.
  - Headless swap: `OHOSEGLWindow::swapBuffers()` is a no-op for pbuffer
    surfaces (eglSwapBuffers is a no-op per spec and the Mali driver returns
    EGL_FALSE, which the base EGLWindow turns into "Fail to SwapBuffers");
    it only swaps externally-created surfaces. Patch regen workflow after
    editing the ogre tree: `git -C target_deps_src/ogre-1.12.10 add -N
    RenderSystems/GLSupport/*/EGL/OHOS` (intent-to-add, else git diff drops
    the new files), then `git -C ... diff > ../ogre-1.12.10-ohos.patch` and
    re-run build_ogre_ohos.sh (incremental).
  - rviz2 main.cpp needs no patch: its xcb forcing only triggers when
    XDG_SESSION_TYPE=wayland and env.sh already sets QT_QPA_PLATFORM=offscreen.
  - Board smoke (offscreen, timeout 40 rviz2 -> RC=124 means it survived):
    /rviz + /transform_listener_impl_* appear in `ros2 node list`, topics
    flow (verified with turtlesim /turtle1/pose), zero Ogre exceptions.
    SVG cursors/icons work: qtsvg 5.15.8 is cross-built by
    `target_deps_src/build_qtsvg_ohos.sh` (cross-qmake; export
    OHOS_SDK_PATH in the environment so the recursive qmake invocations
    spawned by make see it - cross-qmake.bat's `set` only covers its own
    process), installing libQt5Svg + plugins/imageformats/libqsvg.
- Several upstream sources carry small OHOS-marked patches (musl fixes in
  rcutils / osrf_testing_tools_cpp / rttest / cbg_executor, vendor-package
  extras for libcurl/yaml-cpp, `colcon.pkg` BUILD_IDLC=OFF in cyclonedds,
  dependency unrolls in package.xml files, export-order fix in
  rosbag2_storage, mqueue/acl/mutex musl stubs in iceoryx, LTTng include-dir
  and libatomic guards in ros2_tracing, the lttngpy extension-suffix patch,
  urdfdom built WITHOUT -fvisibility=hidden so the header-only urdfdom_headers
  classes keep one global weak typeinfo across DSOs - libc++abi compares
  typeinfo by pointer, otherwise dynamic_cast<const urdf::Sphere&> in
  rviz_default_plugins' robot_link.cpp throws std::bad_cast);
  `git -C src/<repo> diff` shows them.
- `hdc` caveats (Windows host): pass local paths via `cygpath -w`, set
  `MSYS2_ARG_CONV_EXCL='*'` so Git Bash does not rewrite remote `/data/...`
  paths, never rely on `hdc shell` exit codes or stdin - verify results
  explicitly on the device, and never `pkill -f` a pattern that appears in
  the invoking `hdc shell` command line itself. For native crashes on the
  board, OHOS faultlogger writes fully symbolized stacks to
  `/data/log/faultlog/faultlogger/cppcrash-<mod>-*.log` - always check there
  first.

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
