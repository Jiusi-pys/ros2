# ROS 2 迁移到 KaihongOS / RK3588A 完整教程（FastDDS + CycloneDDS）

> 本教程是把 **ROS 2 (Jazzy)** 交叉编译、部署并验证到 **KaihongOS / OpenHarmony 6.x** 的
> **RK3588A**（`aarch64-linux-ohos`，**musl** libc）开发板的**单一权威入口**。它从零讲清"为什么这样做"
> 与"每一步怎么做"，覆盖两种 DDS 中间件（FastDDS 与 CycloneDDS）、单板全功能验证、两板跨板传输与
> 跨厂商互通。全部步骤来自本仓库可运行脚本，已在两块 RK3588A 上实测：**单板 130/130、两板全功能矩阵
> 99 PASS / 0 FAIL / 1 NA**。

## 0. 文档地图

| 文档 | 用途 |
|---|---|
| **本文（OHOS_MIGRATION_TUTORIAL_zh.md）** | 端到端总教程，新手从这里开始 |
| `ohos/MIGRATION_GUIDE_zh.md` | 精炼复用手册（命令速查 + §9 重拉记录） |
| `ohos/CYCLONEDDS_MIGRATION_zh.md` | CycloneDDS 第二中间件迁移细节 |
| `ohos/CROSS_BOARD_TEST_zh.md` | 两板全功能测试矩阵说明 |
| `ohos/rk3588a_test_results.md` | 历次实测结果矩阵（真相源） |
| `ohos/MIGRATION_SUMMARY.md` | 英文构建摘要 / 内部进度 |
| `ohos/README.md` | `ohos/` 子系统与脚本索引 |

---

## 1. 背景与架构

ROS 2 官方不为 KaihongOS/OHOS 提供构建。本迁移**不依赖 OpenHarmony 树内 GN 集成**，而是用 OHOS SDK 的
LLVM 工具链把上游 ROS 2 源码**逐包交叉编译**为 `aarch64-linux-ohos`（musl），产出一套**自包含、可移植**的
运行时前缀，再用 `hdc` 部署到板子。

分层（自底向上）：

```
应用 / CLI         ros2 node|topic|service|param|action|bag|launch ... + rclcpp / rclpy
核心层             rcl / rclcpp / rclpy / rmw / rosidl / 消息包 / rosbag2 / tf2 / lifecycle ...
中间件 (RMW)       rmw_fastrtps_cpp  ←→  FastDDS     |     rmw_cyclonedds_cpp  ←→  CycloneDDS
平台               KaihongOS / OpenHarmony 6.x，aarch64，musl libc，toybox userland
```

两个中间件**并存但相互独立**：FastDDS 是主前缀，CycloneDDS 以叠加层形式提供（见 §6、§2 的"直链"约束）。

---

## 2. 平台关键约束（先理解，再动手）

这些是 OHOS/musl 区别于桌面 Linux 的硬约束，决定了整套方案的形态：

1. **musl，不是 glibc**：所有产物必须链接 musl。验证：`file <so>` 显示 `aarch64`，`readelf -d` 的
   `DT_NEEDED` 应为 `libc.so` / `libc.musl-aarch64.so.1`，绝不能出现 `ld-linux-x86-64` 或 glibc。
2. **RMW 必须"直链"，不能运行时 dlopen 切换**：ROS 2 标准的 `rmw_implementation` poco（按
   `RMW_IMPLEMENTATION` 环境变量在运行时 `dlopen` 后端）**在 OHOS/musl 上会破坏 C++(rclcpp) 的
   typesupport**（节点初始化创建 /rosout 发布者时报 `Registered Type must have a name`，CycloneDDS 直接
   段错误）——根因是 musl 的 `RTLD_LOCAL` 作用域更严格，C++ typesupport 标识符跨 dlopen 边界去重失败
   （C/rclpy 路径不受影响，`RTLD_GLOBAL` 也修不好）。**因此每个 DDS 各用一套直链产物 + 各自启动器**，
   靠"选哪个启动器"切换中间件，而非环境变量。详见 `CYCLONEDDS_MIGRATION_zh.md`。
