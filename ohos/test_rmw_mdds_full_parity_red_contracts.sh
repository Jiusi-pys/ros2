#!/usr/bin/env bash
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LD_LIBRARY_PATH_VALUE="${ROOT_DIR}/build/rmw_mdds_cpp:${ROOT_DIR}/install/lib:${LD_LIBRARY_PATH:-}"
FAILURES=0

run_gate() {
  local label="$1"
  local binary="$2"
  local filter="$3"
  local output status

  if [[ ! -x "${binary}" ]]; then
    echo "RESULT|${label}|RED|missing_test_binary=${binary}" >&2
    FAILURES=$((FAILURES + 1))
    return
  fi

  output="$(
    LD_LIBRARY_PATH="${LD_LIBRARY_PATH_VALUE}" "${binary}" \
      --gtest_filter="${filter}" --gtest_also_run_disabled_tests 2>&1
  )"
  status=$?
  printf '%s\n' "${output}"
  if [[ "${status}" -eq 0 ]]; then
    echo "RESULT|${label}|PASS"
  else
    echo "RESULT|${label}|RED|status=${status}"
    FAILURES=$((FAILURES + 1))
  fi
}

run_gate \
  "rmw_mdds_full_parity_loaned_shapes" \
  "${ROOT_DIR}/build/rmw_mdds_cpp/test_bridge_loaned_rmw" \
  "RmwMddsBridgeLoanedRmw.DISABLED_FullParityLoaned*"

run_gate \
  "rmw_mdds_full_parity_signed_security" \
  "${ROOT_DIR}/build/rmw_mdds_cpp/test_pubsub_inproc" \
  "RmwMddsPubSub.DISABLED_FullParitySros2RejectsTamperedUnsignedPermissions"

run_gate \
  "rmw_mdds_full_parity_broker_network_flow" \
  "${ROOT_DIR}/build/rmw_mdds_cpp/test_broker_mode" \
  "RmwMddsBrokerMode.DISABLED_FullParityBrokerModeNetworkFlowEndpointsReportMddsTransport"

if [[ "${FAILURES}" -ne 0 ]]; then
  echo "rmw_mdds_full_parity_red_contracts_failed count=${FAILURES}" >&2
  exit 1
fi

echo "rmw_mdds_full_parity_red_contracts_ok"
