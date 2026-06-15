# CycloneDDS 迁移到 RK3588A / KaihongOS（2026-06-15）

把第二个 DDS 中间件 **Eclipse CycloneDDS**（`rmw_cyclonedds_cpp`）迁移到 RK3588A，与已有的 FastDDS
并存。目标：ROS 2 既能跑 FastDDS，也能跑 CycloneDDS（"migrate all dds like FastDDS, cycloneDDS"）。

## 关键结论：OHOS/musl 上 RMW 必须"直链"，不能运行时 dlopen 切换

ROS 2 标准做法是 `rmw_implementation` 的 **poco**（`librmw_implementation.so`，按 `RMW_IMPLEMENTATION`
环境变量在运行时 `dlopen` 后端）。**在 KaihongOS（aarch64-linux-ohos，musl）上这对 C++(rclcpp) 节点不可用**：

- 现象：`[PARTICIPANT Error] Registered Type must have a name -> register_type` →
  `create_publisher() failed to register type`（节点初始化创建 /rosout 发布者时，rcl_interfaces/Log），
  CycloneDDS 下表现为 segfault。
- 根因：musl 的 `RTLD_LOCAL` 作用域比 glibc 严格，`dlopen` 进来的后端与进程镜像里的 **C++** rosidl
  typesupport 标识符去重失败 → C++ typesupport 句柄解析拿到空类型名。给 rcutils 的 `dlopen` 加
  `RTLD_GLOBAL` 也**无效**。
- **C 路径（rclpy）不受影响**：rclpy 在 poco 下切 FastDDS / CycloneDDS 都能正常收发。只有 C++ 炸。
- 原始 OHOS 构建之所以"天生正常"：`rmw_implementation` 在只有 1 个 RMW 可用时自动
  `RMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=ON`，所有二进制 `DT_NEEDED librmw_fastrtps_cpp.so`
  **直链**——这正是 C++ typesupport 能工作的前提，也是本平台正确架构。

**因此多 DDS 方案 = 每个 DDS 各自"直链"部署**（一个 DDS 一个 prefix，靠选 prefix/launcher 切换，
而不是运行时 `RMW_IMPLEMENTATION`）。详见内存 `ohos-musl-rmw-direct-link`。

## 构建步骤

1. **CycloneDDS core**（`libddsc.so`，header-only 内置类型无需 idlc）：
   ```bash
   ./ohos/build_cyclonedds_stack.sh      # -> install/ohos-cyclonedds/lib/libddsc.so (0.10.5)
   ```
   关键 CMake 选项：`ENABLE_SECURITY=NO`（免 OpenSSL）、无 iceoryx（0.10.x 自动关 SHM）、
   `BUILD_TESTING/EXAMPLES/IDLC=OFF`、`ENABLE_LTO=OFF`。产物 `libddsc.so.0.10.5` 仅依赖 `libc.so`（musl）。

2. **rmw_cyclonedds_cpp**（用通用 introspection typesupport，**无需重建每个消息包**）：
   ```bash
   ./ohos/build_ros2_package.sh src/ros2/rmw_cyclonedds/rmw_cyclonedds_cpp -- \
     -DCycloneDDS_DIR=$PWD/install/ohos-cyclonedds/lib/cmake/CycloneDDS
   ```
   cyclonedds 未开 SHM → rmw_cyclonedds 不需要 iceoryx（`SHM_SUPPORT_IS_AVAILABLE` 为假）。

3. **CycloneDDS 直链消费栈**（rcl/rclcpp/demos/rosbag2/tf2_ros/... 共 ~40 包）：
   ```bash
   export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp     # 让 get_default_rmw_implementation 与直链默认一致
   ./ohos/build_ros2_package.sh src/ros2/rmw_implementation/rmw_implementation -- \
     -DRMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=ON -DRMW_IMPLEMENTATION=rmw_cyclonedds_cpp \
     -DCycloneDDS_DIR=$PWD/install/ohos-cyclonedds/lib/cmake/CycloneDDS
   # 然后对每个 rmw 消费包：build_ros2_package.sh <pkg> -- -DCycloneDDS_DIR=<...>
   ```
   重建集合 = 所有 `DT_NEEDED librmw_fastrtps_cpp.so` 的 lib/可执行/python 扩展对应的包（用
   `llvm-readelf -d` 扫 `install/ohos-ros2` 得到，~40 个）。`rclpy` 需要 `RCLPY_OHOS_*` Python 提示
   （已并入 `build_ros2_package.sh`）；`quality_of_service_demo_cpp` 路径 basename 为 `rclcpp` 与真 rclcpp
   build 目录冲突，需显式 build 目录；`robot_state_publisher` 需要 Eigen（`/tmp/eigen3-root/usr/include/eigen3`）。

## 部署（cyclone 叠加层）

CycloneDDS 部署为叠加层 `/data/local/tmp/ohos-cyc`，复用 FastDDS underlay（`ohos-prefix`）里所有
rmw 无关库，只覆盖直链 cyclone 的消费者 + `libddsc.so` + `librmw_cyclonedds_cpp.so`：

- `ohos-cyc/lib/` = cyclone 直链的 lib/可执行/rclpy 扩展（用 `llvm-readelf` 选出 `DT_NEEDED librmw_cyclonedds_cpp.so` 的产物）+ `libddsc.so*`
- `ohos-cyc/share/ament_index/resource_index/packages/<pkg>` 标记，使 `ros2 run` 命中 cyclone 叠加层
- 启动器 `ohos-cyc/ros2-cyclone`：`LD_LIBRARY_PATH`/`AMENT_PREFIX_PATH`/`PYTHONPATH` 把 `ohos-cyc` 排在
  `ohos-prefix` 前；`RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`。

用法：`/data/local/tmp/ohos-cyc/ros2-cyclone run demo_nodes_cpp talker`（`/usr/local/bin` 只读，
无法装全局 `ros2-cyclone`）。

## 验证结果（两板，2026-06-15）

| 项 | 结果 |
|---|---|
| `libddsc.so.0.10.5` | ARM aarch64 / musl，仅依赖 `libc.so` |
| CycloneDDS C++ talker→listener（单板，直链叠加层） | PASS（12/12） |
| CycloneDDS C++ service `add_two_ints` | PASS（`Result of add_two_ints: 5`） |
| **跨板 CycloneDDS C++ A→B**（eth1, domain 55） | PASS（B 收到 9 条 over eth1） |
| `ros2-cyclone run` + `ros2-cyclone doctor` | PASS（`middleware name: rmw_cyclonedds_cpp`） |
| rclpy（C 路径）在 CycloneDDS 下 | PASS（poco 实验阶段已验证发布） |
| **FastDDS 全 130 车道两板回归** | 130/130 PASS（恢复直链后无回归） |

## 其他 DDS（rmw_connextdds / rmw_gurumdds）

源码树有 `rmw_connextdds`（RTI Connext，**商业授权**，需 RTI Connext 库与 license，本环境不具备，
无法编译/运行——其 DDS 实现是闭源商业件）。`rmw_gurumdds` 不在源码树内。可落地的开源 DDS 即
**FastDDS + CycloneDDS**，两者均已迁移并在 RK3588A 上验证。