3. **toybox tar 拒绝逃逸符号链接**：设备端 tar 会拒绝解压指向 prefix 之外的**绝对符号链接**，并令
   **整个解压失败**。故 numpy 依赖的 `libc.musl-aarch64.so.1`（指向设备 `/lib/ld-musl-aarch64.so.1`）
   **不能进 host tarball**，必须部署解压后在设备端 `ln -sf`（见 §7）。
4. **根分区只读**：`/usr/local/bin` 等在只读 ext4 上，装全局 `ros2` 启动器需 `mount -o rw,remount /`
   写入后再 remount 回 ro。
5. **ROS 2 域 ID 必须在 [0, 232]**：超出即 `domainId is over 232`。两板同网并行时域段必须不重叠。
6. **hdc 长会话怪癖**：宿主 `hdc` 可能在产出有效输出后段错误/返回 -1。**一律以"设备端 marker/结果
   文件"判成败**，而非 hdc 退出码；板侧 `nohup` + 结果文件 + 轮询最可靠。

---

## 3. 前置条件与环境

| 依赖 | 说明 |
|---|---|
| KaihongOS SDK | `command-line-tools`（OHOS LLVM / CMake / Ninja / HMOS 工具链）。默认根 `/home/kaihong/M-DDS_4.1`，自动回退 `/home/kaihong/M-DDS` |
| OpenHarmony 预编译 | `OpenHarmony/out/arm64/<product>/.../usr`：目标 Python 3.12 stdlib、libtinyxml2.a、asio 等 |
| ROS 2 源码 | 本仓库 `src/`（`vcs import src < ros2.repos`，分支 jazzy） |
| CPython 3.12.7 源码 | 交叉编译 Python 扩展用 |
| 主机工具 | `hdc`、glibc tar、Python3 |

常用环境变量（不设则用默认）：

```bash
export ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT=/home/kaihong/M-DDS/command-line-tools
export ROS2_OHOS_OPENHARMONY_ROOT=/home/kaihong/M-DDS/OpenHarmony
export ROS2_OHOS_ARCH=arm64-v8a
export ROS2_OHOS_STL=c++_static
export ROS2_OHOS_BUILD_TYPE=Release
export ROS2_OHOS_TARGET_PYTHON_VERSION=3.12
export ROS2_OHOS_TARGET_PYTHON_EXTENSION_SUFFIX=.cpython-312-aarch64-linux-ohos.so
```

交叉编译工具链文件：`ohos/cmake/kaihongos.toolchain.cmake`（引入 HMOS `hmos.toolchain.cmake`）。
工具自动从 SDK 根推导 `cmake`/`ninja`/主机 `python3`。

两块板（实测设备，IP 不持久，重启后需重设——部署/测试脚本会幂等重应用）：

| 板 | device id | eth1 IP |
|---|---|---|
| A | `3e01ff55454d202020104033bf453b00` | `192.168.77.10` |
| B | `3e01ff55454d202020104433991c3b00` | `192.168.77.11` |

---

## 4. 产物布局（构建后的"真相源"）

| 主机产物 | 内容 |
|---|---|
| `install/ohos-ros2` | **自包含 underlay**（约 194 资源）：rcl/rclcpp/rclpy、rmw_fastrtps、消息包、rosbag2、tf2 等；FastDDS 运行库 + vendor 库（yaml/spdlog/sqlite3/lz4/zstd）并入 `lib/`；真 numpy + 支撑库、ament_copyright、packaging 等在 `lib/python3.12/site-packages/` |
| `install/ohos-colcon-rk3588a` | **colcon overlay**（45 包 / 58 资源）：CLI + 运行时切片，`bin/ros2` wrapper 自动导出 HOME/LD/PYTHONPATH |
| `install/ohos-fastdds` | FastDDS 前缀（运行库已并入 underlay，供 CMake 路径变量用） |
| `install/ohos-cyclonedds` | CycloneDDS core（`libddsc.so.0.10.5`），见 §6 |
| `build/ohos-python-runtime/usr` | 目标 Python 3.12 运行时闭包（stdlib + lib-dynload） |

