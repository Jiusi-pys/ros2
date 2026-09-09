# 两种 DDS 双板测试工具

同一份 C++ `rclcpp` 应用，在已部署的 ROS 2 运行环境中切换
`rmw_fastrtps_cpp` / `rmw_cyclonedds_cpp`。
测试二进制独立部署于 `/data/local/tmp/dds-bench-<内容哈希>/`，不覆盖 ROS 运行目录。

## 已实现

- BEST_EFFORT / RELIABLE；VOLATILE；队列深度可比较 1、10、100。
- A→B、B→A、同时双向，独立话题、独立运行标识；每板支持单进程双向收发。
- RTT ping/pong：同一个发送板的 steady_clock；正式样本与预热分开。
- 单向持续发送/接收：限速 10、100、1000 条/s 及不主动限速的负载阶梯。
- RTT 和 publish 调用耗时的 p1、p50、p95、p99、最大值、样本数；nearest-rank 算法。
- 成功率、超时、发送异常、内容完整性、重复、乱序、迟到、截止时未收到的序号数。
- 250 ms 周期采集应用的 RSS/CPU、可用内存、温度、各网卡字节/包计数。
- 接收进程重启与恢复观测、20 ms 慢接收、30 分钟持续运行配置。
- 最大 4 MiB 单条消息的内存预检、运行时资源阈值、外层进程超时及精确进程组清理。
- 原始 JSONL、JSON 汇总、CSV、可离线打开的 HTML 报告。

## 一键使用（Windows PowerShell）

在 `C:\Users\17715\Documents\codes\M-DDS\ros2` 下：

```powershell
# 编译并运行主机契约测试
./scripts/dds_bench/build.ps1

# 只部署：两板校验 SHA256，并运行原生报文契约测试
./.pixi/envs/default/python.exe scripts/dds_bench/run.py deploy

# 8 项：两 RMW × 两 QoS × RTT/持续发送，1 KiB
./.pixi/envs/default/python.exe scripts/dds_bench/run.py run --profile smoke

# 相同验收，双端同时发送；改成 ba 可单独测 B→A
./.pixi/envs/default/python.exe scripts/dds_bench/run.py run --profile smoke --direction both
```

执行时两块板需空闲。工具使用 `/data/local/tmp/ros2/.dds-bench-activity-lock`，遇到已有占用
不会抢占。建议不要在测试期间运行其他话题、旧验收任务或修改部署文件。
后台板端 supervisor 即使失去主机也有最长运行时间；若不能证明清理完成，保留锁并报错。

## 正式矩阵及预计规模

默认 `smoke` 是工具验收，不是完整性能结论。下面的正式矩阵可能运行数小时至数天，
先用 `plan` 导出明确的配置清单，再执行 `run`。可用 `--only-rmw`、`--only-bytes`、
`--min-bytes`、`--mode`、`--limit` 缩小批次；`--direction` 覆盖方向。

| profile | 用途 | 默认规模 |
|---|---|---|
| latency | 1/4/16/32/64/128/256/512 KiB、1/4 MiB；3 个方向、3 次重复 | 360 项；每项最多 60 秒发起测量，目标 10,000 样本 |
| throughput | 同上消息大小；10/100/1000/不限速；3 个方向、3 次重复 | 1440 项；每项 60 秒发送 |
| boundary | 1 KiB～4 MiB 单条消息、depth=1 | 40 项；各一条，无预热，回包等待最多 30 秒 |
| slow | 1 KiB/64 KiB/1 MiB；depth=1/10/100；接收处理延迟 20 ms | 108 项；每项 60 秒 |
| soak | 1 KiB/64 KiB/1 MiB | 12 项；每项持续 30 分钟 |
| restart | 发送持续进行时，接收进程 5 秒后重启一次 | 4 项；每项 60 秒 |

```powershell
./.pixi/envs/default/python.exe scripts/dds_bench/run.py plan --profile latency
./.pixi/envs/default/python.exe scripts/dds_bench/run.py run --profile latency --only-rmw rmw_fastrtps_cpp --only-bytes 65536
./.pixi/envs/default/python.exe scripts/dds_bench/run.py run --profile boundary

# 大消息测试结束后的两 RMW × 两 QoS 小消息恢复验证
./.pixi/envs/default/python.exe scripts/dds_bench/run.py run --profile smoke --mode latency
```

