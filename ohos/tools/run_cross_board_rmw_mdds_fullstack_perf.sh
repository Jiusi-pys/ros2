#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 || $# -gt 4 ]]; then
  echo "usage: $0 <client-device-id> <server-device-id> [mdds-domain] [fastrtps-domain]" >&2
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CLIENT_DEVICE_ID="$1"
SERVER_DEVICE_ID="$2"
MDDS_DOMAIN="${3:-221}"
FASTRTPS_DOMAIN="${4:-222}"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${RMW_MDDS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
PYTHON_BIN="${RMW_MDDS_REMOTE_PYTHON:-/data/local/release/usr/bin/python3.12}"
WORK_DIR="${RMW_MDDS_PERF_WORK_DIR:-/data/local/tmp/rmw_mdds_fullstack_perf}"
REMOTE_PROBE="${WORK_DIR}/rmw_mdds_fullstack_perf_probe.py"
LOCAL_PROBE="${ROOT_DIR}/ohos/tools/rmw_mdds_fullstack_perf_probe.py"
SIZES="${RMW_MDDS_PERF_SIZES:-128,1024,65536,1048576,4194304}"
MAX_P95_MS="${RMW_MDDS_PERF_MAX_P95_MS:-50}"
MIN_MSG_S="${RMW_MDDS_PERF_MIN_MSG_S:-100}"
HDC_RETRY_ATTEMPTS="${RMW_MDDS_HDC_RETRY_ATTEMPTS:-3}"
HDC_RETRY_DELAY_SECONDS="${RMW_MDDS_HDC_RETRY_DELAY_SECONDS:-1}"

validate_domain() {
  local label="$1"
  local value="$2"
  if [[ ! "${value}" =~ ^(0|[1-9][0-9]{0,2})$ ]] ||
      ((10#${value} > 232)); then
    echo "${label} domain must be an integer in 0..232: ${value}" >&2
    return 1
  fi
}

if ! validate_domain "mdds" "${MDDS_DOMAIN}" ||
    ! validate_domain "fastrtps" "${FASTRTPS_DOMAIN}"; then
  exit 2
fi

ACTIVE_MODE=""

hdc_output_succeeded() {
  local status="$1"
  local output_file="$2"
  [[ "${status}" -eq 0 ]] ||
    { [[ "${status}" -eq 139 ]] && grep -qE 'BOARD_RC=0|FileTransfer finish|SERVER_STARTED|OK' "${output_file}"; }
}

capture_hdc_shell() {
  local device_id="$1"
  local command="$2"
  local output_file status attempt
  for ((attempt = 1; attempt <= HDC_RETRY_ATTEMPTS; attempt += 1)); do
    output_file="$(mktemp)"
    set +e
    "${HDC_BIN}" -t "${device_id}" shell "${command}" >"${output_file}" 2>&1
    status=$?
    set -e
    if hdc_output_succeeded "${status}" "${output_file}"; then
      cat "${output_file}"
      rm -f "${output_file}"
      return 0
    fi
    if [[ "${attempt}" -lt "${HDC_RETRY_ATTEMPTS}" ]]; then
      rm -f "${output_file}"
      sleep "${HDC_RETRY_DELAY_SECONDS}"
      continue
    fi
    cat "${output_file}"
    rm -f "${output_file}"
    return 1
  done
}

send_probe() {
  local device_id="$1"
  local output_file status attempt
  for ((attempt = 1; attempt <= HDC_RETRY_ATTEMPTS; attempt += 1)); do
    output_file="$(mktemp)"
    set +e
    "${HDC_BIN}" -t "${device_id}" file send "${LOCAL_PROBE}" "${REMOTE_PROBE}" >"${output_file}" 2>&1
    status=$?
    set -e
    if hdc_output_succeeded "${status}" "${output_file}"; then
      cat "${output_file}"
      rm -f "${output_file}"
      return 0
    fi
    if [[ "${attempt}" -lt "${HDC_RETRY_ATTEMPTS}" ]]; then
      rm -f "${output_file}"
      sleep "${HDC_RETRY_DELAY_SECONDS}"
      continue
    fi
    cat "${output_file}"
    rm -f "${output_file}"
    return 1
  done
}

remote_base_env() {
  cat <<EOF
export HOME='/data/local/tmp'; export ROS_LOG_DIR='${WORK_DIR}/roslogs'; export ROS_DISTRO='jazzy'; export LD_LIBRARY_PATH='${REMOTE_PREFIX}/lib:/data/local/tmp/ohos-prefix/lib:/data/local/tmp/ohos-fastdds/lib:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64'; unset LD_PRELOAD; export PYTHONHOME='/data/local/release/usr'; export PYTHONPATH='${REMOTE_PREFIX}/lib/python3.12/site-packages:/data/local/tmp/ohos-prefix/lib/python3.12/site-packages:/data/local/tmp/ohos-prefix/lib/python3.11/site-packages'; export AMENT_PREFIX_PATH='${REMOTE_PREFIX}:/data/local/tmp/ohos-prefix'; export ROS_LOCALHOST_ONLY=0;
EOF
}

remote_mode_env() {
  local mode="$1"
  local domain="$2"
  local socket="$3"
  local base
  base="$(remote_base_env)"
  if [[ "${mode}" == "rmw_mdds_cpp" ]]; then
    printf "%s export ROS_DOMAIN_ID='%s'; export RMW_IMPLEMENTATION='rmw_mdds_cpp'; export RMW_MDDS_BROKER=1; export RMW_MDDS_BROKER_SOCKET='%s'; export RMW_MDDS_BRIDGE_LIBRARY='/data/local/tmp/libmdds_bridge_shared.z.so';" \
      "${base}" "${domain}" "${socket}"
  else
    printf "%s export ROS_DOMAIN_ID='%s'; export RMW_IMPLEMENTATION='rmw_fastrtps_cpp'; unset RMW_MDDS_BROKER; unset RMW_MDDS_BROKER_SOCKET; unset RMW_MDDS_BRIDGE_LIBRARY;" \
      "${base}" "${domain}"
  fi
}

kill_remote_pattern() {
  local device_id="$1"
  local pattern="$2"
  capture_hdc_shell "${device_id}" \
    "ps -ef | grep '${pattern}' | grep -v grep | while read -r user pid rest; do [ -z \"\${pid}\" ] || kill -9 \"\${pid}\" 2>/dev/null || true; done; echo BOARD_RC=0" \
    >/dev/null || true
}

cleanup_all_mdds_brokers() {
  local device_id
  for device_id in "${CLIENT_DEVICE_ID}" "${SERVER_DEVICE_ID}"; do
    kill_remote_pattern "${device_id}" "[r]mw_mdds_broker"
    capture_hdc_shell "${device_id}" \
      "for _i in 1 2 3 4 5; do ps -ef | grep '[r]mw_mdds_broker' >/dev/null 2>&1 || break; sleep 1; done; rm -f /data/local/tmp/rmw_mdds_cpp.sock /data/local/tmp/rmw_mdds_cpp.sock.autostart.lock /data/local/tmp/rmw_mdds_cpp.sock.listener.lock '${WORK_DIR}/rmw_mdds_cpp.sock' '${WORK_DIR}/rmw_mdds_cpp.sock.autostart.lock' '${WORK_DIR}/rmw_mdds_cpp.sock.listener.lock'; rm -rf /data/local/tmp/rmw_mdds_cpp.sock.autostart.lockdir /data/local/tmp/rmw_mdds_cpp.sock.listener.lockdir '${WORK_DIR}/rmw_mdds_cpp.sock.autostart.lockdir' '${WORK_DIR}/rmw_mdds_cpp.sock.listener.lockdir'; echo BOARD_RC=0" >/dev/null
  done
}

cleanup_mode() {
  local mode="$1"
  local socket="${WORK_DIR}/${mode}.sock"
  kill_remote_pattern "${CLIENT_DEVICE_ID}" "rmw_mdds_fullstack_perf_probe.py"
  kill_remote_pattern "${SERVER_DEVICE_ID}" "rmw_mdds_fullstack_perf_probe.py"
  kill_remote_pattern "${CLIENT_DEVICE_ID}" "rmw_mdds_broker --socket ${socket}"
  kill_remote_pattern "${SERVER_DEVICE_ID}" "rmw_mdds_broker --socket ${socket}"
  capture_hdc_shell "${CLIENT_DEVICE_ID}" \
    "rm -f '${socket}' '${WORK_DIR}/${mode}_server.pid'; echo BOARD_RC=0" >/dev/null || true
  capture_hdc_shell "${SERVER_DEVICE_ID}" \
    "rm -f '${socket}' '${WORK_DIR}/${mode}_server.pid'; echo BOARD_RC=0" >/dev/null || true
}

cleanup() {
  cleanup_all_mdds_brokers
  cleanup_mode "rmw_mdds_cpp"
  cleanup_mode "rmw_fastrtps_cpp"
}
trap cleanup EXIT

wait_for_marker() {
  local device_id="$1"
  local log_file="$2"
  local marker="$3"
  local attempts="${4:-120}"
  local output
  for ((i = 0; i < attempts; i += 1)); do
    output="$(capture_hdc_shell "${device_id}" "test -f '${log_file}' && grep -F '${marker}' '${log_file}' || true; echo BOARD_RC=0")"
    if grep -Fq "${marker}" <<<"${output}"; then
      return 0
    fi
    sleep 0.25
  done
  capture_hdc_shell "${device_id}" "test -f '${log_file}' && cat '${log_file}' || true; echo BOARD_RC=1" >&2 || true
  return 1
}

run_mode() {
  local mode="$1"
  local domain="$2"
  local run_id="perf_${mode}_${domain}_$(date +%s)"
  local topic_prefix="/rmw_fullstack_perf_${domain}"
  local socket="${WORK_DIR}/${mode}.sock"
  local server_log="${WORK_DIR}/${mode}_server.log"
  local client_log="${WORK_DIR}/${mode}_client.log"
  local server_pid="${WORK_DIR}/${mode}_server.pid"
  local summary="${WORK_DIR}/${mode}_summary.json"
  local env client_output server_output summary_output mode_rc=0

  ACTIVE_MODE="${mode}"
  cleanup_all_mdds_brokers
  cleanup_mode "${mode}"
  env="$(remote_mode_env "${mode}" "${domain}" "${socket}")"
  capture_hdc_shell "${CLIENT_DEVICE_ID}" \
    "mkdir -p '${WORK_DIR}/roslogs'; rm -f '${client_log}' '${summary}' '${socket}'; chmod 755 '${REMOTE_PROBE}'; echo BOARD_RC=0" >/dev/null
  capture_hdc_shell "${SERVER_DEVICE_ID}" \
    "mkdir -p '${WORK_DIR}/roslogs'; rm -f '${server_log}' '${server_pid}' '${socket}'; chmod 755 '${REMOTE_PROBE}'; nohup sh -c \"${env} exec '${PYTHON_BIN}' '${REMOTE_PROBE}' server --run-id '${run_id}' --topic-prefix '${topic_prefix}' --rmw '${mode}' > '${server_log}' 2>&1\" >/dev/null 2>&1 & echo \$! > '${server_pid}'; echo SERVER_STARTED" >/dev/null
  wait_for_marker "${SERVER_DEVICE_ID}" "${server_log}" "PERF_SERVER_READY|run_id=${run_id}"

  client_output="$(capture_hdc_shell "${CLIENT_DEVICE_ID}" \
    "${env} timeout 480 '${PYTHON_BIN}' '${REMOTE_PROBE}' client --run-id '${run_id}' --topic-prefix '${topic_prefix}' --rmw '${mode}' --sizes '${SIZES}' --output '${summary}' > '${client_log}' 2>&1; rc=\$?; cat '${client_log}'; echo BOARD_RC=\${rc}")"
  printf '%s\n' "${client_output}"
  if ! grep -q 'PERF_PROBE_SUMMARY|.*status=PASS' <<<"${client_output}" ||
      ! grep -q 'BOARD_RC=0' <<<"${client_output}"; then
    mode_rc=1
  fi
  if ! wait_for_marker "${SERVER_DEVICE_ID}" "${server_log}" "PERF_SERVER_SUMMARY|run_id=${run_id}"; then
    mode_rc=1
  fi
  server_output="$(capture_hdc_shell "${SERVER_DEVICE_ID}" "cat '${server_log}'; echo BOARD_RC=0")"
  printf '%s\n' "${server_output}"
  if ! grep -q 'PERF_SERVER_SUMMARY|.*status=PASS' <<<"${server_output}"; then
    mode_rc=1
  fi
  summary_output="$(capture_hdc_shell "${CLIENT_DEVICE_ID}" "cat '${summary}' 2>/dev/null || true; echo BOARD_RC=0")"
  sed '/^BOARD_RC=0$/d' <<<"${summary_output}"
  cleanup_mode "${mode}"
  ACTIVE_MODE=""
  return "${mode_rc}"
}

mkdir -p "${ROOT_DIR}/build/rmw_mdds_fullstack_perf"
capture_hdc_shell "${CLIENT_DEVICE_ID}" "mkdir -p '${WORK_DIR}'; echo BOARD_RC=0" >/dev/null
capture_hdc_shell "${SERVER_DEVICE_ID}" "mkdir -p '${WORK_DIR}'; echo BOARD_RC=0" >/dev/null
send_probe "${CLIENT_DEVICE_ID}" >/dev/null
send_probe "${SERVER_DEVICE_ID}" >/dev/null

set +e
MDDS_OUTPUT="$(run_mode "rmw_mdds_cpp" "${MDDS_DOMAIN}")"
MDDS_RC=$?
set -e
printf '%s\n' "${MDDS_OUTPUT}"
echo "PERF_MODE_RC|rmw=rmw_mdds_cpp|rc=${MDDS_RC}"
MDDS_JSON="$(sed -n '/^{/,/^}/p' <<<"${MDDS_OUTPUT}")"

set +e
FASTRTPS_OUTPUT="$(run_mode "rmw_fastrtps_cpp" "${FASTRTPS_DOMAIN}")"
FASTRTPS_RC=$?
set -e
printf '%s\n' "${FASTRTPS_OUTPUT}"
echo "PERF_MODE_RC|rmw=rmw_fastrtps_cpp|rc=${FASTRTPS_RC}"
FASTRTPS_JSON="$(sed -n '/^{/,/^}/p' <<<"${FASTRTPS_OUTPUT}")"

if [[ -z "${MDDS_JSON}" || -z "${FASTRTPS_JSON}" ]]; then
  echo "missing board-side JSON for one or both RMW modes" >&2
  exit 1
fi

python3 - "${MDDS_JSON}" "${FASTRTPS_JSON}" "${MAX_P95_MS}" "${MIN_MSG_S}" <<'PY'
import json
import sys

mdds = json.loads(sys.argv[1])
fast = json.loads(sys.argv[2])
max_p95_ms = float(sys.argv[3])
min_msg_s = float(sys.argv[4])

if mdds.get("status") != "PASS" or fast.get("status") != "PASS":
    raise SystemExit("probe summary did not pass for both RMW implementations")

mdds_cases = {case["payload_size"]: case for case in mdds.get("cases", [])}
fast_cases = {case["payload_size"]: case for case in fast.get("cases", [])}
if not mdds_cases or mdds_cases.keys() != fast_cases.keys():
    raise SystemExit("RMW performance case sets are empty or mismatched")

for size in sorted(mdds_cases):
    mdds_case = mdds_cases[size]
    fast_case = fast_cases[size]
    for case in (mdds_case, fast_case):
        if case["status"] != "PASS" or case["missing_acks"] != 0 or case["publish_errors"] != 0:
            raise SystemExit(f"incomplete RELIABLE performance case: {case}")
    latency_ratio = (
        mdds_case["latency_p95_ms"] / fast_case["latency_p95_ms"]
        if fast_case["latency_p95_ms"] > 0.0
        else 0.0
    )
    throughput_ratio = (
        mdds_case["throughput_msg_s"] / fast_case["throughput_msg_s"]
        if fast_case["throughput_msg_s"] > 0.0
        else 0.0
    )
    print(
        "PERF_COMPARE|"
        f"size={size}|mdds_p95_ms={mdds_case['latency_p95_ms']:.3f}|"
        f"fastdds_p95_ms={fast_case['latency_p95_ms']:.3f}|latency_ratio={latency_ratio:.3f}|"
        f"mdds_msg_s={mdds_case['throughput_msg_s']:.3f}|"
        f"fastdds_msg_s={fast_case['throughput_msg_s']:.3f}|throughput_ratio={throughput_ratio:.3f}"
    )

design_case = mdds_cases.get(1024)
if design_case is None:
    raise SystemExit("missing required 1KiB design case")
if design_case["latency_p95_ms"] > max_p95_ms:
    raise SystemExit(
        f"rmw_mdds 1KiB p95 {design_case['latency_p95_ms']:.3f}ms exceeds {max_p95_ms:.3f}ms"
    )
if design_case["throughput_msg_s"] < min_msg_s:
    raise SystemExit(
        f"rmw_mdds 1KiB throughput {design_case['throughput_msg_s']:.3f}msg/s below {min_msg_s:.3f}msg/s"
    )
print(
    "RESULT|rmw_mdds_fullstack_perf|PASS|"
    f"p95_1k_ms={design_case['latency_p95_ms']:.3f}|"
    f"throughput_1k_msg_s={design_case['throughput_msg_s']:.3f}|"
    f"max_p95_ms={max_p95_ms:.3f}|min_msg_s={min_msg_s:.3f}"
)
PY

echo "RMW_MDDS_PERF_CLIENT_EVIDENCE=${WORK_DIR}"
echo "RMW_MDDS_PERF_SERVER_EVIDENCE=${WORK_DIR}"
echo "rmw_mdds_fullstack_perf_ok"