设备端布局（部署后）：`/data/local/tmp/ohos-prefix`（underlay）、`/data/local/tmp/ohos-colcon-rk3588a`
（overlay）、`/data/local/tmp/ohos-fastdds`、`/data/local/tmp/ohos-cyc`（CycloneDDS 叠加层）。

---

## 5. 构建 ROS 2 + FastDDS（主路径）

从 OHOS 源码根目录运行（脚本在 `ohos/`）：

```bash
# 1) ament 引导 + 核心 ament_cmake 包 → install/ohos-ros2 骨架
./ohos/build_ros2_bootstrap.sh

# 2) FastDDS 栈：foonathan_memory_vendor → Fast-CDR → Fast-DDS
#    关键开关：-DNO_TLS=ON -DSHM_TRANSPORT_DEFAULT=OFF -DSECURITY=OFF
./ohos/build_fastdds_stack.sh

# 3) 目标 Python 3.12 运行时（扩展 + 纯 stdlib）
./ohos/build_python312_dynload.sh
./ohos/stage_python312_stdlib.sh

# 4) 逐包交叉编译 ROS 2，填满 install/ohos-ros2
#    单包用法：./ohos/build_ros2_package.sh <source-dir> [build-dir] [-- <额外 cmake 参数>]
#    例（zstd 压缩插件需 vendor 提示）：
./ohos/build_ros2_package.sh src/ros2/rosbag2/rosbag2_compression_zstd -- \
  -Dzstd_LIBRARY="$PWD/install/ohos-ros2/opt/zstd_vendor/lib/libzstd.so" \
  -Dzstd_INCLUDE_DIR="$PWD/install/ohos-ros2/opt/zstd_vendor/include"

# 5) colcon overlay（45 包默认集）→ install/ohos-colcon-rk3588a
ROS2_OHOS_COLCON_CLEAN_INSTALL=1 ./ohos/colcon_rk3588a.sh

# 6) 运行时闭包 staging（把纯 Python 助手并入 overlay/underlay）
ROS2_OHOS_COLCON_INSTALL_BASE="$PWD/install/ohos-colcon-rk3588a" \
  ./ohos/stage_colcon_runtime_closure.sh
```

要点：
- `build_ros2_package.sh` 已内置 `RCLPY_OHOS_*` 与 numpy/pybind11 提示，使 **rclpy** 能交叉编译（其 OHOS
  适配在提示就位时只 `find_package(Python3 Interpreter)`，跳过无法满足的 Development 组件）。
- staging 会并入：`packaging`、`pyparsing`、`catkin_pkg`、`em`、`argcomplete`、`yaml`、`lark`、
  **`ament_copyright`**（`ros2 pkg create` 必需）、**真 numpy**（优先于桩，见 §11 numpy 坑）。
- 构建用 `PYTHONPATH` 须同时含 `${PREFIX}/lib/python3.12/site-packages` 与 `build/ohos-ros2/pydeps`。

---

## 6. 构建 CycloneDDS（第二中间件）

CycloneDDS 与 FastDDS 并存，各自直链部署（§2 约束 2）。完整说明见 `CYCLONEDDS_MIGRATION_zh.md`。

```bash
# 1) CycloneDDS core（libddsc 0.10.5；header-only 内置类型，无需 idlc）
#    关键开关：ENABLE_SECURITY=NO（免 OpenSSL）、无 iceoryx（自动关 SHM）、ENABLE_LTO=OFF
./ohos/build_cyclonedds_stack.sh        # → install/ohos-cyclonedds/lib/libddsc.so

# 2) rmw_cyclonedds_cpp（复用通用 introspection typesupport，无需重建每个消息包）
./ohos/build_ros2_package.sh src/ros2/rmw_cyclonedds/rmw_cyclonedds_cpp -- \
  -DCycloneDDS_DIR=$PWD/install/ohos-cyclonedds/lib/cmake/CycloneDDS

# 3) CycloneDDS 直链消费栈（rcl/rclcpp/demos/rosbag2/tf2_ros/... 约 40 包）
#    设 RMW_IMPLEMENTATION=rmw_cyclonedds_cpp 使 get_default_rmw_implementation 与直链默认一致，
#    rmw_implementation 用 -DRMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=ON 直链 cyclone；
#    逐包带 -DCycloneDDS_DIR。重建集合 = 所有 DT_NEEDED librmw_fastrtps_cpp.so 的产物（约 40 个）。
```

