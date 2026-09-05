#!/usr/bin/env bash
# Run the one official FastDDS test that is commonly cited as an external
# baseline failure, and emit a narrow, machine-readable evidence record.
#
# This is intentionally not an RMW-suite runner.  It selects exactly
# test_subscription__rmw_fastrtps_cpp from test_rmw_implementation, delegates
# board setup/evidence capture to run_board_tests.sh, and records the raw test
# result as PASS, FAIL, EXEMPTION, or BLOCKED.  An EXEMPTION is never a PASS and
# never means that the overall official RMW suite is green.
#
# Usage:
#   ./scripts/run_fastrtps_baseline_gate.sh [A|B|board_serial] \
#       [--exemption-file path/to/exemption.record]
#
# Exit status: 0 PASS; 1 actual test FAIL; 2 BLOCKED/unproven evidence;
#              3 named EXEMPTION (intentional non-pass).
set -uo pipefail

cd "$(dirname "$0")/.."

BOARD_A=3e01ff55454d202020104033bf453b00
BOARD_B=3e01ff55454d202020104433991c3b00
PACKAGE=test_rmw_implementation
CTEST_SELECTOR=test_subscription__rmw_fastrtps_cpp
RMW_IMPLEMENTATION=rmw_fastrtps_cpp

usage() {
  cat <<'EOF'
Usage: ./scripts/run_fastrtps_baseline_gate.sh [A|B|board_serial] [--exemption-file FILE]

Runs exactly test_rmw_implementation/test_subscription__rmw_fastrtps_cpp on
the selected board using rmw_fastrtps_cpp.  The result record is written below
FASTDDS_BASELINE_LOGROOT (default: ohos_test_logs/fastdds_baseline).
EOF
}

safe_component() {
  [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ ]]
}

regular_file() {
  [[ -f "$1" && ! -L "$1" ]]
}

sha256_file() {
  sha256sum "$1" | awk '{print $1}'
}

byte_count() {
  wc -c < "$1" | tr -d '[:space:]'
}

line_count_exact() {
  local file="$1" expected="$2" count
  count="$(LC_ALL=C grep -Fxc -- "$expected" "$file" 2>/dev/null || true)"
  [[ "$count" =~ ^[0-9]+$ ]] || return 1
  printf '%s\n' "$count"
}

one_matching_line() {
  local file="$1" expression="$2" count
  count="$(LC_ALL=C grep -Ec -- "$expression" "$file" 2>/dev/null || true)"
  [[ "$count" == "1" ]]
}

BOARD="$BOARD_A"
EXEMPTION_FILE="${FASTDDS_BASELINE_EXEMPTION_FILE:-}"
board_set=0
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --exemption-file)
      [ $# -ge 2 ] || { echo "ERROR: --exemption-file requires a path" >&2; exit 2; }
      EXEMPTION_FILE="$2"
      shift 2
      ;;
    --*)
      echo "ERROR: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [ "$board_set" -ne 0 ]; then
        echo "ERROR: provide at most one board target" >&2
        exit 2
      fi
      case "$1" in
        A|a) BOARD="$BOARD_A" ;;
        B|b) BOARD="$BOARD_B" ;;
        *) BOARD="$1" ;;
      esac
      board_set=1
      shift
      ;;
  esac
done

if ! safe_component "$BOARD"; then
  echo "ERROR: board target must contain only A-Za-z0-9_.-" >&2
  exit 2
fi

RUN_ID="${MDDS_RUN_ID:-fastdds_baseline_$(date -u +%Y%m%dT%H%M%SZ)_$RANDOM}"
RUN_NONCE="${MDDS_FASTDDS_BASELINE_NONCE:-fastdds_${RANDOM}_${RANDOM}_$$}"
LOGROOT="${FASTDDS_BASELINE_LOGROOT:-ohos_test_logs/fastdds_baseline}"
if ! safe_component "$RUN_ID" || ! safe_component "$RUN_NONCE"; then
  echo "ERROR: run ID and nonce must contain only A-Za-z0-9_.-" >&2
  exit 2
