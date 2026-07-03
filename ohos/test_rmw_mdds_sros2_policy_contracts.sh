#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_BIN="${RMW_MDDS_TEST_PUBSUB_BIN:-${ROOT_DIR}/build/rmw_mdds_cpp/test_pubsub_inproc}"
LD_LIBRARY_PATH_VALUE="${ROOT_DIR}/build/rmw_mdds_cpp:${ROOT_DIR}/install/lib:${LD_LIBRARY_PATH:-}"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

[[ -x "${TEST_BIN}" ]] || fail "test binary is missing or not executable: ${TEST_BIN}"

set +e
OUTPUT="$(
  LD_LIBRARY_PATH="${LD_LIBRARY_PATH_VALUE}" "${TEST_BIN}" \
    --gtest_filter='RmwMddsPubSub.DISABLED_Sros2Policy*' \
    --gtest_also_run_disabled_tests 2>&1
)"
STATUS=$?
set -e

printf '%s\n' "${OUTPUT}"

grep -q "2 tests from RmwMddsPubSub" <<<"${OUTPUT}" || \
  fail "SROS2 policy contract did not execute both disabled tests"

[[ "${STATUS}" -eq 0 ]] || fail "SROS2 policy contract failed"

grep -q "\\[  PASSED  \\] 2 tests" <<<"${OUTPUT}" || \
  fail "SROS2 policy contract did not pass both positive and negative host tests"

echo "RESULT|rmw_mdds_sros2_policy_contracts|PASS"
echo "rmw_mdds_sros2_policy_contracts_ok"
