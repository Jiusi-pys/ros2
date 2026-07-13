#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_cross_board_rmw_mdds_large_service_soak.sh \
  <client-device-id> <server-device-id> [domain-id]

Runs a time-driven, four-client, near-16-MiB ROS 2 service soak across two
RK3588A devices with RMW_IMPLEMENTATION=rmw_mdds_cpp.

Environment:
  HDC_BIN                                      HDC executable, default: hdc
  ROS2_OHOS_REMOTE_PREFIX                      ROS 2 prefix on each device
  RMW_MDDS_BRIDGE_LIBRARY                      Bridge loaded by each broker
  RMW_MDDS_LARGE_SOAK_DURATION_SECONDS         Load duration, default: 7200
  RMW_MDDS_LARGE_SOAK_CLIENTS                  Concurrent clients, default: 4
  RMW_MDDS_LARGE_SOAK_PAYLOAD_BODY_BYTES       Request/response body, default: 16777049
  RMW_MDDS_LARGE_SOAK_MIN_REQUESTS             Aggregate minimum, default: 1000
  RMW_MDDS_LARGE_SOAK_REQUEST_TIMEOUT_SECONDS  Per-request timeout, default: 600
  RMW_MDDS_LARGE_SOAK_POLL_SECONDS             Host poll interval, default: 30
  RMW_MDDS_LARGE_SOAK_RUN_ID                   Artifact label, default: UTC timestamp
  RMW_MDDS_LARGE_SOAK_GRAPH_DEBUG              Broker graph debug flag, default: 1
  RMW_MDDS_HDC_TIMEOUT_SECONDS                 Per-HDC timeout, default: 120
  RMW_MDDS_HDC_RETRY_ATTEMPTS                  HDC retries, default: 5
EOF
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

CLIENT_DEVICE_ID="$1"
SERVER_DEVICE_ID="$2"
DOMAIN_ID="${3:-${ROS_DOMAIN_ID:-203}}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORKER_LOCAL="${ROOT_DIR}/ohos/tools/rmw_mdds_large_service_soak.py"
HDC_BIN="${HDC_BIN:-hdc}"
REMOTE_PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
UNDERLAY_PREFIX="${ROS2_OHOS_REMOTE_UNDERLAY_PREFIX:-/data/local/tmp/ohos-prefix}"
FASTDDS_PREFIX="${ROS2_OHOS_REMOTE_FASTDDS_PREFIX:-/data/local/tmp/ohos-fastdds}"
BRIDGE_LIBRARY="${RMW_MDDS_BRIDGE_LIBRARY:-${REMOTE_PREFIX}/lib/libmdds_bridge_shared.z.so}"
DURATION_SECONDS="${RMW_MDDS_LARGE_SOAK_DURATION_SECONDS:-7200}"
CLIENTS="${RMW_MDDS_LARGE_SOAK_CLIENTS:-4}"
PAYLOAD_BODY_BYTES="${RMW_MDDS_LARGE_SOAK_PAYLOAD_BODY_BYTES:-16777049}"
MIN_REQUESTS="${RMW_MDDS_LARGE_SOAK_MIN_REQUESTS:-1000}"
REQUEST_TIMEOUT_SECONDS="${RMW_MDDS_LARGE_SOAK_REQUEST_TIMEOUT_SECONDS:-600}"
POLL_SECONDS="${RMW_MDDS_LARGE_SOAK_POLL_SECONDS:-30}"
RUN_ID="${RMW_MDDS_LARGE_SOAK_RUN_ID:-$(date -u +%Y%m%dT%H%M%SZ)}"
GRAPH_DEBUG="${RMW_MDDS_LARGE_SOAK_GRAPH_DEBUG:-1}"
HDC_TIMEOUT_SECONDS="${RMW_MDDS_HDC_TIMEOUT_SECONDS:-120}"
HDC_RETRY_ATTEMPTS="${RMW_MDDS_HDC_RETRY_ATTEMPTS:-5}"
HDC_RETRY_DELAY_SECONDS="${RMW_MDDS_HDC_RETRY_DELAY_SECONDS:-1}"
PYTHON="/data/local/release/usr/bin/python3.12"
LOG_DIR="/data/local/tmp/rmw_mdds_large_service_soak_${RUN_ID}_${DOMAIN_ID}"
REMOTE_WORKER="${LOG_DIR}/rmw_mdds_large_service_soak.py"
SERVICE_NAME="/rmw_mdds_large_service_soak_${RUN_ID}_${DOMAIN_ID}"
SERVER_LOG="${LOG_DIR}/server.log"
SERVER_RC_FILE="${LOG_DIR}/server.rc"
SUMMARY_FILE="${LOG_DIR}/summary.txt"