1 KiB = 1024 字节；1 MiB = 1,048,576 字节。
`bytes` 指有效随机载荷，不含额外 40 字节测试头及 ROS/CDR/中间件头。
数据由固定运行种子生成，每条携带序号；接收端全量校验 FNV-1a 内容摘要（用于传输差错检查，非密码学认证）。
接收校验和回显发布的开销包含在应用 RTT 中。发送端接到回包时先取时间再校验。
测试复用单线程 executor，轮询间隔为 50 μs；实际调度唤醒可能更慢，
RTT 包含这部分应用调度开销，不是裸网卡或纯 DDS 内核延迟。
任何结果均不是同步单向延迟，不自动除以二称为单向延迟。

## 如何阅读结果

输出默认在父目录的 `verification_evidence/dds_bench_<profile>_<时间>/`：

- `deployment.json`：测试程序哈希、板卡身份、运行库哈希。
- `plan.json`：每一项的大小、QoS、深度、速率、方向、超时、重复号。
- `case_XXXX/`：两端原始样本、进程日志、资源采样、退出状态与 `result.json`。
- `results.json`：全部病例，失败/能力边界同样保存。
- `latency.csv`、`throughput.csv`、`report.html`：便于汇报与后续绘图。

`VALID` 表示本项基础收发与完整性检查完成，不代表无丢包、满足性能指标或通过全部验收。
BEST_EFFORT 和 RELIABLE 都报告实际交付率。publish 成功仅表示本次调用成功，不能当作
对端收到。持续发送结束后额外等待 2 秒，之后仍未收到的数据记录为 `missing_at_cutoff`，
不宣称永久丢失。接收速率使用首末有效到达时间跨度，扣除第一个样本的字节数；
小样本的瞬时速率不应作为容量结论。

超时不混入成功样本分位数，也不会消失；发布失败不混入成功 RTT。
目标样本不足、成功 RTT 少于 10,000 会标注。预热占用总时限，慢组合会产生样本不足；
不要把 30 个 smoke 样本的 p99 用作正式排名。
持续发送还有每进程 1,000,000 条的资源保护上限；若先达到该上限，记录实际测量时间
和 `duration_shortfall_seconds`，不能将其写成已持续完整目标时长。

CPU 100% 代表一个核心，按 supervisor 整段运行时间计算，包含启动与排空阶段。
CPU 时间来自已回收子进程的 getrusage；RSS 是采样峰值，可能漏过短暂峰值，不含内核缓冲区；
另记录 getrusage 的最大单个子进程 RSS（不是多进程同时 RSS 之和）。
同时记录整机 CPU 忙碌率（100% 表示所有核心）及共享 softbus_server 的 CPU/RSS 观测。
共享服务只读采样，绝不终止；其负载可能来自其他系统活动，不自动全部归因于本轮测试。
同时记录可用内存下降，双向测试同时统计两条流。
网卡计数是整块板上的接口流量，可能包含 HDC、系统服务等，不能全归因于 DDS。
应用逐条记录写入本地 JSONL，记录开销会影响吞吐上限；比较时保持两种实现相同设置。
默认要求可用内存 >= 5 × payload + 256 MiB，运行期间总 RSS 上限 3 GiB、
可用内存下限 256 MiB；触发时保留明确的资源限制结果，不修改中间件上限绕过失败。

## 故障与公平性

工具自动化的是接收进程重启；物理断链/拔网线并未自动执行。
如需测试链路中断，应单独约定时间，保持 USB HDC 控制可用；不要把进程重启当成断链验证。
记录并尽量保持交换网络、网口、CPU governor、温度和后台负载一致。
全矩阵按重复号轮换两种 RMW 顺序，减轻固定先后顺序影响。
工具为 Fast DDS 创建只允许 eth1 IPv4 的 UDPv4 transport profile，为 Cyclone 创建 eth1 配置；
这些 XML 每轮随证据保存，通过接口流量确认实际链路。当前既有运行环境的 Cyclone SHM 关闭。
早期未固定网口的工具验收发现 Fast DDS 同时向 Wi-Fi 发包，该批记录保留为自动选路基线，
不能与固定 eth1 的批次混合做公平性能排名。

代码位置均在本目录，不改动 RMW 实现。测试计划与验收口径见 PLAN.md。