`libddsc.so.0.10.5` 应为 ARM aarch64、仅依赖 `libc.so`（无 OpenSSL/iceoryx）。CycloneDDS 部署为叠加层
`/data/local/tmp/ohos-cyc`（复用 FastDDS underlay 里所有 RMW 无关库，只覆盖直链 cyclone 的消费者 +
`libddsc.so` + `librmw_cyclonedds_cpp.so` + ament `packages` 标记），并配启动器 `ohos-cyc/ros2-cyclone`
（`LD_LIBRARY_PATH`/`AMENT_PREFIX_PATH`/`PYTHONPATH` 把 `ohos-cyc` 排在 `ohos-prefix` 前，
`RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`）。

---

## 7. 部署到 RK3588A（一键、可复现）

```bash
# 清盘 + 全新部署 FastDDS 整套栈到一块板
bash ohos/deploy_all_rk3588a.sh <device_id> --wipe
```

五阶段，每阶段以**设备端 marker** 判成败：

| 阶段 | 动作 | 目标 | marker |
|---|---|---|---|
| 1 underlay | 分块(4MB) gzip 部署 `install/ohos-ros2` | `/data/local/tmp/ohos-prefix` | `ros2_prefix_chunked_deploy_ok` |
| — musl link | 设备端建 numpy 依赖的 musl libc 符号链接 | `.../ohos-prefix/lib/libc.musl-aarch64.so.1` | `MUSL_LINK_OK` |
| 2 fastdds | 部署 `install/ohos-fastdds`（可选） | `/data/local/tmp/ohos-fastdds` | `ohos-fastdds_OK` |
| 3 overlay | 部署 `install/ohos-colcon-rk3588a` | `/data/local/tmp/ohos-colcon-rk3588a` | `ohos-colcon-rk3588a_OK` |
| 4 scripts | 部署验证脚本 | `/data/local/tmp/ros2-validate` | `VAL_OK` |
| 5 launcher | 装全局 `ros2` 启动器 | `/usr/local/bin/ros2` | `INSTALL_OK` |

**musl 符号链接**（关键）：部署解压后于设备端执行（脚本已内置）：

```bash
ln -sf /lib/ld-musl-aarch64.so.1 /data/local/tmp/ohos-prefix/lib/libc.musl-aarch64.so.1
```

**全局 `ros2` 启动器**：`install_ros2_launcher.sh` 会 remount 根分区为 rw，在 `/usr/local/bin/ros2`
写一个**用绝对路径 exec overlay wrapper** 的启动器（不能用符号链接——wrapper 靠 `dirname $0` 定位
PREFIX），再 remount 回 ro。之后任意 shell `ros2 node list` 即可。

**CycloneDDS 叠加层**部署到 `/data/local/tmp/ohos-cyc`（含 `ros2-cyclone` 启动器）；用法见 §6 与
`CYCLONEDDS_MIGRATION_zh.md`。

---

## 8. 运行（两种中间件）

```bash
# FastDDS（默认，全局启动器）
ros2 run demo_nodes_cpp talker
ros2 topic list

# CycloneDDS（叠加层启动器；/usr/local/bin 只读无法装全局）
/data/local/tmp/ohos-cyc/ros2-cyclone run demo_nodes_cpp talker
/data/local/tmp/ohos-cyc/ros2-cyclone doctor --report   # middleware name: rmw_cyclonedds_cpp
```

裸二进制（不经启动器）需手动导出 `HOME=/data/local/tmp`、`ROS_LOG_DIR`、`LD_LIBRARY_PATH`、
`PYTHONPATH`、`ROS_DOMAIN_ID`、`ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET`。

---

## 9. 验证：单板 130 车道

部署后在板上跑三套矩阵（`RESULT|<车道>|PASS/FAIL|<证据>`，跑完打 `*VALIDATION_DONE`）：

