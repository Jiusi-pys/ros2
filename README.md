# ROS 2 for OpenHarmony (RK3588A)

This fork ports the full ROS 2 Jazzy Jalisco stack to OpenHarmony boards
(aarch64-linux-ohos, musl libc) — tested on RK3588A / KaihongOS. Everything
except the Connext RMW works on the board: CycloneDDS and Fast-DDS RMWs,
rclcpp/rclpy, ros2cli, rosbag2, tf2, iceoryx zero-copy, LTTng tracing, and
the Qt5 GUI stack (rqt, turtlesim, rviz with GLES2 OGRE). 364 packages
cross-build cleanly; the package ctest suites run on the board.

The OHOS work lives on the `jazzy_ohos` branch.

## Quick start

Prerequisites: Windows host with Git Bash, [Pixi](https://pixi.sh/), the
OpenHarmony command-line-tools SDK (NDK), and `hdc` access to the board(s).

```bash
git clone -b jazzy_ohos git@github.com:Jiusi-pys/ros2.git
cd ros2
pixi install && pixi shell

# fetch the upstream ROS 2 sources, then replay the OHOS port patches
vcs import --input ros2.repos src/
./scripts/apply_patches.sh

# one-time target dependencies (CPython sysroot, tinyxml2, OGRE, Qt, ...)
./scripts/pull_python_target.sh
./scripts/build_target_deps.sh
./target_deps_src/build_ogre_ohos.sh
./target_deps_src/build_assimp_ohos.sh
./target_deps_src/build_qtsvg_ohos.sh
# Qt5 / PyQt5 cross builds: see target_deps_src/pyqt/ and AGENTS.md

# cross-build everything, then deploy to the board(s) over hdc
./scripts/build_ohos.sh
./scripts/install_board_python_deps.sh
./scripts/deploy_ohos.sh

# verify
./scripts/smoke_loopback.sh          # same-board talker/listener
./scripts/run_bidirectional_test.sh  # board A <-> board B
./scripts/run_board_tests.sh         # ctest suites on the board
```

On the board: `. /data/local/tmp/ros2/env.sh`, then `ros2`, `rqt`, `rviz2`,
`$ROS2_TALKER`/`$ROS2_LISTENER`, `turtlesim_node`, ...

See [AGENTS.md](AGENTS.md) for the full porting details (toolchain, musl
quirks, Qt/OGRE recipes, board-test infrastructure, debugging tips).

## How the port is maintained (no upstream push access)

The `src/` subrepos are read-only upstream clones. Every OHOS modification
is kept as local commits in the subrepo **and** as an exported patch series
in `patches/` (one `.patch` + `.base` per repo):

- `./scripts/export_patches.sh` — re-export `patches/` after committing or
  amending anything in a subrepo (commit the result here).
- `./scripts/apply_patches.sh` — replay `patches/` onto a fresh
  `vcs import` checkout; idempotent, 3-way apply.

Syncing with upstream ROS 2:

```bash
vcs pull src/                  # fetch upstream updates
./scripts/apply_patches.sh     # re-apply the port (resolve 3-way conflicts)
./scripts/export_patches.sh    # re-export and commit the updated series
```

# About 
The Robot Operating System (ROS) is a set of software libraries and tools that help you build robot applications.
From drivers to state-of-the-art algorithms, and with powerful developer tools, ROS has what you need for your next robotics project.
And it's all open source.
Full project details on [ROS.org](https://ros.org/)

# Getting Started 
Looking to get started with ROS?
Our [installation guide is here](https://www.ros.org/blog/getting-started/).
Once you've installed ROS start by learning some [basic concepts](https://docs.ros.org/en/rolling/Concepts/Basic.html) and take a look at our [beginner tutorials](https://docs.ros.org/en/rolling/Tutorials/Beginner-CLI-Tools.html).

# Join the ROS Community

## Community Resources

* [ROS Discussion Forum](https://discourse.ros.org/)
* [ROS Zulip Server](https://openrobotics.zulipchat.com/)
* [Robotics Stack Exchange](https://robotics.stackexchange.com/) (preferred ROS support forum).
* [Official ROS Videos](https://vimeo.com/osrfoundation)
* [ROSCon](https://roscon.ros.org), our yearly developer conference. 
* Cite ROS 2 in academic work using [DOI: 10.1126/scirobotics.abm6074](https://www.science.org/doi/10.1126/scirobotics.abm6074) 

## Developer Resources
* [ROS 2 Documentation](https://docs.ros.org/)
* [ROS Package API reference](https://docs.ros.org/en/rolling/p/)
* [ROS Package Index](https://index.ros.org/)
* [ROS on Docker Hub](https://hub.docker.com/_/ros/)
* [ROS Resource Status Page](https://status.openrobotics.org/)
* [REP-2000](https://ros.org/reps/rep-2000.html): ROS 2 Releases and Target Platforms

## Project Resources
* [Purchase ROS Swag](https://spring.ros.org/)
* [Information about the ROS Trademark](https://www.ros.org/blog/media/)
* On Social Media
  * [Open Robotics on LinkedIn](https://www.linkedin.com/company/open-source-robotics-foundation)
  * [Open Robotics on Twitter](https://twitter.com/OpenRoboticsOrg)
  * [ROS.org on Twitter](https://twitter.com/ROSOrg)

ROS is made possible through the generous support of open source contributors and the non-profit [Open Source Robotics Foundation (OSRF)](https://www.openrobotics.org/).
Tax deductible donations to the OSRF can be [made here.](https://donorbox.org/support-open-robotics?utm_medium=qrcode&utm_source=qrcode)
