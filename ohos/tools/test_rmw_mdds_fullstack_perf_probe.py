#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import unittest


PROBE_PATH = Path(__file__).with_name("rmw_mdds_fullstack_perf_probe.py")
SPEC = importlib.util.spec_from_file_location("rmw_mdds_fullstack_perf_probe", PROBE_PATH)
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


class AckTrackerTest(unittest.TestCase):
    def test_accepts_only_exact_run_phase_sequence_once(self):
        tracker = PROBE.AckTracker("run-7", "latency")
        tracker.arm(3, 1_000_000)

        self.assertFalse(tracker.record("other|latency|3|1000000|1100000", 1_200_000))
        self.assertFalse(tracker.record("run-7|throughput|3|1000000|1100000", 1_200_000))
        self.assertTrue(tracker.record("run-7|latency|3|1000000|1100000", 1_200_000))
        self.assertFalse(tracker.record("run-7|latency|3|1000000|1100000", 1_300_000))

        self.assertEqual({3}, tracker.received_sequences)
        self.assertEqual([0.2], tracker.rtt_ms)
        self.assertEqual(3, tracker.ignored_acks)


class StatisticsTest(unittest.TestCase):
    def test_percentile_uses_linear_interpolation(self):
        self.assertEqual(3.0, PROBE.percentile([1.0, 2.0, 3.0, 4.0, 5.0], 50.0))
        self.assertAlmostEqual(4.8, PROBE.percentile([1.0, 2.0, 3.0, 4.0, 5.0], 95.0))

    def test_case_result_reports_missing_acks_and_strict_status(self):
        result = PROBE.build_case_result(
            rmw="rmw_mdds_cpp",
            payload_size=1024,
            latency_expected=4,
            latency_rtt_ms=[10.0, 20.0, 30.0],
            throughput_expected=200,
            throughput_received=199,
            throughput_elapsed_sec=1.0,
            ignored_acks=2,
        )

        self.assertEqual(2, result["missing_acks"])
        self.assertEqual(199.0, result["throughput_msg_s"])
        self.assertAlmostEqual(14.5, result["latency_p95_ms"])
        self.assertAlmostEqual(29.0, result["rtt_p95_ms"])
        self.assertEqual("FAIL", result["status"])

    def test_complete_case_passes(self):
        result = PROBE.build_case_result(
            rmw="rmw_mdds_cpp",
            payload_size=1024,
            latency_expected=3,
            latency_rtt_ms=[10.0, 12.0, 14.0],
            throughput_expected=200,
            throughput_received=200,
            throughput_elapsed_sec=1.0,
            ignored_acks=0,
        )

        self.assertEqual(0, result["missing_acks"])
        self.assertEqual("PASS", result["status"])
        self.assertAlmostEqual(0.1953125, result["throughput_mib_s"])


class WorkloadTest(unittest.TestCase):
    def test_case_counts_bound_large_payload_volume(self):
        self.assertEqual((30, 200), PROBE.case_counts(1024))
        self.assertEqual((20, 100), PROBE.case_counts(65536))
        self.assertEqual((10, 20), PROBE.case_counts(1048576))
        self.assertEqual((5, 5), PROBE.case_counts(4194304))

    def test_throughput_window_stays_below_fixed_history_watermarks(self):
        self.assertEqual(32, PROBE.throughput_window(1024))
        self.assertEqual(32, PROBE.throughput_window(65536))
        self.assertEqual(8, PROBE.throughput_window(1048576))
        self.assertEqual(2, PROBE.throughput_window(4194304))

    def test_request_round_trip_preserves_correlation_and_size(self):
        request = PROBE.build_request("run-a", "latency", 9, 123456, 128)
        parsed = PROBE.parse_request(request)

        self.assertEqual(("run-a", "latency", 9, 123456), parsed[:4])
        self.assertEqual(128, len(request.encode("utf-8")))


if __name__ == "__main__":
    unittest.main(verbosity=2)
