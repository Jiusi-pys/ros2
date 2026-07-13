#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/tools/run_rmw_mdds_broker_dynamic_loan_board.sh"
# language: "shell"
# summary: "Runs exact-artifact dynamic broker subscription-loan gates on two RK3588A boards."
# symbols: ["capture_hdc_shell", "verify_artifacts", "run_self_test", "run_remote_direction"]
# generated_by: "codex"
# codex-file-meta: end

set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_broker_dynamic_loan_board.sh <board-a> <board-b> [domain-base]

Runs broker-owned dynamic String, sequence, nested, content-filter, ownership,
two-slot pressure, teardown, and cross-board mapped-loan checks. HDC commands
are issued serially; board-side result files are authoritative when HDC exits
139 after returning output.

Environment:
  HDC_BIN                       HDC executable, default: hdc
  RMW_MDDS_HDC_TIMEOUT_SECONDS  Per-HDC timeout, default: 300
  ROS2_OHOS_REMOTE_PREFIX       Board overlay, default: /data/local/tmp/ohos-colcon-rk3588a
  RMW_MDDS_FULL_OVERLAY_PREFIX  Local overlay, default: install/ohos-colcon-rk3588a
  MDDS_BRIDGE_SHARED_SO         Local production bridge used for exact hash verification
EOF
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

BOARD_A="$1"
BOARD_B="$2"
DOMAIN_BASE="${3:-1700}"
[[ "${DOMAIN_BASE}" =~ ^[0-9]+$ ]] || {
  echo "domain-base must be numeric" >&2
  exit 2
}

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HDC_BIN="${HDC_BIN:-hdc}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-300}"
LOCAL_PREFIX="${RMW_MDDS_FULL_OVERLAY_PREFIX:-${ROOT_DIR}/install/ohos-colcon-rk3588a}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
REMOTE_PROBE="${REMOTE_PREFIX}/lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_broker_dynamic_loan_probe"
REMOTE_RUNNER="${REMOTE_PREFIX}/lib/rmw_mdds_dynamic_loan_probe/broker_dynamic_loan_board_runner.sh"
REMOTE_BROKER="${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker"
REMOTE_BRIDGE="/data/local/tmp/libmdds_bridge_shared.z.so"
WORK_ROOT="/data/local/tmp/rmw_mdds_broker_dynamic_loan_${DOMAIN_BASE}"

LOCAL_RMW="${LOCAL_PREFIX}/lib/librmw_mdds_cpp.so"
LOCAL_BROKER="${LOCAL_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker"
LOCAL_PROBE="${LOCAL_PREFIX}/lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_broker_dynamic_loan_probe"
LOCAL_RUNNER="${LOCAL_PREFIX}/lib/rmw_mdds_dynamic_loan_probe/broker_dynamic_loan_board_runner.sh"
BRIDGE_SO="${MDDS_BRIDGE_SHARED_SO:-}"
if [[ -z "${BRIDGE_SO}" ]]; then
  for candidate in \
    "${HOME}/M-DDS/OpenHarmony_lyl/out/arm64/targets/communication/dsoftbus/libmdds_bridge_shared.z.so" \
    "/home/kaihong/M-DDS/OpenHarmony_lyl/out/arm64/targets/communication/dsoftbus/libmdds_bridge_shared.z.so"; do
    if [[ -f "${candidate}" ]]; then
      BRIDGE_SO="${candidate}"
      break
    fi
  done
fi

for artifact in "${LOCAL_RMW}" "${LOCAL_BROKER}" "${LOCAL_PROBE}" "${LOCAL_RUNNER}" "${BRIDGE_SO}"; do
  [[ -f "${artifact}" ]] || {
    echo "missing exact-artifact input: ${artifact}" >&2
    exit 1
  }
done

RMW_SHA="$(sha256sum "${LOCAL_RMW}" | cut -d ' ' -f 1)"
BROKER_SHA="$(sha256sum "${LOCAL_BROKER}" | cut -d ' ' -f 1)"
PROBE_SHA="$(sha256sum "${LOCAL_PROBE}" | cut -d ' ' -f 1)"
RUNNER_SHA="$(sha256sum "${LOCAL_RUNNER}" | cut -d ' ' -f 1)"
BRIDGE_SHA="$(sha256sum "${BRIDGE_SO}" | cut -d ' ' -f 1)"

capture_hdc_shell() {
  local board="$1"
  local command="$2"
  local output_file status
  output_file="$(mktemp)"
  set +e
  timeout "${HDC_TIMEOUT_SECONDS}s" "${HDC_BIN}" -t "${board}" shell "${command}" >"${output_file}" 2>&1
  status=$?
  set -e
  cat "${output_file}"
  rm -f "${output_file}"
  [[ ${status} -eq 0 || ${status} -eq 139 ]]
}

verify_artifacts() {
  local board="$1"
  local label="$2"
  local output
  output="$({ capture_hdc_shell "${board}" \
    "test -x '${REMOTE_PROBE}' && test -x '${REMOTE_RUNNER}' && test -x '${REMOTE_BROKER}' && test -f '${REMOTE_BRIDGE}' && test \"\$(sha256sum '${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so' | cut -d ' ' -f 1)\" = '${RMW_SHA}' && test \"\$(sha256sum '${REMOTE_BROKER}' | cut -d ' ' -f 1)\" = '${BROKER_SHA}' && test \"\$(sha256sum '${REMOTE_PROBE}' | cut -d ' ' -f 1)\" = '${PROBE_SHA}' && test \"\$(sha256sum '${REMOTE_RUNNER}' | cut -d ' ' -f 1)\" = '${RUNNER_SHA}' && test \"\$(sha256sum '${REMOTE_BRIDGE}' | cut -d ' ' -f 1)\" = '${BRIDGE_SHA}' && echo RESULT\|rmw_mdds_broker_dynamic_loan_artifacts\|PASS\|board=${label}\|rmw_sha=${RMW_SHA}\|broker_sha=${BROKER_SHA}\|probe_sha=${PROBE_SHA}\|bridge_sha=${BRIDGE_SHA}"; } || true)"
  printf '%s\n' "${output}"
  grep -q "RESULT|rmw_mdds_broker_dynamic_loan_artifacts|PASS|board=${label}" <<<"${output}" || {
    echo "RESULT|rmw_mdds_broker_dynamic_loan_artifacts|FAIL|board=${label}" >&2
    return 1
  }
}