fi
if [[ -z "$LOGROOT" ]]; then
  echo "ERROR: FASTDDS_BASELINE_LOGROOT must not be empty" >&2
  exit 2
fi

GATE_DIR="$LOGROOT/$RUN_ID/$RUN_NONCE"
if [[ -e "$GATE_DIR" || -L "$GATE_DIR" ]]; then
  echo "ERROR: refusing to reuse existing baseline-gate directory: $GATE_DIR" >&2
  exit 2
fi
if ! (umask 077; mkdir -p "$GATE_DIR"); then
  echo "ERROR: cannot create baseline-gate directory: $GATE_DIR" >&2
  exit 2
fi

RUNNER_LOGROOT="$GATE_DIR/board_runner"
RUNNER_STDOUT="$GATE_DIR/run_board_tests.stdout"
RUNNER_STDERR="$GATE_DIR/run_board_tests.stderr"
RESULT_RECORD="$GATE_DIR/fastdds_baseline_gate.record"

actual_test_result=UNOBSERVED
result=BLOCKED
reason=RUNNER_NOT_STARTED
runner_rc=UNOBSERVED
exemption_status=NOT_SUPPLIED
exemption_id=NONE
exemption_sha256=NONE
raw_archive_sha256=NONE
raw_archive_bytes=NONE
driver_stdout_sha256=NONE
run_txt_sha256=NONE
runner_stdout_sha256=NONE
runner_stderr_sha256=NONE
driver_archive_rmw_binding=UNVERIFIED

# The generic board runner retains the same board environment and evidence
# archive protocol as the official RMW suite.  Its selector is strict: a typo
# is an error, never a fall-through to a broader package run.
(
  MDDS_RUN_ID="$RUN_ID" \
  MDDS_BOARDTEST_RUN_NONCE="$RUN_NONCE" \
  MDDS_BOARDTEST_LOGROOT="$RUNNER_LOGROOT" \
  MDDS_BOARDTEST_ONLY_TEST="$CTEST_SELECTOR" \
  ./scripts/run_board_tests.sh "$BOARD" "$PACKAGE"
) > "$RUNNER_STDOUT" 2> "$RUNNER_STDERR"
runner_rc=$?

if regular_file "$RUNNER_STDOUT"; then
  runner_stdout_sha256="$(sha256_file "$RUNNER_STDOUT")"
fi
if regular_file "$RUNNER_STDERR"; then
  runner_stderr_sha256="$(sha256_file "$RUNNER_STDERR")"
fi

RUNNER_EVIDENCE_DIR="$RUNNER_LOGROOT/$RUN_ID/$RUN_NONCE/board_$BOARD"
RUN_TXT="$RUNNER_EVIDENCE_DIR/run.txt"
DRIVER_STDOUT="$RUNNER_EVIDENCE_DIR/$PACKAGE.driver.stdout"
RAW_ARCHIVE="$RUNNER_EVIDENCE_DIR/$PACKAGE.remote.tar"

provenance_ok=1
if ! regular_file "$RUN_TXT"; then
  provenance_ok=0
  reason=MISSING_RUN_RECORD
elif ! regular_file "$DRIVER_STDOUT"; then
  provenance_ok=0
  reason=MISSING_DRIVER_STDOUT
elif ! regular_file "$RAW_ARCHIVE"; then
  provenance_ok=0
  reason=MISSING_RAW_ARCHIVE
fi

if [ "$provenance_ok" -eq 1 ]; then
  run_txt_sha256="$(sha256_file "$RUN_TXT")"
  driver_stdout_sha256="$(sha256_file "$DRIVER_STDOUT")"
  raw_archive_sha256="$(sha256_file "$RAW_ARCHIVE")"
  raw_archive_bytes="$(byte_count "$RAW_ARCHIVE")"

  for required_line in \
    "BOARDTEST_RUN_ID=$RUN_ID" \
    "BOARDTEST_BOARD=$BOARD" \
    "BOARDTEST_RUN_NONCE=$RUN_NONCE" \
    "BOARDTEST_ONLY_TEST=$CTEST_SELECTOR" \
    "BOARDTEST_SELECTION package=$PACKAGE selector=$CTEST_SELECTOR"; do
    if [[ "$(line_count_exact "$RUN_TXT" "$required_line")" != "1" ]]; then
      provenance_ok=0
      reason=RUN_RECORD_BINDING_MISMATCH
      break
    fi
  done