```bash
DOM_BASE=60  sh /data/local/tmp/ros2-validate/rk3588a_validate_all.sh   # 25 核心
DOM_BASE=160 sh /data/local/tmp/ros2-validate/rk3588a_validate_ext.sh   # 42 扩展
DOM=51       sh /data/local/tmp/ros2-validate/rk3588a_validate_cli.sh   # 63 CLI 命令
```

覆盖：rclcpp/rclpy pub/sub、service、action、lifecycle、composition、component、tf2、
robot_state_publisher、pluginlib、QoS 全策略、参数全集、序列化/loaned、内容过滤、服务内省、wait-set、
话题统计、日志、执行器/回调组/guard、image/point-cloud transport、rosbag2（sqlite3+mcap+burst+convert+
zstd）、launch、ros2 全 CLI 命令。**实测两板各 130/130 PASS**。域段约束见 §2 约束 5。

---

## 10. 验证：两板跨板 + 双 DDS + 跨厂商互通

一条命令跑完三种 DDS 模式的全功能跨板矩阵 + 每板本地非分布式功能（说明见 `CROSS_BOARD_TEST_zh.md`）：

```bash
bash ohos/tools/run_cross_board_full_matrix.sh \
  3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00
```

- **fastdds**：两板都用 FastDDS，跑全部车道；
- **cyclonedds**：两板都用 CycloneDDS，跑全部车道；
- **interop**：A=FastDDS ↔ B=CycloneDDS（同域），跑 pub/sub 类车道（**跨厂商 RTPS pub/sub 互通**）；
- **local**：每板各跑两种 DDS 的本地功能（进程内组合、组件容器、wait-set、日志、话题统计、rclpy 回环、
  传输插件、内省、doctor）。

**实测结果：TOTAL 99 PASS / 0 FAIL / 1 NA。** 唯一 NA 是 `fastdds_content_filter`——内容过滤主题
（content-filtered topic）在精简 FastDDS 构建里未启用；**同一车道在 CycloneDDS 上 PASS**（cyclone 支持
CFT）。

**关键互通结论**：FastDDS ↔ CycloneDDS 的 **pub/sub 双向互通**；但**请求-应答（service/action）跨厂商
不互通**（调用会挂起），故 interop 只跑 pub/sub 类车道，并在客户端加设备端 `timeout` 守护防止挂死。

也可只跑最简单的跨板 std_msgs 双向（FastDDS）：

```bash
bash ohos/tools/run_cross_board_cli_pubsub.sh <dev_A> <dev_B> 55
```

---

## 11. 已知坑大全（复用者必读）

| 症状 | 根因 | 修复 |
|---|---|---|
| rclpy **action / 含定长数组消息**序列化即 SIGSEGV | numpy **桩**（list 子类）不满足 C-API 内存布局 | 用**真 numpy**（交叉编译，扩展后缀 `-ohos.so`）；staging 优先真 numpy |
| C++ 节点用运行时 RMW 切换报 `Registered Type must have a name` / 段错误 | musl `RTLD_LOCAL` 下 C++ typesupport 跨 dlopen 去重失败（§2 约束 2） | 每 DDS 各用直链产物 + 各自启动器，勿用 `RMW_IMPLEMENTATION` 运行时切换 |
| 清盘部署解压 `tar: had errors` | `lib/libc.musl-aarch64.so.1` 是逃逸符号链接，toybox tar 拒绝 | host prefix 不放它，部署后设备端 `ln -sf`（§7） |
| `ros2 pkg create` 报 `No module named 'ament_copyright'` | 未部署 | staging 把纯 Python `ament_copyright` 并入两 prefix |
| `ros2 bag record` 后台跑收不了尾、丢 `metadata.yaml` | POSIX sh 给后台作业设 `SIGINT=SIG_IGN`，CPython 不恢复 | recorder 前台跑 + 后台看门狗 `kill -TERM`（`rk3588a_bag_lanes.sh`） |
| `ros2 bag burst -n N` 挂死 | burst 播完保持暂停不退出 | 后台跑 + 几秒后 kill |
| `import yaml/packaging` ModuleNotFoundError | host prefix 曾用悬空符号链接 / 仅 overlay 有 | 换真实拷贝；staging 两 prefix 都并入 |
| 节点崩在 `//.ros/log` | `HOME` 未设 | 启动器自动导出 `HOME`/`ROS_LOG_DIR`；裸二进制需手设 |
| 某域全失败 `domainId is over 232` | 域 ID 超上限 | `DOM_BASE ≤ 193`，脚本内有守卫 |
| hdc 输出有效却返回 -1 / 段错误 | 宿主 hdc 长会话怪癖 | 以设备端 marker/结果文件判成败（§2 约束 6） |
| `ros2 bag` 默认 mcap 而非 sqlite3 | 本构建默认 mcap | 显式 `-s sqlite3` |
| 重拉 jazzy 后某消息发布即 `UnsupportedTypeSupport` | 接口包新增 `.msg` 但未重建（typesupport 缺新符号） | 重建集合须含**消息集变化的接口 repo**（如 `common_interfaces`），见 §12 |
| 跨厂商 service/action 调用挂起 | FastDDS↔CycloneDDS 请求-应答不互通 | interop 只测 pub/sub；客户端加 `timeout` |
| FastDDS 下 `content_filter` 失败 | 精简 FastDDS 构建未启用 content-filtered topic | 属可选特性，记为 NA；CycloneDDS 下可用 |