run_self_test() {
  local board="$1"
  local label="$2"
  local domain="$3"
  local work="${WORK_ROOT}/self_${label}"
  local output case_count
  output="$({ capture_hdc_shell "${board}" \
    "'${REMOTE_RUNNER}' --self-test '${domain}' '${work}'"; } || true)"
  printf '%s\n' "${output}"
  case_count="$(grep -c '^RESULT|rmw_mdds_broker_dynamic_loan_case|PASS|' <<<"${output}" || true)"
  grep -q '^RESULT|rmw_mdds_broker_dynamic_loan|PASS|mode=--self-test|' <<<"${output}" &&
    grep -q '^RESULT|rmw_mdds_broker_dynamic_loan_board_runner|PASS|mode=--self-test|' <<<"${output}" &&
    [[ "${case_count}" -eq 7 ]] || {
      echo "RESULT|rmw_mdds_broker_dynamic_loan_self|FAIL|board=${label}|cases=${case_count}" >&2
      return 1
    }
  echo "RESULT|rmw_mdds_broker_dynamic_loan_self|PASS|board=${label}|cases=7|domain=${domain}"
}

run_remote_direction() {
  local subscriber_board="$1"
  local publisher_board="$2"
  local label="$3"
  local domain="$4"
  local subscriber_work="${WORK_ROOT}/remote_${label}_subscriber"
  local publisher_work="${WORK_ROOT}/remote_${label}_publisher"
  local start_output publisher_output subscriber_output case_count

  start_output="$({ capture_hdc_shell "${subscriber_board}" \
    "nohup '${REMOTE_RUNNER}' --remote-subscriber '${domain}' '${subscriber_work}' >/dev/null 2>&1 & echo RESULT\|rmw_mdds_broker_dynamic_loan_remote_start\|PASS\|direction=${label}"; } || true)"
  printf '%s\n' "${start_output}"
  grep -q "RESULT|rmw_mdds_broker_dynamic_loan_remote_start|PASS|direction=${label}" <<<"${start_output}" || {
    echo "RESULT|rmw_mdds_broker_dynamic_loan_remote|FAIL|direction=${label}|stage=start" >&2
    return 1
  }
  sleep 3

  publisher_output="$({ capture_hdc_shell "${publisher_board}" \
    "'${REMOTE_RUNNER}' --remote-publisher '${domain}' '${publisher_work}'"; } || true)"
  printf '%s\n' "${publisher_output}"
  grep -q '^RESULT|rmw_mdds_broker_dynamic_loan_remote_publisher|PASS|' <<<"${publisher_output}" &&
    grep -q '^RESULT|rmw_mdds_broker_dynamic_loan_board_runner|PASS|mode=--remote-publisher|' <<<"${publisher_output}" || {
      echo "RESULT|rmw_mdds_broker_dynamic_loan_remote|FAIL|direction=${label}|stage=publisher" >&2
      return 1
    }

  subscriber_output="$({ capture_hdc_shell "${subscriber_board}" \
    "i=0; while test \${i} -lt 210 && test ! -f '${subscriber_work}/done'; do sleep 1; i=\$((i + 1)); done; test -f '${subscriber_work}/done' && cat '${subscriber_work}/result.txt'"; } || true)"
  printf '%s\n' "${subscriber_output}"
  case_count="$(grep -c '^RESULT|rmw_mdds_broker_dynamic_loan_case|PASS|case=remote_' <<<"${subscriber_output}" || true)"
  grep -q '^RESULT|rmw_mdds_broker_dynamic_loan_remote_subscriber|PASS|shapes=3$' <<<"${subscriber_output}" &&
    grep -q '^RESULT|rmw_mdds_broker_dynamic_loan_board_runner|PASS|mode=--remote-subscriber|' <<<"${subscriber_output}" &&
    [[ "${case_count}" -eq 3 ]] || {
      echo "RESULT|rmw_mdds_broker_dynamic_loan_remote|FAIL|direction=${label}|stage=subscriber|cases=${case_count}" >&2
      return 1
    }
  echo "RESULT|rmw_mdds_broker_dynamic_loan_remote|PASS|direction=${label}|shapes=3|domain=${domain}"
}

verify_artifacts "${BOARD_A}" a
verify_artifacts "${BOARD_B}" b
run_self_test "${BOARD_A}" a "$((DOMAIN_BASE + 1))"
run_self_test "${BOARD_B}" b "$((DOMAIN_BASE + 2))"
run_remote_direction "${BOARD_B}" "${BOARD_A}" a_to_b "$((DOMAIN_BASE + 3))"
run_remote_direction "${BOARD_A}" "${BOARD_B}" b_to_a "$((DOMAIN_BASE + 4))"

echo "RESULT|rmw_mdds_broker_dynamic_loan_board|PASS|self=2|remote_directions=2|remote_shapes=6|rmw_sha=${RMW_SHA}|broker_sha=${BROKER_SHA}|probe_sha=${PROBE_SHA}|bridge_sha=${BRIDGE_SHA}"
echo "rmw_mdds_broker_dynamic_loan_board_ok"