fi

if [ "$provenance_ok" -eq 1 ] && ! one_matching_line "$RUN_TXT" \
  "^BOARDTEST_DRIVER_TERMINAL package=$PACKAGE .* result=RC=0 OK$"; then
  provenance_ok=0
  reason=UNVERIFIED_DRIVER_TERMINAL
fi
if [ "$provenance_ok" -eq 1 ] && ! one_matching_line "$RUN_TXT" \
  "^BOARDTEST_ARCHIVE_CONTROLS package=$PACKAGE .* ready=EXACT terminal=EXACT$"; then
  provenance_ok=0
  reason=UNVERIFIED_ARCHIVE_CONTROLS
fi
if [ "$provenance_ok" -eq 1 ] && ! one_matching_line "$RUN_TXT" \
  "^BOARDTEST_VERDICT_SET package=$PACKAGE expected=1 stdout=1 archive=1 raw_logs=1 xml=1 result=EXACT$"; then
  provenance_ok=0
  reason=UNVERIFIED_VERDICT_SET
fi
if [ "$provenance_ok" -eq 1 ] && ! one_matching_line "$RUN_TXT" \
  "^BOARDTEST_ACTIVITY_LOCK_RELEASE board=$BOARD .* result=MDDS_ACTIVITY_LOCK_RELEASED$"; then
  provenance_ok=0
  reason=ACTIVITY_LOCK_NOT_PROVEN_RELEASED
fi

archive_line=""
if [ "$provenance_ok" -eq 1 ]; then
  archive_line="$(LC_ALL=C grep -E "^BOARDTEST_ARCHIVE package=$PACKAGE remote=.* sha256=[0-9a-f]{64} bytes=[1-9][0-9]*$" "$RUN_TXT" 2>/dev/null || true)"
  if [[ "$(printf '%s\n' "$archive_line" | sed '/^$/d' | wc -l | tr -d '[:space:]')" != "1" ]]; then
    provenance_ok=0
    reason=RAW_ARCHIVE_RECORD_MISMATCH
  else
    announced_sha256="$(printf '%s\n' "$archive_line" | sed -n 's/.* sha256=\([0-9a-f]\{64\}\) bytes=.*/\1/p')"
    announced_bytes="$(printf '%s\n' "$archive_line" | sed -n 's/.* bytes=\([1-9][0-9]*\)$/\1/p')"
    if [[ "$announced_sha256" != "$raw_archive_sha256" || "$announced_bytes" != "$raw_archive_bytes" ]]; then
      provenance_ok=0
      reason=RAW_ARCHIVE_DIGEST_MISMATCH
    fi
  fi
fi

# The selected CTest name alone is not sufficient proof of the implementation
# it launched.  The generic runner already binds this archived driver to its
# READY manifest; inspect the archived bytes without extracting to disk and
# require the exact FastDDS environment and one subscription command.
if [ "$provenance_ok" -eq 1 ]; then
  driver_member="$PACKAGE/run_tests_board.sh"
  driver_member_count="$(tar -tf "$RAW_ARCHIVE" 2>/dev/null | tr -d '\r' | grep -Fxc -- "$driver_member" || true)"
  if [[ "$driver_member_count" != "1" ]]; then
    provenance_ok=0
    reason=ARCHIVED_DRIVER_MEMBER_MISMATCH
  elif ! archived_driver="$(tar -xOf "$RAW_ARCHIVE" "$driver_member" 2>/dev/null)"; then
    provenance_ok=0
    reason=ARCHIVED_DRIVER_UNREADABLE
  elif [[ "$(printf '%s\n' "$archived_driver" | tr -d '\r' | grep -Ec \
    '^env GTEST_BRIEF=1 RMW_IMPLEMENTATION=rmw_fastrtps_cpp timeout [0-9]+ "\$MDDS_TOKEN_EXEC" -- \./test_subscription( |$)' || true)" != "1" ||
    "$(printf '%s\n' "$archived_driver" | tr -d '\r' | grep -Fxc \
      "# BOARDTEST_EXPECTED $CTEST_SELECTOR" || true)" != "1" ||
    "$(printf '%s\n' "$archived_driver" | tr -d '\r' | grep -Fxc \
      "# BOARDTEST_TOKEN_MODE $CTEST_SELECTOR REQUIRED" || true)" != "1" ||
    "$(printf '%s\n' "$archived_driver" | tr -d '\r' | grep -Fxc \
      'exit "$overall_rc"' || true)" != "1" ]]; then
    provenance_ok=0
    reason=ARCHIVED_DRIVER_RMW_BINDING_MISMATCH
  else
    driver_archive_rmw_binding=VERIFIED
  fi
