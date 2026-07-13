#!/usr/bin/env python3

import argparse
import json
import math
import os
import re
import sys
import time

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import DurabilityPolicy, HistoryPolicy, QoSProfile, ReliabilityPolicy
    from std_msgs.msg import String
except ImportError:
    rclpy = None
    Node = object
    DurabilityPolicy = None
    HistoryPolicy = None
    QoSProfile = None
    ReliabilityPolicy = None
    String = None


class AckTracker:
    def __init__(self, run_id, phase):
        self.run_id = run_id
        self.phase = phase
        self.received_sequences = set()
        self.rtt_ms = []
        self.ignored_acks = 0
        self.last_received_ns = 0
        self._sent_ns = {}

    def arm(self, sequence, sent_ns):
        self._sent_ns[int(sequence)] = int(sent_ns)

    def record(self, data, received_ns):
        try:
            run_id, phase, sequence_text, sent_ns_text, _server_received_ns = data.split("|", 4)
            sequence = int(sequence_text)
            sent_ns = int(sent_ns_text)
        except (TypeError, ValueError):
            self.ignored_acks += 1
            return False
        if run_id != self.run_id or phase != self.phase or sequence in self.received_sequences:
            self.ignored_acks += 1
            return False
        if self._sent_ns.get(sequence) != sent_ns or received_ns < sent_ns:
            self.ignored_acks += 1
            return False
        self.received_sequences.add(sequence)
        self.rtt_ms.append((received_ns - sent_ns) / 1_000_000.0)
        self.last_received_ns = int(received_ns)
        return True


def percentile(samples, percent):
    if not samples:
        return 0.0
    ordered = sorted(float(sample) for sample in samples)
    rank = max(0.0, min(100.0, float(percent))) / 100.0 * (len(ordered) - 1)
    lower = math.floor(rank)
    upper = math.ceil(rank)
    if lower == upper:
        return ordered[lower]
    fraction = rank - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def case_counts(payload_size):
    if payload_size <= 1024:
        return 30, 200
    if payload_size <= 65536:
        return 20, 100
    if payload_size <= 1048576:
        return 10, 20
    return 5, 5


def throughput_window(payload_size):
    if payload_size <= 65536:
        return 32
    if payload_size <= 1048576:
        return 8
    return 2


def build_request(run_id, phase, sequence, sent_ns, payload_size):
    header = f"{run_id}|{phase}|{int(sequence)}|{int(sent_ns)}|"
    encoded_size = len(header.encode("utf-8"))
    if encoded_size > payload_size:
        raise ValueError(f"payload size {payload_size} is smaller than correlation header {encoded_size}")
    return header + ("x" * (payload_size - encoded_size))


def parse_request(data):
    run_id, phase, sequence_text, sent_ns_text, payload = data.split("|", 4)
    return run_id, phase, int(sequence_text), int(sent_ns_text), payload


def build_case_result(
    *,
    rmw,
    payload_size,
    latency_expected,
    latency_rtt_ms,
    throughput_expected,
    throughput_received,
    throughput_elapsed_sec,
    ignored_acks,
    publish_errors=0,
):
    latency_received = len(latency_rtt_ms)
    latency_missing = max(0, latency_expected - latency_received)
    throughput_missing = max(0, throughput_expected - throughput_received)
    elapsed = max(0.0, float(throughput_elapsed_sec))
    throughput_msg_s = throughput_received / elapsed if elapsed > 0.0 else 0.0
    throughput_mib_s = payload_size * throughput_msg_s / (1024.0 * 1024.0)
    latency_ms = [rtt / 2.0 for rtt in latency_rtt_ms]
    complete = (
        latency_expected > 0
        and latency_received == latency_expected
        and throughput_expected > 0
        and throughput_received == throughput_expected
        and elapsed > 0.0
        and publish_errors == 0
    )
    return {
        "rmw": rmw,
        "payload_size": payload_size,
        "latency_expected": latency_expected,
        "latency_received": latency_received,
        "latency_p50_ms": percentile(latency_ms, 50.0),
        "latency_p95_ms": percentile(latency_ms, 95.0),
        "latency_p99_ms": percentile(latency_ms, 99.0),
        "rtt_p95_ms": percentile(latency_rtt_ms, 95.0),
        "throughput_expected": throughput_expected,
        "throughput_received": throughput_received,
        "throughput_elapsed_sec": elapsed,
        "throughput_msg_s": throughput_msg_s,
        "throughput_mib_s": throughput_mib_s,
        "missing_acks": latency_missing + throughput_missing,
        "ignored_acks": ignored_acks,
        "publish_errors": publish_errors,
        "status": "PASS" if complete else "FAIL",
    }


def reliable_qos(depth=1024):
    return QoSProfile(
        history=HistoryPolicy.KEEP_LAST,
        depth=depth,
        reliability=ReliabilityPolicy.RELIABLE,
        durability=DurabilityPolicy.VOLATILE,
    )


def node_suffix(run_id):
    suffix = re.sub(r"[^A-Za-z0-9_]", "_", run_id)
    return suffix[-48:] or "run"


