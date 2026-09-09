# Two-board DDS comparison execution contract

User-approved scope: Fast DDS and Cyclone DDS; BEST_EFFORT and
RELIABLE; A->B, B->A and simultaneous bidirectional traffic; 1 KiB through
4 MiB payloads, including the boundary profile.

Implementation: independent C++ rclcpp executable using UInt8MultiArray, identical
application for all RMWs; Python host planner/reporter and board process/resource
supervisor. No middleware changes. Deploy in a separate hash-identified directory,
using the existing /data/local/tmp/ros2 runtime. Preserve the runtime and source baseline.

Gates: host RED tests -> statistics/planner GREEN; native wire/validation contract
RED -> cross-build GREEN -> two-board deployment SHA check -> 2 RMW x 2 QoS
smoke (both latency and stream) -> boundary capability probes. Full repetitions,
soak, slow receiver and load ladders are explicit selectable plans, not implied
by smoke acceptance. Physical cable interruption remains a separately scheduled
manual scenario; process restart can be tested without losing HDC.

Statistics: nearest-rank p1/p50/p95/p99/max, units microseconds. RTT uses only
sender steady_clock; never claim synchronized one-way latency. RTT includes
echo-side payload checksum work, serialization, scheduling and transport.
Record raw sample events, warmup, attempts, publish errors, timeouts, duplicates,
out-of-order arrivals and checksum failures. Throughput is receiver validated
payload bytes / explicit measurement window; wire bytes are interface counters
and may include system traffic. Percentiles are null with zero valid samples.

Bounded execution: max payload 4 MiB, max samples 1 million, outer wall-clock
deadline, per-board memory monitoring (application), process
group cleanup, unique namespaces and run directories, deployment activity locks.
Boundary failures are results, never silently skipped or converted to successes.
1 MiB data can exceed a nominal 1 MiB transport envelope because application/CDR
headers add bytes. Payload length excludes the 40-byte benchmark header.

Formal settings: depth 10 (large boundary depth 1 explicitly reported), volatile,
3 repetitions with rotating RMW order; 10,000 requested measured RTT samples,
sample shortfall disclosed. Record build/runtime hashes, board serials, QoS,
payload, rate, direction, temperature, CPU and memory evidence for each case.