fi

verdict_lines=()
if [ "$provenance_ok" -eq 1 ]; then
  mapfile -t verdict_lines < <(tr -d '\r' < "$DRIVER_STDOUT" | grep '^BOARDTEST ' || true)
  if [ "${#verdict_lines[@]}" -ne 1 ]; then
    provenance_ok=0
    reason=SELECTOR_DID_NOT_YIELD_EXACTLY_ONE_VERDICT
  elif [[ "${verdict_lines[0]}" == "BOARDTEST $CTEST_SELECTOR PASS rc=0" ]]; then
    actual_test_result=PASS
  elif [[ "${verdict_lines[0]}" =~ ^BOARDTEST[[:space:]]+$CTEST_SELECTOR[[:space:]]+FAIL[[:space:]]+rc=[1-9][0-9]*$ ]]; then
    actual_test_result=FAIL
  else
    provenance_ok=0
    reason=UNEXPECTED_SELECTED_VERDICT
  fi
fi

if [ "$provenance_ok" -eq 1 ]; then
  if [[ "$actual_test_result" == PASS && "$runner_rc" -ne 0 ]]; then
    provenance_ok=0
    reason=PASS_WITH_NONZERO_RUNNER_EXIT
  elif [[ "$actual_test_result" == FAIL && "$runner_rc" -ne 1 ]]; then
    provenance_ok=0
    reason=FAIL_WITH_UNEXPECTED_RUNNER_EXIT
  fi
fi