---

## 12. 从官方同步并重新迁移（后续工作流）

精简流程（完整实操记录见 `MIGRATION_GUIDE_zh.md` §8-§9）：

1. **先全量备份** `src/`（OHOS 适配大多是 src 内未提交改动，`vcs pull` 会抹掉）。
2. **剥除工具注入的 `codex-file-meta` 噪声**，再 `git checkout` 还原伪空行改动，留纯 OHOS delta。
3. **捕获完整 OHOS delta**：每个核心 repo `git diff origin/jazzy > ohos/patches_full/<slug>.patch` +
   复制未跟踪新文件（含未跟踪**目录**）。
4. **拉取 + 重应用**：`git reset --hard origin/jazzy && git clean -fdq` 后 `git apply --3way <patch>`。
5. **构建环境两个必修坑**：① SDK `setuptools` 升级要求 `packaging≥22`，用 SDK vendored packaging 24.2
   替换旧 20.3；② 清掉 `build/ohos-ros2/<pkg>/CMakeCache.txt` 里失效的旧编译器路径缓存。
6. **重建集合 = OHOS 改动 repo ∪ 消息集变化的接口 repo**（jazzy ABI 只增不减，纯 C++ 旧库可用；唯独
   接口包新增 `.msg`/`.srv`/`.action` 会让 typesupport 导出新符号，必须重建该接口包）。
7. 清盘重部署 + 单板 130 车道 + 两板矩阵重验，对照 §11 排查回归。

---

## 13. 其他 DDS 与边界

- **rmw_connextdds**（RTI Connext）：商业授权，需 RTI 库与 license，本环境不具备 → 阻塞。
- **rmw_gurumdds**：不在源码树内。
- 可落地的开源 DDS 即 **FastDDS + CycloneDDS**，两者均已迁移并在 RK3588A 上验证。

---

## 14. 速查

```bash
# 设备
hdc list targets                              # 查 device id（USB Connected）
# 重设 eth1（重启后；脚本会幂等做）
hdc -t <dev> shell "ip link set eth1 up; ip addr add 192.168.77.10/24 dev eth1"

# 一句话复现（已有主机产物）
bash ohos/deploy_all_rk3588a.sh <dev_A> --wipe
bash ohos/deploy_all_rk3588a.sh <dev_B> --wipe
bash ohos/tools/run_cross_board_full_matrix.sh <dev_A> <dev_B>   # 99/0/1
```

| 关键路径 | 值 |
|---|---|
| FastDDS 启动器 | `/usr/local/bin/ros2` |
| CycloneDDS 启动器 | `/data/local/tmp/ohos-cyc/ros2-cyclone` |
| underlay / overlay | `/data/local/tmp/ohos-prefix` / `/data/local/tmp/ohos-colcon-rk3588a` |
| 设备 Python | `/data/local/release/usr/bin/python3.12` |
| 工具链文件 | `ohos/cmake/kaihongos.toolchain.cmake` |
