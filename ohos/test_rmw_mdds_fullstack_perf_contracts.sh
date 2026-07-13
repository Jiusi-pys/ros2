#!/usr/bin/env bash

set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE="${ROOT_DIR}/ohos/tools/rmw_mdds_fullstack_perf_probe.py"
PROBE_TEST="${ROOT_DIR}/ohos/tools/test_rmw_mdds_fullstack_perf_probe.py"
RUNNER="${ROOT_DIR}/ohos/tools/run_cross_board_rmw_mdds_fullstack_perf.sh"
FAILURES=0

fail_contract() {
  echo "RESULT|rmw_mdds_fullstack_perf_contracts|RED|$*" >&2
  FAILURES=$((FAILURES + 1))
}

require_file() {
  local path="$1"
  [[ -f "${path}" ]] || fail_contract "missing_file=${path#${ROOT_DIR}/}"
}

require_pattern() {
  local path="$1"
  local pattern="$2"
  local label="$3"
  if [[ ! -f "${path}" ]] || ! grep -qE -- "${pattern}" "${path}"; then
    fail_contract "missing_capability=${label}"
  fi
}

require_absent_pattern() {
  local path="$1"
  local pattern="$2"
  local label="$3"
  if [[ -f "${path}" ]] && grep -qE -- "${pattern}" "${path}"; then
    fail_contract "forbidden_capability=${label}"
  fi
}

require_file "${PROBE}"
require_file "${PROBE_TEST}"
require_file "${RUNNER}"

require_pattern "${PROBE}" 'import rclpy' "probe_uses_rclpy"
require_pattern "${PROBE}" 'from std_msgs\.msg import String' "probe_uses_standard_ros_message"
require_pattern "${PROBE}" 'run_id.*phase.*sequence' "probe_correlates_run_phase_sequence"
require_pattern "${PROBE}" 'PERF_CASE\|' "probe_emits_case_marker"
require_pattern "${PROBE}" 'PERF_PROBE_SUMMARY\|' "probe_emits_summary_marker"
require_pattern "${PROBE}" 'json\.dump' "probe_writes_board_json"
require_pattern "${PROBE}" 'latency_p95_ms' "probe_reports_latency_p95"
require_pattern "${PROBE}" 'throughput_msg_s' "probe_reports_throughput"
require_pattern "${PROBE}" 'missing_acks' "probe_reports_missing_acks"

require_pattern "${RUNNER}" 'RMW_IMPLEMENTATION=.rmw_mdds_cpp' "runner_runs_rmw_mdds"
require_pattern "${RUNNER}" 'RMW_IMPLEMENTATION=.rmw_fastrtps_cpp' "runner_runs_fastrtps"
require_pattern "${RUNNER}" 'MDDS_RC=' "runner_retains_mdds_failure"
require_pattern "${RUNNER}" 'FASTRTPS_RC=' "runner_always_runs_fastrtps"
require_pattern "${RUNNER}" 'unset LD_PRELOAD' "runner_uses_runtime_selected_rmw"
require_pattern "${RUNNER}" 'RMW_MDDS_PERF_MAX_P95_MS:-50' "runner_enforces_design_latency"
require_pattern "${RUNNER}" 'RMW_MDDS_PERF_MIN_MSG_S:-100' "runner_enforces_design_throughput"
require_pattern "${RUNNER}" 'missing_acks.*0' "runner_requires_zero_reliable_loss"
require_pattern "${RUNNER}" 'RESULT\|rmw_mdds_fullstack_perf\|PASS' "runner_emits_final_pass"
require_pattern "${RUNNER}" 'cleanup' "runner_has_cleanup"
require_pattern "${RUNNER}" 'cleanup_all_mdds_brokers' "runner_isolates_stale_brokers"
require_absent_pattern "${RUNNER}" '3e01ff[0-9a-f]+' "hardcoded_device_id"
require_absent_pattern "${RUNNER}" '192\.168\.[0-9]+\.[0-9]+' "hardcoded_lab_address"

if [[ -f "${RUNNER}" ]]; then
  domain_output="$(mktemp)"
  set +e
  timeout -k 1 2 env HDC_BIN=/bin/false HDC_RETRY_ATTEMPTS=1 \
    "${RUNNER}" client-device server-device 1 233 >"${domain_output}" 2>&1
  domain_status=$?
  set -e
  if [[ "${domain_status}" -ne 2 ]] ||
      ! grep -q 'domain.*0\.\.232' "${domain_output}"; then
    fail_contract "runner_does_not_reject_invalid_fastdds_domain"
  fi
  rm -f "${domain_output}"
fi

if [[ -f "${PROBE_TEST}" ]]; then
  if ! python3 "${PROBE_TEST}"; then
    fail_contract "probe_unit_tests_failed"
  fi
fi

if [[ "${FAILURES}" -ne 0 ]]; then
  echo "rmw_mdds_fullstack_perf_contracts_failed count=${FAILURES}" >&2
  exit 1
fi

echo "RESULT|rmw_mdds_fullstack_perf_contracts|PASS"
echo "rmw_mdds_fullstack_perf_contracts_ok"