if [[ "${CLIENT_DEVICE_ID}" == "${SERVER_DEVICE_ID}" ]]; then
  echo "client and server devices must be distinct" >&2
  exit 2
fi
if ! [[ "${DOMAIN_ID}" =~ ^[0-9]+$ ]] || ((DOMAIN_ID > 232)); then
  echo "domain id must be an integer in the range 0..232" >&2
  exit 2
fi
if ! [[ "${DURATION_SECONDS}" =~ ^[0-9]+$ ]] || ((DURATION_SECONDS == 0)); then
  echo "duration must be a positive integer" >&2
  exit 2
fi
if ! [[ "${CLIENTS}" =~ ^[0-9]+$ ]] || ((CLIENTS == 0 || CLIENTS > 16)); then
  echo "client count must be in the range 1..16" >&2
  exit 2
fi
if ! [[ "${PAYLOAD_BODY_BYTES}" =~ ^[0-9]+$ ]] || ((PAYLOAD_BODY_BYTES < 64)); then
  echo "payload body size must be an integer of at least 64 bytes" >&2
  exit 2
fi
for positive_integer in \
  "${MIN_REQUESTS}" "${REQUEST_TIMEOUT_SECONDS}" "${POLL_SECONDS}" \
  "${HDC_TIMEOUT_SECONDS}" "${HDC_RETRY_ATTEMPTS}"; do
  if ! [[ "${positive_integer}" =~ ^[0-9]+$ ]] || ((positive_integer == 0)); then
    echo "timeout, poll, retry, and minimum-request values must be positive integers" >&2
    exit 2
  fi
done
if ! [[ "${RUN_ID}" =~ ^[A-Za-z0-9_]+$ ]]; then
  echo "run id may contain only letters, digits, and underscore" >&2
  exit 2
fi
if [[ "${GRAPH_DEBUG}" != "0" && "${GRAPH_DEBUG}" != "1" ]]; then
  echo "graph debug must be 0 or 1" >&2
  exit 2
fi
if [[ ! -f "${WORKER_LOCAL}" ]]; then
  echo "missing board worker: ${WORKER_LOCAL}" >&2
  exit 1
fi

hdc_output_succeeded() {
  local status="$1"
  local output_file="$2"
  [[ ${status} -eq 0 || ${status} -eq 139 ]] || return 1
  ! grep -qE 'Connect server failed|Connect key failed|No device|device offline|\[Fail\]' \
    "${output_file}"
}

capture_hdc_shell() {
  local device_id="$1"
  local command="$2"
  local attempt
  local output_file
  local status
  for ((attempt = 1; attempt <= HDC_RETRY_ATTEMPTS; attempt += 1)); do
    output_file="$(mktemp)"
    set +e
    timeout "${HDC_TIMEOUT_SECONDS}s" "${HDC_BIN}" -t "${device_id}" shell \
      "${command}" >"${output_file}" 2>&1
    status=$?
    set -e
    if hdc_output_succeeded "${status}" "${output_file}"; then
      cat "${output_file}"
      rm -f "${output_file}"
      return 0
    fi
    if ((attempt < HDC_RETRY_ATTEMPTS)); then
      rm -f "${output_file}"
      sleep "${HDC_RETRY_DELAY_SECONDS}"
      continue
    fi
    cat "${output_file}" >&2
    rm -f "${output_file}"
    return 1
  done
}

send_file() {
  local device_id="$1"
  local local_path="$2"
  local remote_path="$3"
  local attempt
  local output_file
  local status
  for ((attempt = 1; attempt <= HDC_RETRY_ATTEMPTS; attempt += 1)); do
    output_file="$(mktemp)"
    set +e
    timeout "${HDC_TIMEOUT_SECONDS}s" "${HDC_BIN}" -t "${device_id}" file send \
      "${local_path}" "${remote_path}" >"${output_file}" 2>&1
    status=$?
    set -e
    if hdc_output_succeeded "${status}" "${output_file}"; then
      rm -f "${output_file}"
      return 0
    fi
    if ((attempt < HDC_RETRY_ATTEMPTS)); then
      rm -f "${output_file}"
      sleep "${HDC_RETRY_DELAY_SECONDS}"
      continue
    fi
    cat "${output_file}" >&2
    rm -f "${output_file}"
    return 1
  done
}

