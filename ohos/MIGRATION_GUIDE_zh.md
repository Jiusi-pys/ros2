# ROS 2 迁移到 KaihongOS / RK3588A 完整复用指南

> 面向他人复用：如何把 ROS 2（Jazzy）交叉编译、部署并验证到 **KaihongOS / OpenHarmony**
> 的 **RK3588A**（`aarch64-linux-ohos`，**musl** libc）开发板，并实现两块板之间通过 ROS 2 over
> FastDDS 跨板通信。本指南记录的全部步骤都来自仓库内实际可运行的脚本，已在两块 RK3588A 上
> **清盘从零重部署 + 130 项验证全过**。

本指南是面向复用的中文操作手册；英文构建摘要见 `ohos/MIGRATION_SUMMARY.md`，测试结果矩阵见
`ohos/rk3588a_test_results.md`。

---

## 0. 目标与成果

- **目标平台**：RK3588A，KaihongOS / OpenHarmony 6.x，`aarch64-linux-ohos`，**musl**（不是 glibc）。
- **中间件**：Fast DDS（`rmw_fastrtps_cpp`），不依赖 OpenHarmony 树内 GN 集成。
- **成果**：标准 ROS 2 CLI（`ros2 node/topic/service/param/action/...`）、rclcpp/rclpy、生命周期、
  组件、tf2、rosbag2（sqlite3 + mcap + zstd 压缩）、launch 等全部可用；两块板可通过 FastDDS 跨板收发。
- **验证规模**：130 个验证车道（25 核心 + 42 扩展 + 63 CLI 命令）两板各跑全过 + 跨板双向传输。

---

## 1. 前置条件

| 依赖 | 说明 |
|---|---|
| KaihongOS M-DDS SDK | `command-line-tools`（含 OpenHarmony LLVM / CMake / Ninja / HMOS 工具链）。默认根：`/home/kaihong/M-DDS_4.1` 或回退 `/home/kaihong/M-DDS` |
| OpenHarmony 预编译 | `OpenHarmony/out/arm64/<product>/.../usr`：提供目标 Python 3.12 stdlib、libtinyxml2.a、asio 等 |
| ROS 2 源码 | 本仓库 `src/`（`vcs import src < ros2.repos`） |
| CPython 3.12.7 源码 | 用于交叉编译 Python 扩展（`build/Python-3.12.7` 或 `/tmp/Python-3.12.7`） |
| 主机工具 | `hdc`（OpenHarmony Device Connector）、glibc tar、Python3 |

关键环境变量（不设则用默认）：

```bash
export ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT=/home/kaihong/M-DDS_4.1/command-line-tools
export ROS2_OHOS_OPENHARMONY_ROOT=/home/kaihong/M-DDS_4.1/OpenHarmony
export ROS2_OHOS_ARCH=arm64-v8a
export ROS2_OHOS_STL=c++_static
export ROS2_OHOS_BUILD_TYPE=Release
export ROS2_OHOS_TARGET_PYTHON_VERSION=3.12
export ROS2_OHOS_TARGET_PYTHON_EXTENSION_SUFFIX=.cpython-312-aarch64-linux-ohos.so
```

工具自动从 `COMMAND_LINE_TOOLS_ROOT` 推导：
`.../sdk/default/openharmony/native/build-tools/cmake/bin/{cmake,ninja}` 与
`.../llvm/python3/bin/python3`（主机侧构建用 Python）。交叉编译工具链文件：
`ohos/cmake/kaihongos.toolchain.cmake`（会引入 HMOS `hmos.toolchain.cmake`）。

---

## 2. 产物总览（构建后的"真相源"）

| 主机产物 | 内容 |
|---|---|
| `install/ohos-ros2` | **standalone underlay**：194 资源的 ROS 2 运行时（含 rcl/rclcpp/rclpy、rmw_fastrtps、消息包、rosbag2、tf2 等）；FastDDS 运行库与 vendor 库（libyaml/spdlog/sqlite3/lz4/zstd）已并入 `lib/`；真 numpy + 支撑库（openblas/libgfortran/libgcc_s）、ament_copyright、yaml、packaging 等均在 `lib/python3.12/site-packages/` |
| `install/ohos-colcon-rk3588a` | **colcon overlay**：45 包 / 58 资源的 CLI+运行时切片，`bin/ros2` 等 wrapper 自动导出 HOME/LD/PYTHONPATH |
| `install/ohos-fastdds` | FastDDS 前缀（运行库其实已并入 underlay `lib/`，此前缀供 CMake 路径变量用，部署时可选） |
| `build/ohos-python-runtime/usr` | 目标 Python 3.12 运行时闭包（stdlib + lib-dynload 扩展） |