def parse_sizes(value):
    sizes = []
    for item in value.split(","):
        size = int(item.strip())
        if size < 64:
            raise ValueError("each payload size must be at least 64 bytes")
        if size not in sizes:
            sizes.append(size)
    if not sizes:
        raise ValueError("at least one payload size is required")
    return sizes


class PerfServer(Node):
    def __init__(self, run_id, topic_prefix):
        super().__init__(f"rmw_perf_server_{node_suffix(run_id)}")
        self.run_id = run_id
        self.stop_requested = False
        self.request_count = 0
        self.invalid_count = 0
        qos = reliable_qos()
        self.ack_publisher = self.create_publisher(String, f"{topic_prefix}/ack", qos)
        self.request_subscription = self.create_subscription(
            String, f"{topic_prefix}/request", self._on_request, qos
        )

    def _on_request(self, message):
        try:
            run_id, phase, sequence, sent_ns, _payload = parse_request(message.data)
        except (TypeError, ValueError):
            self.invalid_count += 1
            return
        if run_id != self.run_id:
            self.invalid_count += 1
            return
        received_ns = time.monotonic_ns()
        ack = String()
        ack.data = f"{run_id}|{phase}|{sequence}|{sent_ns}|{received_ns}"
        self.ack_publisher.publish(ack)
        self.request_count += 1
        if phase == "stop":
            self.stop_requested = True


class PerfClient(Node):
    def __init__(self, run_id, topic_prefix):
        super().__init__(f"rmw_perf_client_{node_suffix(run_id)}")
        self.run_id = run_id
        self.trackers = {}
        self.unrouted_acks = 0
        qos = reliable_qos()
        self.request_publisher = self.create_publisher(String, f"{topic_prefix}/request", qos)
        self.ack_subscription = self.create_subscription(String, f"{topic_prefix}/ack", self._on_ack, qos)

    def _on_ack(self, message):
        try:
            run_id, phase, _sequence, _sent_ns, _server_ns = message.data.split("|", 4)
        except (TypeError, ValueError):
            self.unrouted_acks += 1
            return
        tracker = self.trackers.get(phase)
        if run_id != self.run_id or tracker is None:
            self.unrouted_acks += 1
            return
        tracker.record(message.data, time.monotonic_ns())

    def wait_for_match(self, timeout_sec):
        deadline = time.monotonic() + timeout_sec
        while time.monotonic() < deadline:
            rclpy.spin_once(self, timeout_sec=0.1)
            if (
                self.request_publisher.get_subscription_count() > 0
                and self.ack_subscription.get_publisher_count() > 0
            ):
                return True
        return False

    def _publish(self, phase, sequence, payload_size, tracker):
        sent_ns = time.monotonic_ns()
        tracker.arm(sequence, sent_ns)
        message = String()
        message.data = build_request(self.run_id, phase, sequence, sent_ns, payload_size)
        self.request_publisher.publish(message)

    def _wait_for_count(self, tracker, expected, timeout_sec):
        deadline = time.monotonic() + timeout_sec
        while len(tracker.received_sequences) < expected and time.monotonic() < deadline:
            rclpy.spin_once(self, timeout_sec=0.05)
        return len(tracker.received_sequences) == expected

    def warmup(self, payload_size):
        phase = f"warmup-{payload_size}"
        tracker = AckTracker(self.run_id, phase)
        self.trackers[phase] = tracker
        for sequence in range(2):
            self._publish(phase, sequence, payload_size, tracker)
            if not self._wait_for_count(tracker, sequence + 1, max(5.0, payload_size / 262144.0)):
                return False
        return True

    def run_case(self, rmw, payload_size):
        latency_expected, throughput_expected = case_counts(payload_size)
        publish_errors = 0

        latency_phase = f"latency-{payload_size}"
        latency_tracker = AckTracker(self.run_id, latency_phase)
        self.trackers[latency_phase] = latency_tracker
        per_message_timeout = max(5.0, payload_size / (1024.0 * 1024.0) * 6.0)
        for sequence in range(latency_expected):
            try:
                self._publish(latency_phase, sequence, payload_size, latency_tracker)
            except Exception as error:
                publish_errors += 1
                print(f"PERF_PUBLISH_ERROR|phase={latency_phase}|sequence={sequence}|error={error}", flush=True)
                break
            if not self._wait_for_count(latency_tracker, sequence + 1, per_message_timeout):
                break

        throughput_phase = f"throughput-{payload_size}"
        throughput_tracker = AckTracker(self.run_id, throughput_phase)
        self.trackers[throughput_phase] = throughput_tracker
        throughput_start_ns = time.monotonic_ns()
        throughput_timeout = max(
            10.0,
            payload_size * throughput_expected / (2.0 * 1024.0 * 1024.0) * 4.0,
        )
        throughput_deadline = time.monotonic() + throughput_timeout
        next_sequence = 0
        window = throughput_window(payload_size)
        while (
            len(throughput_tracker.received_sequences) < throughput_expected
            and time.monotonic() < throughput_deadline
            and publish_errors == 0
        ):
            in_flight = next_sequence - len(throughput_tracker.received_sequences)
            while next_sequence < throughput_expected and in_flight < window:
                try:
                    self._publish(throughput_phase, next_sequence, payload_size, throughput_tracker)
                except Exception as error:
                    publish_errors += 1
                    print(
                        f"PERF_PUBLISH_ERROR|phase={throughput_phase}|sequence={next_sequence}|error={error}",
                        flush=True,
                    )
                    break
                next_sequence += 1
                in_flight += 1
            rclpy.spin_once(self, timeout_sec=0.01)
        throughput_end_ns = throughput_tracker.last_received_ns or time.monotonic_ns()
        throughput_elapsed_sec = max(0.0, (throughput_end_ns - throughput_start_ns) / 1_000_000_000.0)

        return build_case_result(
            rmw=rmw,
            payload_size=payload_size,
            latency_expected=latency_expected,
            latency_rtt_ms=latency_tracker.rtt_ms,
            throughput_expected=throughput_expected,
            throughput_received=len(throughput_tracker.received_sequences),
            throughput_elapsed_sec=throughput_elapsed_sec,
            ignored_acks=(
                self.unrouted_acks + latency_tracker.ignored_acks + throughput_tracker.ignored_acks
            ),
            publish_errors=publish_errors,
        )

    def request_stop(self):
        tracker = AckTracker(self.run_id, "stop")
        self.trackers["stop"] = tracker
        self._publish("stop", 0, 128, tracker)
        self._wait_for_count(tracker, 1, 3.0)


