# 两板 RK3588A ROS 2 全功能跨板测试（FastDDS + CycloneDDS + 互通）

主机编排的两板测试：证明两块物理 RK3588A 用 ROS 2 跨板传输，并覆盖**全部 ROS 2 功能**，
分别在 **FastDDS**、**CycloneDDS** 两种中间件下运行，外加 **跨厂商互通（interop）**。

## 一键运行

```bash
bash ohos/tools/run_cross_board_full_matrix.sh \
  3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00
```

- 自动预检：两板在线、eth1 静态 IP（`192.168.77.10/.11`，幂等重应用）、ping、两个 launcher 存在。
- 输出 `RESULT|<lane>|PASS/FAIL/NA|<evidence>` 与汇总，结果同时写入 `$REPORT`（默认 `/tmp/xb_full_matrix.out`）。
- 不改设备上已部署的二进制；仅通过两个 launcher 驱动现有 FastDDS underlay + CycloneDDS overlay。

## 平台前提（关键）

OHOS/musl 上 RMW 必须**直链**，不能运行时 `dlopen` 选择（见 `CYCLONEDDS_MIGRATION_zh.md`、
内存 `ohos-musl-rmw-direct-link`）。因此：
- FastDDS 走 `/usr/local/bin/ros2`；
- CycloneDDS 走直链 overlay 启动器 `/data/local/tmp/ohos-cyc/ros2-cyclone`。
两种 DDS 都是 RTPS，因此**跨厂商 pub/sub 可互通**；而 **service/action 等请求-应答（RPC）跨厂商不互通**
（实测会挂起），故 interop 模式只跑 pub/sub 类车道。

## 测试结构

三个跨板模式（`ohos/tools/crossboard_lanes.sh`，服务/发布在 A 板，对端在 B 板，经 eth1）：

| 模式 | A 启动器 | B 启动器 | 车道集 |
|---|---|---|---|
| `fastdds` | ros2 | ros2 | 全部（pub/sub 类 + RPC 类） |
| `cyclonedds` | ros2-cyclone | ros2-cyclone | 全部 |
| `interop` | ros2 (FastDDS) | ros2-cyclone (CycloneDDS) | 仅 pub/sub 类 |

跨板车道（每条都验证消息真正过 eth1）：
- pub/sub C++（talker→listener，A→B 与 B→A 双向）
- pub/sub Python（`ros2 topic pub`→`echo`，rclpy 路径）
- 复杂消息类型（geometry_msgs/PoseStamped 嵌套字段）
- QoS：best-effort 订阅、reliable 订阅
- 序列化消息（serialized message）
- 内容过滤（content filter，本 RMW 构建未启用 CFT → NA）
- tf2（static_transform_publisher→tf2_echo）
- 服务 C++（add_two_ints，sum=5）
- 动作 C++（Fibonacci action）
- 生命周期（lifecycle 跨板 set/get）
- 参数（parameter_blackboard 跨板 set）
- CLI 图内省（node info / topic type / find / echo / hz / bw，从对端跨板查询）
- rosbag2（A 录制后 play，B 订阅收到回放）

每板本地（非分布式）功能（`ohos/tools/local_features.sh`，每板 × 两种 DDS）：
- 进程内组合（manual_composition）、组件容器（component load/list，class_loader/pluginlib）、
  wait set、日志、话题统计（topic statistics）、rclpy 回环（executor/pub/sub）、
  image/point-cloud transport、interface show、pkg list、doctor（确认 RMW 身份）。

## 域 ID 分配（避免串扰，均 ≤232）

| 组 | 域 |
|---|---|
| 跨板 fastdds | 100 |
| 跨板 cyclonedds | 112 |
| 跨板 interop | 124 |
| 本地 fastdds A / B | 130 / 140 |
| 本地 cyclonedds A / B | 150 / 160 |

## 判定

- PASS：对端确实收到/服务返回/期望输出出现。
- NA：该功能的 demo 二进制未部署，或该可选 DDS 特性在此 RMW 构建未启用（如 content-filtered topic、
  跨厂商 RPC）。NA 表示"非缺陷的不适用"，不计为失败。
- FAIL：应当工作却未工作 —— 需修复。

合格标准：所有 `fastdds_*` / `cyclonedds_*` / `interop_*` 跨板车道与 `local_*` 车道为 PASS 或 NA，
无 FAIL。