validate_exemption() {
  local line reason_line expiry epoch now
  if [[ -z "$EXEMPTION_FILE" ]]; then
    exemption_status=NOT_SUPPLIED
    return 1
  fi
  if ! regular_file "$EXEMPTION_FILE"; then
    exemption_status=NOT_A_REGULAR_FILE
    return 1
  fi
  exemption_sha256="$(sha256_file "$EXEMPTION_FILE")"
  mapfile -t exemption_lines < "$EXEMPTION_FILE"
  for i in "${!exemption_lines[@]}"; do
    exemption_lines[$i]="${exemption_lines[$i]%$'\r'}"
  done
  if [ "${#exemption_lines[@]}" -ne 9 ]; then
    exemption_status=NONCANONICAL_LINE_COUNT
    return 1
  fi
  [[ "${exemption_lines[0]}" == "V=1" &&
     "${exemption_lines[1]}" == "TARGET_PACKAGE=$PACKAGE" &&
     "${exemption_lines[2]}" == "TARGET_TEST=$CTEST_SELECTOR" &&
     "${exemption_lines[3]}" == "RMW_IMPLEMENTATION=$RMW_IMPLEMENTATION" &&
     "${exemption_lines[4]}" == "DECISION=EXEMPT" ]] || {
    exemption_status=TARGET_OR_DECISION_MISMATCH
    return 1
  }
  exemption_id="${exemption_lines[5]#EXEMPTION_ID=}"
  if [[ "${exemption_lines[5]}" != EXEMPTION_ID=* || ! "$exemption_id" =~ ^[A-Za-z0-9][A-Za-z0-9_.:-]*$ ]]; then
    exemption_status=INVALID_EXEMPTION_ID
    return 1
  fi
  if [[ "${exemption_lines[6]}" != OWNER=* || -z "${exemption_lines[6]#OWNER=}" ||
        ${#exemption_lines[6]} -gt 160 ]]; then
    exemption_status=INVALID_OWNER
    return 1
  fi
  expiry="${exemption_lines[7]#EXPIRES_UTC=}"
  if [[ "${exemption_lines[7]}" != EXPIRES_UTC=* ||
        ! "$expiry" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$ ]]; then
    exemption_status=INVALID_EXPIRY_FORMAT
    return 1
  fi
  epoch="$(date -u -d "$expiry" +%s 2>/dev/null || true)"
  now="$(date -u +%s)"
  if ! [[ "$epoch" =~ ^[0-9]+$ ]] || [ "$epoch" -le "$now" ]; then
    exemption_status=EXEMPTION_EXPIRED_OR_UNPARSEABLE
    return 1
  fi
  reason_line="${exemption_lines[8]#REASON=}"
  if [[ "${exemption_lines[8]}" != REASON=* || -z "$reason_line" || ${#reason_line} -gt 1024 ]]; then
    exemption_status=INVALID_REASON
    return 1
  fi
  exemption_status=VALID
  return 0
}

if [ "$provenance_ok" -ne 1 ]; then
  result=BLOCKED
elif [[ "$actual_test_result" == PASS ]]; then
  result=PASS
  reason=EXACT_FASTDDS_TEST_PASSED
elif validate_exemption; then
  result=EXEMPTION
  reason=NAMED_EXEMPTION_MATCHED_ACTUAL_FAIL
else
  result=FAIL
  reason=EXACT_FASTDDS_TEST_FAILED_WITHOUT_VALID_EXEMPTION
fi

if ! (umask 077; set -C; {
  printf 'FASTDDS_BASELINE_GATE V=1\n'
  printf 'RUN_ID=%s\n' "$RUN_ID"
  printf 'NONCE=%s\n' "$RUN_NONCE"
  printf 'BOARD=%s\n' "$BOARD"
  printf 'PACKAGE=%s\n' "$PACKAGE"
  printf 'CTEST_SELECTOR=%s\n' "$CTEST_SELECTOR"
  printf 'RMW_IMPLEMENTATION=%s\n' "$RMW_IMPLEMENTATION"
  printf 'RESULT=%s\n' "$result"
  printf 'ACTUAL_TEST_RESULT=%s\n' "$actual_test_result"
  printf 'OVERALL_RMW_SUITE=NOT_RUN_BY_THIS_GATE\n'
  printf 'RUN_BOARD_TESTS_EXIT=%s\n' "$runner_rc"
  printf 'REASON=%s\n' "$reason"
  printf 'EXEMPTION_STATUS=%s\n' "$exemption_status"
  printf 'EXEMPTION_ID=%s\n' "$exemption_id"
  printf 'EXEMPTION_SHA256=%s\n' "$exemption_sha256"
  printf 'RUNNER_STDOUT_SHA256=%s\n' "$runner_stdout_sha256"
  printf 'RUNNER_STDERR_SHA256=%s\n' "$runner_stderr_sha256"
  printf 'RUN_TXT_SHA256=%s\n' "$run_txt_sha256"
  printf 'DRIVER_STDOUT_SHA256=%s\n' "$driver_stdout_sha256"
  printf 'ARCHIVED_DRIVER_RMW_BINDING=%s\n' "$driver_archive_rmw_binding"
  printf 'RAW_ARCHIVE_SHA256=%s\n' "$raw_archive_sha256"
  printf 'RAW_ARCHIVE_BYTES=%s\n' "$raw_archive_bytes"
} > "$RESULT_RECORD") 2>/dev/null; then
  echo "ERROR: cannot create baseline gate result record: $RESULT_RECORD" >&2
  exit 2
fi

printf 'FASTDDS_BASELINE_GATE_RESULT=%s ACTUAL_TEST_RESULT=%s OVERALL_RMW_SUITE=NOT_RUN_BY_THIS_GATE RECORD=%s\n' \
  "$result" "$actual_test_result" "$RESULT_RECORD"
case "$result" in
  PASS) exit 0 ;;
  FAIL) exit 1 ;;
  BLOCKED) exit 2 ;;
  EXEMPTION) exit 3 ;;
  *) exit 2 ;;
esac