best_effort_shell() {
  capture_hdc_shell "$1" "$2" 2>/dev/null || true
}

remote_env() {
  cat <<EOF
PREFIX='${REMOTE_PREFIX}'; UNDERLAY_PREFIX='${UNDERLAY_PREFIX}'; FASTDDS_PREFIX='${FASTDDS_PREFIX}'; VENDOR_LIB_PATH=; for dir in \${PREFIX}/opt/*/lib; do [ -d \${dir} ] && VENDOR_LIB_PATH=\${VENDOR_LIB_PATH:+\${VENDOR_LIB_PATH}:}\${dir}; done; UNDERLAY_VENDOR_LIB_PATH=; for dir in \${UNDERLAY_PREFIX}/opt/*/lib; do [ -d \${dir} ] && UNDERLAY_VENDOR_LIB_PATH=\${UNDERLAY_VENDOR_LIB_PATH:+\${UNDERLAY_VENDOR_LIB_PATH}:}\${dir}; done; export LD_PRELOAD=/data/local/release/usr/lib/libpython3.12.so.1.0; export PYTHONHOME=/data/local/release/usr; export HOME=/data/local/tmp; export ROS_LOG_DIR='${LOG_DIR}/roslogs'; export LD_LIBRARY_PATH=\${PREFIX}/lib:\${UNDERLAY_PREFIX}/lib:\${FASTDDS_PREFIX}/lib\${VENDOR_LIB_PATH:+:\${VENDOR_LIB_PATH}}\${UNDERLAY_VENDOR_LIB_PATH:+:\${UNDERLAY_VENDOR_LIB_PATH}}:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64; export AMENT_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}; export CMAKE_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}:\${FASTDDS_PREFIX}; export COLCON_PREFIX_PATH=\${PREFIX}:\${UNDERLAY_PREFIX}; export PYTHONPATH=\${PREFIX}/lib/python3.12/site-packages:\${UNDERLAY_PREFIX}/lib/python3.12/site-packages:\${UNDERLAY_PREFIX}/lib/python3.11/site-packages; export ROS_DOMAIN_ID='${DOMAIN_ID}'; export RMW_IMPLEMENTATION=rmw_mdds_cpp; export RMW_MDDS_BROKER=1; export RMW_MDDS_BRIDGE_LIBRARY='${BRIDGE_LIBRARY}'; export RMW_MDDS_BROKER_SOCKET='${LOG_DIR}/broker.sock'; export RMW_MDDS_BROKER_LOG='${LOG_DIR}/broker.log'; export RMW_MDDS_BROKER_PID_FILE='${LOG_DIR}/broker.pid'; export RMW_MDDS_GRAPH_DEBUG='${GRAPH_DEBUG}';
EOF
}

extract_hashes() {
  printf '%s\n' "$1" | sed -n \
    's/^\([0-9a-fA-F]\{64\}\)[[:space:]][[:space:]]*.*/\1/p' | \
    tr 'A-F' 'a-f'
}

collect_artifacts() {
  capture_hdc_shell "$1" \
    "sha256sum '${REMOTE_PREFIX}/lib/librmw_mdds_cpp.so' \
      '${REMOTE_PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker' \
      '${BRIDGE_LIBRARY}' '${REMOTE_PREFIX}/lib/libsoftbus_client.z.so'"
}

kill_remote_run() {
  local device_id="$1"
  best_effort_shell "${device_id}" \
    "ps -ef | grep -E '${REMOTE_WORKER}|${LOG_DIR}/broker.sock' | grep -v grep | while read -r user pid rest; do [ -z \"\${pid}\" ] || kill -15 \"\${pid}\" 2>/dev/null || true; done; sleep 3; ps -ef | grep -E '${REMOTE_WORKER}|${LOG_DIR}/broker.sock' | grep -v grep | while read -r user pid rest; do [ -z \"\${pid}\" ] || kill -9 \"\${pid}\" 2>/dev/null || true; done; rm -f '${LOG_DIR}/broker.sock'" \
    >/dev/null
}

cleanup() {
  kill_remote_run "${CLIENT_DEVICE_ID}"
  kill_remote_run "${SERVER_DEVICE_ID}"
}
trap cleanup EXIT