> **设计要点**：`install/ohos-ros2` 是**自包含且可移植**的——清盘后仅靠它（+ overlay）即可复现整套
> 部署，无需任何手工补丁。唯一的设备相关项是 musl libc 符号链接（见 §4 与 §6）。

---

## 3. 构建流程（从源码产出上述前缀）

> 全部从 OHOS 源码根目录（含 `build.sh`）下运行；脚本在 `ohos/`。

```bash
# 1) ament 引导 + 核心 ament_cmake 包，产出 install/ohos-ros2 骨架
./ohos/build_ros2_bootstrap.sh

# 2) FastDDS 栈：foonathan_memory_vendor → Fast-CDR → Fast-DDS（关键开关 -DNO_TLS=ON
#    -DSHM_TRANSPORT_DEFAULT=OFF -DSECURITY=OFF），产出 install/ohos-fastdds
./ohos/build_fastdds_stack.sh

# 3) 目标 Python 3.12 运行时：扩展(lib-dynload) + 纯 stdlib
./ohos/build_python312_dynload.sh
./ohos/stage_python312_stdlib.sh

# 4) 逐包交叉编译 ROS 2（per-package CMake builder），填满 install/ohos-ros2
#    单包：./ohos/build_ros2_package.sh <source-dir> [build-dir] [-- <额外 cmake 参数>]
#    例（补建 zstd 压缩插件，带 vendor 提示）：
./ohos/build_ros2_package.sh src/ros2/rosbag2/rosbag2_compression_zstd -- \
  -Dzstd_LIBRARY="$PWD/install/ohos-ros2/opt/zstd_vendor/lib/libzstd.so" \
  -Dzstd_INCLUDE_DIR="$PWD/install/ohos-ros2/opt/zstd_vendor/include"

# 5) colcon overlay（45 包默认集），产出 install/ohos-colcon-rk3588a
ROS2_OHOS_COLCON_CLEAN_INSTALL=1 ./ohos/colcon_rk3588a.sh

# 6) 运行时闭包 staging（把纯 Python 助手并入 overlay/underlay）
ROS2_OHOS_COLCON_INSTALL_BASE="$PWD/install/ohos-colcon-rk3588a" \
  ./ohos/stage_colcon_runtime_closure.sh
```

`stage_colcon_runtime_closure.sh` 会把下列**纯 Python 依赖**并入 site-packages（缺则运行/CLI 报错）：
`packaging`、`pyparsing`、`catkin_pkg`、`em`、`argcomplete`、`yaml`、`lark`，以及
**`ament_copyright`**（`ros2 pkg create` 必需）和**真 numpy**（优先于桩——见 §5 numpy 坑）。

> 构建用 PYTHONPATH 必须同时含 `${PREFIX}/lib/python3.12/site-packages` 与 `build/ohos-ros2/pydeps`
> （后者从 OpenHarmony 预编译 Python 软链 catkin_pkg/packaging/yaml/lark/em 供主机侧代码生成）。

---

## 4. 部署流程（一键、可复现）

提供一键脚本 `ohos/deploy_all_rk3588a.sh`，从主机 `install/` 前缀**全新**部署整套栈：

```bash
# 清盘 + 全新部署到一块板（<device_id> 用 hdc list targets 查）
bash ohos/deploy_all_rk3588a.sh <device_id> --wipe
```

五个阶段（每阶段以**设备端 marker** 判成败，而非 hdc 退出码——见 §5 hdc 坑）：

| 阶段 | 动作 | 目标路径 | 成功标记 |
|---|---|---|---|
| 1 underlay | 分块(4MB) gzip 部署 `install/ohos-ros2` | `/data/local/tmp/ohos-prefix` | `ros2_prefix_chunked_deploy_ok` |
| — musl link | 设备端建 numpy 依赖的 musl libc 符号链接 | `.../ohos-prefix/lib/libc.musl-aarch64.so.1` | `MUSL_LINK_OK` |
| 2 fastdds | 部署 `install/ohos-fastdds`（可选） | `/data/local/tmp/ohos-fastdds` | `ohos-fastdds_OK` |
| 3 overlay | 部署 `install/ohos-colcon-rk3588a` | `/data/local/tmp/ohos-colcon-rk3588a` | `ohos-colcon-rk3588a_OK` |
| 4 scripts | 部署验证脚本 | `/data/local/tmp/ros2-validate` | `VAL_OK` |
| 5 launcher | 装全局 `ros2` 启动器 | `/usr/local/bin/ros2` | `INSTALL_OK` |

