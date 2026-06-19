# Focal system dependencies for the full (non-GUI) Jazzy source build

## apt (system C/C++ libs + tools)
```bash
# toolchain (see CLAUDE/commits): gcc-12, cmake (Kitware), python3.12 not used (conda)
sudo apt-get install -y \
  libssl-dev libtinyxml2-dev libtinyxml-dev libeigen3-dev libasio-dev \
  liblttng-ust-dev lttng-tools libspdlog-dev libyaml-dev libconsole-bridge-dev pybind11-dev \
  libsqlite3-dev libzstd-dev libyaml-cpp-dev liborocos-kdl-dev graphviz graphviz-dev libbullet-dev \
  libcurl4-openssl-dev libgraphicsmagick++1-dev nlohmann-json3-dev libzmq3-dev \
  libexpected-dev libcap-dev libassimp-dev liboctomap-dev libccd-dev \
  freeglut3-dev libglew-dev

# LTTng 2.13 (focal apt ships 2.11; jazzy lttngpy needs >=2.12) — official PPA:
sudo add-apt-repository -y ppa:lttng/stable-2.13
sudo apt-get update
sudo apt-get install -y liblttng-ctl-dev liblttng-ust-dev lttng-tools
```

## conda (micromamba env `jazzy-build`, python3.12) — pinned/critical versions
- eigen=3.4.0  (NOT 5.0: 5.0 sets no EIGEN3_INCLUDE_DIRS; NOT apt 3.3.7: GCC-12 bugs)
- xtensor=0.25.0 (NOT 0.26+: header layout moved); xtl, xsimd
- fmt, fcl, qhull, assimp, ompl, ceres-solver, suitesparse, metis, nanoflann,
  geographiclib-cpp, boost(python312), tbb-devel, opencv, pcl, pinocchio, ruckig(source)
- python: numpy, lxml, psutil, ntplib, semver, transforms3d, deprecated,
  filelock, rospkg, pygraphviz, jinja2, typeguard, python-orocos-kdl