client_snapshot() {
  printf '%s\n' "$1" | awk '
    {
      sent = ok = valid = timeouts = errors = done_ack = elapsed = -1;
      is_done = ($1 == "SOAK_CLIENT_DONE");
      for (i = 1; i <= NF; i++) {
        split($i, kv, "=");
        if (kv[1] == "sent") sent = kv[2] + 0;
        else if (kv[1] == "ok") ok = kv[2] + 0;
        else if (kv[1] == "valid") valid = kv[2] + 0;
        else if (kv[1] == "timeouts") timeouts = kv[2] + 0;
        else if (kv[1] == "errors") errors = kv[2] + 0;
        else if (kv[1] == "done_ack") done_ack = kv[2] + 0;
        else if (kv[1] == "elapsed_sec") elapsed = int(kv[2] + 0);
      }
      if (sent >= 0) total_sent += sent;
      if (ok >= 0) total_ok += ok;
      if (valid >= 0) total_valid += valid;
      if (timeouts > 0) total_timeouts += timeouts;
      if (errors > 0) total_errors += errors;
      if (is_done) {
        done_count++;
        if (done_count == 1 || elapsed < min_elapsed) min_elapsed = elapsed;
        if (sent > 0 && sent == ok && sent == valid && timeouts == 0 &&
            errors == 0 && done_ack == 1) valid_done++;
      }
    }
    END {
      if (done_count == 0) min_elapsed = 0;
      printf "CLIENT_SENT=%d CLIENT_OK=%d CLIENT_VALID=%d CLIENT_DONE=%d " \
        "CLIENT_VALID_DONE=%d CLIENT_TIMEOUT=%d CLIENT_ERROR=%d " \
        "CLIENT_MIN_ELAPSED=%d", total_sent, total_ok, total_valid,
        done_count, valid_done, total_timeouts, total_errors, min_elapsed;
    }
  '
}

snapshot_value() {
  local line="$1"
  local key="$2"
  printf '%s\n' "${line}" | awk -v wanted="${key}" '
    {
      for (i = 1; i <= NF; i++) {
        split($i, kv, "=");
        if (kv[1] == wanted) {
          print kv[2];
          exit;
        }
      }
    }
  '
}

client_lines() {
  best_effort_shell "${CLIENT_DEVICE_ID}" \
    "for f in '${LOG_DIR}'/client*.log; do grep -E '^SOAK_CLIENT_(PROGRESS|DONE)' \"\${f}\" 2>/dev/null | tail -1; done"
}

server_line() {
  best_effort_shell "${SERVER_DEVICE_ID}" \
    "grep -E '^SOAK_SERVER_PROGRESS|^SOAK_SERVER_DONE' '${SERVER_LOG}' 2>/dev/null | tail -1"
}

client_rc_count() {
  best_effort_shell "${CLIENT_DEVICE_ID}" \
    "find '${LOG_DIR}' -maxdepth 1 -type f -name 'client*.rc' 2>/dev/null | wc -l" | \
    tr -d '[:space:]'
}

client_rc_zero_count() {
  best_effort_shell "${CLIENT_DEVICE_ID}" \
    "cat '${LOG_DIR}'/client*.rc 2>/dev/null | grep -c '^0$'" | tr -d '[:space:]'
}

server_rc() {
  best_effort_shell "${SERVER_DEVICE_ID}" \
    "cat '${SERVER_RC_FILE}' 2>/dev/null" | tr -d '[:space:]'
}