**musl libc 符号链接（关键、易踩坑）**：numpy 的 `_multiarray_umath.*-ohos.so`（Alpine/musl 编出）
`DT_NEEDED` 了 `libc.musl-aarch64.so.1`。该文件须在 `prefix/lib/` 里，但它是指向设备
`/lib/ld-musl-aarch64.so.1` 的**绝对符号链接**。**绝不能**把它放进 host tarball——设备 toybox tar
出于安全会拒绝解压逃逸 prefix 的符号链接，并令**整个解压失败**（`tar: had errors`）。正解：host
prefix 里不放它，部署脚本在解压后于设备端 `ln -sf`：

```bash
ln -sf /lib/ld-musl-aarch64.so.1 /data/local/tmp/ohos-prefix/lib/libc.musl-aarch64.so.1
```

**全局 `ros2` 启动器**：设备 PATH 为 `/usr/local/bin:/bin:/usr/bin`（只读 ext4 根分区 mmcblk0p6）。
`install_ros2_launcher.sh` 会 `mount -o rw,remount /`，在 `/usr/local/bin/ros2` 写一个**用绝对路径
`exec` overlay wrapper** 的启动器（**不能用符号链接**——wrapper 靠 `dirname $0` 定位 PREFIX，符号
链接会让它误判为 `/`），再 remount 回 ro。装好后任意 shell 直接 `ros2 node list` 即可。

---

## 5. 验证流程

部署后在板上运行三套矩阵（`RESULT|<车道>|PASS/FAIL|<证据>` 约定，跑完打 `*VALIDATION_DONE`）：

```bash
# 25 核心车道（pub/sub、service、action、lifecycle、composition、tf2、bag、launch…）
DOM_BASE=60  sh /data/local/tmp/ros2-validate/rk3588a_validate_all.sh
# 42 扩展车道（QoS 全策略、参数全集、序列化/loaned、内容过滤、内省、wait-set、统计、
#              rclpy 执行器/回调组/guard/action 取消、transport、rosbag2 burst/convert/zstd…）
DOM_BASE=160 sh /data/local/tmp/ros2-validate/rk3588a_validate_ext.sh
# 63 CLI 命令车道（穷举每个 ros2 <命令> <子命令>）
DOM=51       sh /data/local/tmp/ros2-validate/rk3588a_validate_cli.sh
```

**ROS 2 域 ID 必须在 [0,232]**。各矩阵按 `DOM_BASE+偏移` 取域：`validate_all` 用 +0..+20、
`validate_ext` 用 +0..+39（故 `DOM_BASE ≤ 193`，脚本内有守卫）。**两块板同网并行跑单板矩阵时，
域段必须不重叠**（如 A 用 60/160、B 用 20/100）否则跨板串扰。

跨板 FastDDS 双向传输（主机侧编排，需两板 + eth1 互通）：

```bash
bash ohos/tools/run_cross_board_cli_pubsub.sh <device_a> <device_b> 55
# 输出 RESULT|cross_b_to_a|PASS 与 RESULT|cross_a_to_b|PASS 即跨板成功
```

判据汇总：`grep -c PASS`、`grep FAIL`；逐车道细节在 `WORK_DIR`（`/data/local/tmp/val` 或 `valcli`）
下的 `.log`。

---

## 6. 已知坑与修复（复用者必读）

