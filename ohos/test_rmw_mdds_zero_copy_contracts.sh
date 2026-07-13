#!/usr/bin/env bash
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_FILE="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/src/rmw_unsupported.cpp"
PROTOCOL_SOURCE="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/src/ipc_protocol.cpp"
LOAN_POOL_SOURCE="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/src/ipc_loan_pool.cpp"
BROKER_SOURCE="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/src/broker.cpp"
BROKER_TEST="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/test/test_broker_mode.cpp"
BROKER_PROCESS_TEST="${ROOT_DIR}/src/ros2/rmw_mdds/rmw_mdds_cpp/test/test_broker_process.cpp"
FAILURES=0

record_failure() {
  echo "FAIL: $*" >&2
  FAILURES=$((FAILURES + 1))
}

extract_function_body() {
  local function_name="$1"
  local source_file="${2:-${SOURCE_FILE}}"
  awk -v fn="${function_name}" '
    $0 ~ fn "\\(" { in_body = 1 }
    in_body { print }
    in_body && /^}$/ { exit }
  ' "${source_file}"
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

require_present_in_body() {
  local function_name="$1"
  local body="$2"
  local pattern="$3"
  local description="$4"
  if ! grep -Eq "${pattern}" <<<"${body}"; then
    record_failure "${function_name} does not ${description}"
  fi
}

[[ -f "${SOURCE_FILE}" ]] || {
  echo "FAIL: missing ${SOURCE_FILE}" >&2
  exit 1
}
[[ -f "${PROTOCOL_SOURCE}" ]] || record_failure "missing ${PROTOCOL_SOURCE}"
[[ -f "${LOAN_POOL_SOURCE}" ]] || record_failure "missing ${LOAN_POOL_SOURCE}"
[[ -f "${BROKER_SOURCE}" ]] || record_failure "missing ${BROKER_SOURCE}"
[[ -f "${BROKER_TEST}" ]] || record_failure "missing ${BROKER_TEST}"
[[ -f "${BROKER_PROCESS_TEST}" ]] || record_failure "missing ${BROKER_PROCESS_TEST}"

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

broker_take_body="$(extract_function_body "TryTakeBrokerLoanedMessage")"
require_body "TryTakeBrokerLoanedMessage" "${broker_take_body}"
if [[ -n "${broker_take_body}" ]]; then
  require_absent_in_body \
    "TryTakeBrokerLoanedMessage" "${broker_take_body}" \
    'DecodeMdds|adapter\.Decode|AllocateMessage|memcpy' \
    "copies or deserializes broker pool storage before returning the loan"
fi

broker_descriptor_body="$(extract_function_body "EncodeLoanedSampleMessage" "${PROTOCOL_SOURCE}")"
require_body "EncodeLoanedSampleMessage" "${broker_descriptor_body}"
if [[ -n "${broker_descriptor_body}" ]]; then
  require_absent_in_body \
    "EncodeLoanedSampleMessage" "${broker_descriptor_body}" \
    'sample\.payload[^_]|insert\(' \
    "embeds sample payload bytes in the broker loan descriptor frame"
fi

mapping_body="$(extract_function_body "LoanPoolMapping::Open" "${LOAN_POOL_SOURCE}")"
require_body "LoanPoolMapping::Open" "${mapping_body}"
if [[ -n "${mapping_body}" ]] && ! grep -Eq 'mmap\([^;]*PROT_READ[^;]*MAP_SHARED' <<<"${mapping_body}"; then
  record_failure "LoanPoolMapping::Open does not map broker payload storage read-only"
fi
if [[ -n "${mapping_body}" ]]; then
  require_present_in_body \
    "LoanPoolMapping::Open" "${mapping_body}" \
    'mprotect\([^;]*PROT_READ[[:space:]]*\|[[:space:]]*PROT_WRITE' \
    "make only negotiated typed-arena pages writable"
fi

dynamic_construct_body="$(extract_function_body "ConstructBrokerLoanedMessage" "${BROKER_SOURCE}")"
require_body "ConstructBrokerLoanedMessage" "${dynamic_construct_body}"
if [[ -n "${dynamic_construct_body}" ]]; then
  require_present_in_body \
    "ConstructBrokerLoanedMessage" "${dynamic_construct_body}" \
    'MddsLoanMemoryResource' \
    "bind generated dynamic allocations to the mapped typed arena"
  require_present_in_body \
    "ConstructBrokerLoanedMessage" "${dynamic_construct_body}" \
    'ConstructMessageInPlace' \
    "construct the generated ROS object in mapped storage"
  require_present_in_body \
    "ConstructBrokerLoanedMessage" "${dynamic_construct_body}" \
    'DynamicStorageWithinLoan' \
    "reject decoded object graphs that escape the mapped arena"
  require_absent_in_body \
    "ConstructBrokerLoanedMessage" "${dynamic_construct_body}" \
    'AllocateMessage\(' \
    "uses heap-backed typed message allocation"
fi

for test_name in \
  BrokerDynamicStringLoanedTakeUsesMappedArenaAndReturnsSlot \
  BrokerDynamicSequenceNestedLoanedTakeUsesMappedArena \
  BrokerDynamicFilteredLoanRejectsBeforeWaitVisibility \
  BrokerTransientLocalReplayHonorsInitialDynamicFilter; do
  if ! grep -Fq "${test_name}" "${BROKER_TEST}"; then
    record_failure "missing dynamic broker loan contract test ${test_name}"
  fi
done
if ! grep -Eq 'MappingPathForAddress\(received->data\.data\(\)\).*rmw_mdds_loan_' "${BROKER_TEST}"; then
  record_failure "dynamic String contract does not prove character storage uses the broker mapping"
fi
if ! grep -Eq 'MappingPathForAddress\(received->layout\.dim\.data\(\)\).*rmw_mdds_loan_' "${BROKER_TEST}"; then
  record_failure "dynamic nested contract does not prove sequence storage uses the broker mapping"
fi
if ! grep -Fq 'RoutesLoanedStringSampleBetweenSeparateRmwProcesses' "${BROKER_PROCESS_TEST}"; then
  record_failure "missing cross-process dynamic String broker loan test"
fi
if ! grep -Fq -- '--loaned-string-subscriber' "${BROKER_PROCESS_TEST}"; then
  record_failure "cross-process dynamic String test does not use a loaned subscriber child"
fi
if ! grep -Fq 'AddressUsesBrokerLoanMapping(message->data.data())' "${BROKER_PROCESS_TEST}"; then
  record_failure "cross-process dynamic String test does not prove character storage uses the broker mapping"
fi

if [[ "${FAILURES}" -ne 0 ]]; then
  echo "rmw_mdds_zero_copy_contracts_failed count=${FAILURES}" >&2
  exit 1
fi

echo "RESULT|rmw_mdds_zero_copy_contracts|PASS"
echo "rmw_mdds_zero_copy_contracts_ok"