CLIENT_ARTIFACT_OUTPUT="$(collect_artifacts "${CLIENT_DEVICE_ID}")"
SERVER_ARTIFACT_OUTPUT="$(collect_artifacts "${SERVER_DEVICE_ID}")"
mapfile -t CLIENT_HASHES < <(extract_hashes "${CLIENT_ARTIFACT_OUTPUT}")
mapfile -t SERVER_HASHES < <(extract_hashes "${SERVER_ARTIFACT_OUTPUT}")
if [[ ${#CLIENT_HASHES[@]} -ne 4 || ${#SERVER_HASHES[@]} -ne 4 ]]; then
  echo "failed to read the four required artifact hashes from both boards" >&2
  exit 1
fi
RMW_SHA="${CLIENT_HASHES[0]}"
BROKER_SHA="${CLIENT_HASHES[1]}"
BRIDGE_SHA="${CLIENT_HASHES[2]}"
SOFTBUS_SHA="${CLIENT_HASHES[3]}"
if [[ "${CLIENT_HASHES[*]}" != "${SERVER_HASHES[*]}" ]]; then
  echo "client/server artifact hashes differ" >&2
  exit 1
fi
echo "ARTIFACTS|client|RMW_SHA=${RMW_SHA}|BROKER_SHA=${BROKER_SHA}|BRIDGE_SHA=${BRIDGE_SHA}|SOFTBUS_SHA=${SOFTBUS_SHA}"
echo "ARTIFACTS|server|RMW_SHA=${RMW_SHA}|BROKER_SHA=${BROKER_SHA}|BRIDGE_SHA=${BRIDGE_SHA}|SOFTBUS_SHA=${SOFTBUS_SHA}"

cleanup
for device_id in "${CLIENT_DEVICE_ID}" "${SERVER_DEVICE_ID}"; do
  capture_hdc_shell "${device_id}" \
    "rm -rf '${LOG_DIR}'; mkdir -p '${LOG_DIR}/roslogs'" >/dev/null
  send_file "${device_id}" "${WORKER_LOCAL}" "${REMOTE_WORKER}"
done

SERVER_TIMEOUT_SECONDS=$((DURATION_SECONDS + REQUEST_TIMEOUT_SECONDS + 600))
capture_hdc_shell "${SERVER_DEVICE_ID}" \
  "$(remote_env) rm -f '${SERVER_LOG}' '${SERVER_RC_FILE}'; nohup sh -c '${PYTHON} ${REMOTE_WORKER} server ${PAYLOAD_BODY_BYTES} ${SERVICE_NAME} ${CLIENTS} ${SERVER_TIMEOUT_SECONDS} > ${SERVER_LOG} 2>&1; echo \$? > ${SERVER_RC_FILE}' >/dev/null 2>&1 & echo LARGE_SOAK_SERVER_STARTED" \
  | tail -1
sleep 20

for ((client_id = 1; client_id <= CLIENTS; client_id += 1)); do
  client_log="${LOG_DIR}/client${client_id}.log"
  client_rc="${LOG_DIR}/client${client_id}.rc"
  capture_hdc_shell "${CLIENT_DEVICE_ID}" \
    "$(remote_env) rm -f '${client_log}' '${client_rc}'; nohup sh -c '${PYTHON} ${REMOTE_WORKER} client ${PAYLOAD_BODY_BYTES} ${SERVICE_NAME} ${client_id} ${DURATION_SECONDS} ${REQUEST_TIMEOUT_SECONDS} > ${client_log} 2>&1; echo \$? > ${client_rc}' >/dev/null 2>&1 & echo LARGE_SOAK_CLIENT_${client_id}_STARTED" \
    | tail -1
done

started_epoch="$(date +%s)"
deadline_epoch=$((started_epoch + DURATION_SECONDS + REQUEST_TIMEOUT_SECONDS + 900))
while true; do
  lines="$(client_lines)"
  client="$(client_snapshot "${lines}")"
  server="$(server_line)"
  rc_count="$(client_rc_count)"
  rc_zero="$(client_rc_zero_count)"
  server_result="$(server_rc)"
  elapsed_seconds=$(($(date +%s) - started_epoch))
  echo "PROGRESS|mdds_large_service_soak|elapsed_sec=${elapsed_seconds}|${client}|SERVER=${server:-pending}|CLIENT_RC_FILES=${rc_count:-0}|CLIENT_RC0=${rc_zero:-0}|SERVER_RC=${server_result:-pending}"

  if [[ "${client}" == *"CLIENT_DONE=${CLIENTS}"* &&
        "${client}" == *"CLIENT_VALID_DONE=${CLIENTS}"* &&
        "${rc_zero:-0}" == "${CLIENTS}" && "${server_result}" == "0" ]]; then
    break
  fi
  if [[ "${client}" != *"CLIENT_TIMEOUT=0"* || "${client}" != *"CLIENT_ERROR=0"* ]]; then
    break
  fi
  if [[ "${rc_count:-0}" =~ ^[0-9]+$ && "${rc_count:-0}" -gt 0 &&
        "${rc_count:-0}" != "${rc_zero:-0}" ]]; then
    break
  fi
  if [[ -n "${server_result}" && "${server_result}" != "0" ]]; then
    break
  fi
  if (( $(date +%s) >= deadline_epoch )); then
    break
  fi
  sleep "${POLL_SECONDS}"
done

lines="$(client_lines)"
client="$(client_snapshot "${lines}")"
server="$(server_line)"
rc_count="$(client_rc_count)"
rc_zero="$(client_rc_zero_count)"
server_result="$(server_rc)"
total_sent="$(snapshot_value "${client}" CLIENT_SENT)"
total_ok="$(snapshot_value "${client}" CLIENT_OK)"
total_valid="$(snapshot_value "${client}" CLIENT_VALID)"
client_min_elapsed="$(snapshot_value "${client}" CLIENT_MIN_ELAPSED)"
server_requests="$(snapshot_value "${server}" requests)"
server_valid="$(snapshot_value "${server}" valid)"

passed=0
if [[ "${client}" == *"CLIENT_DONE=${CLIENTS}"* &&
      "${client}" == *"CLIENT_VALID_DONE=${CLIENTS}"* &&
      "${client}" == *"CLIENT_TIMEOUT=0"* && "${client}" == *"CLIENT_ERROR=0"* &&
      "${rc_count:-0}" == "${CLIENTS}" && "${rc_zero:-0}" == "${CLIENTS}" &&
      "${server_result}" == "0" &&
      "${server}" == *"invalid=0 duplicates=0 send_errors=0 done_clients=${CLIENTS} expected_clients=${CLIENTS}"* &&
      "${total_sent:-0}" =~ ^[0-9]+$ && "${total_sent:-0}" -ge "${MIN_REQUESTS}" &&
      "${total_ok:-0}" == "${total_sent:-0}" && "${total_valid:-0}" == "${total_sent:-0}" &&
      "${server_requests:-0}" == "${total_sent:-0}" && "${server_valid:-0}" == "${total_sent:-0}" &&
      "${client_min_elapsed:-0}" =~ ^[0-9]+$ && "${client_min_elapsed:-0}" -ge "${DURATION_SECONDS}" ]]; then
  passed=1
fi

cleanup
trap - EXIT
residual_client="$(best_effort_shell "${CLIENT_DEVICE_ID}" "ps -ef | grep -E '${REMOTE_WORKER}|${LOG_DIR}/broker.sock' | grep -v grep | wc -l" | tr -d '[:space:]')"
residual_server="$(best_effort_shell "${SERVER_DEVICE_ID}" "ps -ef | grep -E '${REMOTE_WORKER}|${LOG_DIR}/broker.sock' | grep -v grep | wc -l" | tr -d '[:space:]')"
if [[ "${residual_client:-1}" != "0" || "${residual_server:-1}" != "0" ]]; then
  passed=0
fi

if ((passed == 1)); then
  summary="RESULT|mdds_large_service_soak|PASS|domain=${DOMAIN_ID}|duration_sec=${DURATION_SECONDS}|payload_body_bytes=${PAYLOAD_BODY_BYTES}|requests=${total_sent}|clients=${CLIENTS}|client_min_elapsed_sec=${client_min_elapsed}|timeout=0|error=0|rmw_sha=${RMW_SHA}|broker_sha=${BROKER_SHA}|bridge_sha=${BRIDGE_SHA}|softbus_sha=${SOFTBUS_SHA}|residual_client=0|residual_server=0"
  capture_hdc_shell "${CLIENT_DEVICE_ID}" \
    "printf '%s\n' '${summary}' > '${SUMMARY_FILE}'" >/dev/null
  capture_hdc_shell "${SERVER_DEVICE_ID}" \
    "printf '%s\n' '${summary}' > '${SUMMARY_FILE}'" >/dev/null
  echo "${summary}"
  echo "cross_board_rmw_mdds_large_service_soak_ok"
  exit 0
fi

summary="RESULT|mdds_large_service_soak|FAIL|domain=${DOMAIN_ID}|duration_sec=${DURATION_SECONDS}|payload_body_bytes=${PAYLOAD_BODY_BYTES}|requests=${total_sent:-0}|server_requests=${server_requests:-0}|clients=${CLIENTS}|client_rc0=${rc_zero:-0}|server_rc=${server_result:-missing}|residual_client=${residual_client:-unknown}|residual_server=${residual_server:-unknown}"
best_effort_shell "${CLIENT_DEVICE_ID}" \
  "printf '%s\n' '${summary}' > '${SUMMARY_FILE}'" >/dev/null
best_effort_shell "${SERVER_DEVICE_ID}" \
  "printf '%s\n' '${summary}' > '${SUMMARY_FILE}'" >/dev/null
echo "${summary}" >&2
exit 1