| 症状 | 根因 | 修复 |
|---|---|---|
| rclpy **action / 含定长数组消息** 序列化即 SIGSEGV | numpy **桩**（list 子类）不满足 C-API 内存布局；Release 下 `*_s.c` typesupport 对其调 `PyArray_GETPTR1` → 野指针 | 用**真 numpy**（交叉编译，扩展后缀改 `-ohos.so`）；`stage_colcon_runtime_closure.sh` 优先真 numpy |
| 清盘 chunked 部署解压失败 `tar: had errors` | `lib/libc.musl-aarch64.so.1` 是逃逸 prefix 的**绝对符号链接**，toybox tar 拒绝 | host prefix 不放该符号链接，部署解压后设备端 `ln -sf`（§4） |
| `ros2 pkg create` 报 `No module named 'ament_copyright'` | 该模块未部署 | staging 脚本把工作区 `ament_copyright`（纯 Python）并入两 prefix |
| `ros2 bag record` 后台跑收不了尾、丢 `metadata.yaml` | POSIX sh 给后台作业设 `SIGINT=SIG_IGN`，CPython 不恢复 | recorder **前台跑 + 后台看门狗** `kill -TERM`（见 `rk3588a_bag_lanes.sh`） |
| `ros2 bag burst -n N` 挂死脚本 | burst 播完 N 条后**保持暂停不退出** | 后台跑 + 几秒后 kill（listener 已收到 N 条即可） |
| `ros2 service echo` 报无 `_service_event` 发布者 | 普通 service 默认不启用内省 | 用 `introspection_service` + `ros2 param set ... service_configure_introspection metadata` |
| `import yaml` ModuleNotFoundError | host prefix 的 `site-packages/yaml` 曾是指向 `/usr/lib/python3/...` 的符号链接（设备上悬空） | 换成真实拷贝 |
| CLI 报 `No module named 'packaging'` 等 | 纯 Python 助手只在 overlay、underlay 缺 | staging 脚本两边都并入 |
| 任意节点启动崩在 `//.ros/log` | `HOME` 未设导致日志目录创建失败 | wrapper 自动导出 `HOME=/data/local/tmp`、`ROS_LOG_DIR`（裸二进制需手动设） |
| 所有节点在某域全失败、报 `domainId is over 232` | 域 ID 超过 232 上限 | `DOM_BASE ≤ 193`；脚本内有守卫 |
| hdc 命令输出有效却返回 `-1` / 段错误 | 宿主 hdc 长会话已知怪癖（产出后段错误） | **以设备端 marker 判成败**，`|| true` 包裹避免 `set -e` 误伤；板侧 `nohup`+结果文件+轮询更可靠 |
| `ros2 bag` 默认存储是 mcap 而非 sqlite3 | 本构建 rosbag2 默认 mcap | 录制/reindex 显式 `--storage sqlite3` / `-s sqlite3`，全组一致 |

---

## 7. 一句话复现（已有主机产物时）

```bash
# 1) 清盘 + 全新部署两板
bash ohos/deploy_all_rk3588a.sh <dev_A> --wipe
bash ohos/deploy_all_rk3588a.sh <dev_B> --wipe
# 2) 两板全量验证（域段错开）
hdc -t <dev_A> shell "DOM_BASE=60  sh /data/local/tmp/ros2-validate/rk3588a_validate_all.sh"
hdc -t <dev_A> shell "DOM_BASE=160 sh /data/local/tmp/ros2-validate/rk3588a_validate_ext.sh"
hdc -t <dev_A> shell "DOM=51       sh /data/local/tmp/ros2-validate/rk3588a_validate_cli.sh"
hdc -t <dev_B> shell "DOM_BASE=20  sh /data/local/tmp/ros2-validate/rk3588a_validate_all.sh"
hdc -t <dev_B> shell "DOM_BASE=100 sh /data/local/tmp/ros2-validate/rk3588a_validate_ext.sh"
hdc -t <dev_B> shell "DOM=53       sh /data/local/tmp/ros2-validate/rk3588a_validate_cli.sh"
# 3) 跨板传输
bash ohos/tools/run_cross_board_cli_pubsub.sh <dev_A> <dev_B> 55
```

---

## 8. 与官方 ROS 2 同步并重新迁移（后续工作流）

要从官方 `ros2/ros2`（`upstream`）拉最新并重做迁移：

1. `vcs custom --git --args fetch` 或更新 `ros2.repos` 后 `vcs import --force src < ros2.repos` 同步源码。
2. 重新打本地补丁：`./ohos/apply_workspace_patches.sh`（见 `ohos/patches/`，含 rcutils musl strerror、
   rmw_fastrtps 独立前缀、geometry2 OHOS 等适配）。
3. 按 §3 重新构建（bootstrap → fastdds → python runtime → 逐包 → colcon overlay → staging）。
4. 按 §4 清盘重部署、按 §5 全量验证，对照 §6 排查新版本可能引入的回归。
5. 验证通过后备份到个人 GitHub（`origin`），官方为 `upstream`，公司 Gerrit 另算。

> 注意：升级官方版本后，§6 的坑大多仍适用；新引入的失败优先怀疑"测试脚本判据 vs 真实运行时缺陷"，
> 用本仓库的对抗式排查方式（设备实测交叉印证，勿单信任一来源）逐项定位。