def emit_case(result):
    print(
        "PERF_CASE|"
        f"rmw={result['rmw']}|size={result['payload_size']}|"
        f"latency={result['latency_received']}/{result['latency_expected']}|"
        f"p50_ms={result['latency_p50_ms']:.3f}|p95_ms={result['latency_p95_ms']:.3f}|"
        f"p99_ms={result['latency_p99_ms']:.3f}|"
        f"throughput={result['throughput_received']}/{result['throughput_expected']}|"
        f"throughput_msg_s={result['throughput_msg_s']:.3f}|"
        f"goodput_mib_s={result['throughput_mib_s']:.3f}|"
        f"missing_acks={result['missing_acks']}|publish_errors={result['publish_errors']}|"
        f"status={result['status']}",
        flush=True,
    )


def run_server(args):
    node = PerfServer(args.run_id, args.topic_prefix)
    print(f"PERF_SERVER_READY|run_id={args.run_id}|topic={args.topic_prefix}", flush=True)
    deadline = time.monotonic() + args.idle_timeout
    try:
        while not node.stop_requested and time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.1)
        status = "PASS" if node.stop_requested and node.invalid_count == 0 else "FAIL"
        print(
            f"PERF_SERVER_SUMMARY|run_id={args.run_id}|requests={node.request_count}|"
            f"invalid={node.invalid_count}|status={status}",
            flush=True,
        )
        return 0 if status == "PASS" else 1
    finally:
        node.destroy_node()


def run_client(args):
    node = PerfClient(args.run_id, args.topic_prefix)
    summary = {
        "schema": 1,
        "run_id": args.run_id,
        "rmw": args.rmw,
        "cases": [],
        "status": "FAIL",
    }
    try:
        if not node.wait_for_match(args.match_timeout):
            raise RuntimeError("request/ack endpoints did not match before timeout")
        for payload_size in parse_sizes(args.sizes):
            if not node.warmup(payload_size):
                raise RuntimeError(f"warmup failed for payload size {payload_size}")
            result = node.run_case(args.rmw, payload_size)
            summary["cases"].append(result)
            emit_case(result)
        summary["status"] = "PASS" if all(case["status"] == "PASS" for case in summary["cases"]) else "FAIL"
        node.request_stop()
    except Exception as error:
        summary["error"] = str(error)
        print(f"PERF_PROBE_ERROR|run_id={args.run_id}|error={error}", flush=True)
    finally:
        with open(args.output, "w", encoding="utf-8") as stream:
            json.dump(summary, stream, sort_keys=True, indent=2)
            stream.write("\n")
        print(
            f"PERF_PROBE_SUMMARY|run_id={args.run_id}|rmw={args.rmw}|"
            f"cases={len(summary['cases'])}|status={summary['status']}|json={args.output}",
            flush=True,
        )
        node.destroy_node()
    return 0 if summary["status"] == "PASS" else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("role", choices=("server", "client"))
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--topic-prefix", required=True)
    parser.add_argument("--rmw", default=os.environ.get("RMW_IMPLEMENTATION", "unknown"))
    parser.add_argument("--sizes", default="128,1024,65536,1048576,4194304")
    parser.add_argument("--output", default="/data/local/tmp/rmw_fullstack_perf_summary.json")
    parser.add_argument("--match-timeout", type=float, default=90.0)
    parser.add_argument("--idle-timeout", type=float, default=600.0)
    args = parser.parse_args()
    if rclpy is None:
        print("PERF_PROBE_ERROR|error=rclpy_or_std_msgs_unavailable", file=sys.stderr)
        return 2
    rclpy.init(args=None)
    try:
        return run_server(args) if args.role == "server" else run_client(args)
    finally:
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
