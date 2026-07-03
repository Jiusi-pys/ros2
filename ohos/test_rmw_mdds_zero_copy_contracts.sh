#!/usr/bin/env bash
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_FILE="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/src/rmw_unsupported.cpp"
FAILURES=0

record_failure() {
  echo "FAIL: $*" >&2
  FAILURES=$((FAILURES + 1))
}

extract_function_body() {
  local function_name="$1"
  awk -v fn="${function_name}" '
    $0 ~ fn "\\(" { in_body = 1 }
    in_body { print }
    in_body && /^}$/ { exit }
  ' "${SOURCE_FILE}"
}

require_body() {
  local function_name="$1"
  local body="$2"
  [[ -n "${body}" ]] || record_failure "${function_name} body not found"
}

require_absent_in_body() {
  local function_name="$1"
  local body="$2"
  local pattern="$3"
  local description="$4"
  if grep -Eq "${pattern}" <<<"${body}"; then
    record_failure "${function_name} still ${description}"
  fi
}

[[ -f "${SOURCE_FILE}" ]] || {
  echo "FAIL: missing ${SOURCE_FILE}" >&2
  exit 1
}

publish_body="$(extract_function_body "rmw_publish_loaned_message")"
require_body "rmw_publish_loaned_message" "${publish_body}"
if [[ -n "${publish_body}" ]]; then
  require_absent_in_body \
    "rmw_publish_loaned_message" "${publish_body}" \
    'EncodeMddsIntoBuffer' \
    "serializes ROS typed loaned messages into an MDDS payload buffer"
  require_absent_in_body \
    "rmw_publish_loaned_message" "${publish_body}" \
    'BorrowLoanedSample' \
    "borrows a second payload-only bridge loan as a copy fallback"
fi

take_body="$(extract_function_body "TryTakeBridgeLoanedMessage")"
require_body "TryTakeBridgeLoanedMessage" "${take_body}"
if [[ -n "${take_body}" ]]; then
  require_absent_in_body \
    "TryTakeBridgeLoanedMessage" "${take_body}" \
    'DecodeMdds' \
    "deserializes an MDDS payload into ROS loaned-message storage"
  require_absent_in_body \
    "TryTakeBridgeLoanedMessage" "${take_body}" \
    'AllocateMessage' \
    "allocates ROS-owned storage instead of exposing the bridge loan"
fi

loaned_common_body="$(extract_function_body "TakeLoanedCommon")"
require_body "TakeLoanedCommon" "${loaned_common_body}"
if [[ -n "${loaned_common_body}" ]]; then
  require_absent_in_body \
    "TakeLoanedCommon" "${loaned_common_body}" \
    'AllocateMessage' \
    "falls back to ROS-owned loaned-message allocation"
fi

if [[ "${FAILURES}" -ne 0 ]]; then
  echo "rmw_mdds_zero_copy_contracts_failed count=${FAILURES}" >&2
  exit 1
fi

echo "RESULT|rmw_mdds_zero_copy_contracts|PASS"
echo "rmw_mdds_zero_copy_contracts_ok"
