#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/tools/hdc_send_verify.sh"
# language: "shell"
# summary: "Standalone shell helper defining `run_hdc_capture`, `wait_for_remote_path`, and `ensure_remote_parent` to send one local file to one remote path over HDC with retries and existence verification."
# symbols: ["run_hdc_capture", "wait_for_remote_path", "ensure_remote_parent"]
# generated_by: "codex"
# codex-file-meta: end

set -euo pipefail

HDC_TIMEOUT_SEND="${OHOS_HDC_TIMEOUT_SEND:-45s}"
HDC_TIMEOUT_SHELL="${OHOS_HDC_TIMEOUT_SHELL:-20s}"
HDC_RETRIES="${OHOS_HDC_RETRIES:-5}"
HDC_VERIFY_TIMEOUT_SECONDS="${OHOS_HDC_VERIFY_TIMEOUT_SECONDS:-20}"
HDC_BACKOFF_INITIAL_SECONDS="${OHOS_HDC_BACKOFF_INITIAL_SECONDS:-1}"
HDC_BACKOFF_MAX_SECONDS="${OHOS_HDC_BACKOFF_MAX_SECONDS:-8}"
DEFAULT_WRAPPER="/home/kaihong/.codex/skills/ohos-hdc/scripts/device-control.sh"

usage() {
  cat <<'EOF'
Usage:
  hdc_send_verify.sh <device_id> <local_file> <remote_path>
  OHOS_DEVICE_ID=<device_id> hdc_send_verify.sh <local_file> <remote_path>

Environment:
  OHOS_HDC_WRAPPER                 Explicit wrapper to invoke as `<wrapper> -t <device_id>`
  OHOS_HDC_BIN                      Explicit hdc/hdc_std binary to use
  OHOS_HDC_TIMEOUT_SEND             Timeout for `file send` operations (default: 45s)
  OHOS_HDC_TIMEOUT_SHELL            Timeout for remote shell checks (default: 20s)
  OHOS_HDC_RETRIES                  Send attempts before failing (default: 5)
  OHOS_HDC_VERIFY_TIMEOUT_SECONDS   Per-attempt wait for remote existence (default: 20)
  OHOS_HDC_BACKOFF_INITIAL_SECONDS  Initial sleep before retry (default: 1)
  OHOS_HDC_BACKOFF_MAX_SECONDS      Maximum retry sleep (default: 8)
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

DEVICE_ID="${OHOS_DEVICE_ID:-}"
LOCAL_FILE=""
REMOTE_PATH=""

if [[ $# -eq 3 ]]; then
  DEVICE_ID="$1"
  LOCAL_FILE="$2"
  REMOTE_PATH="$3"
elif [[ $# -eq 2 && -n "${DEVICE_ID}" ]]; then
  LOCAL_FILE="$1"
  REMOTE_PATH="$2"
else
  usage >&2
  exit 1
fi

if [[ -z "${DEVICE_ID}" ]]; then
  echo "Missing device id. Pass it as the first argument or set OHOS_DEVICE_ID." >&2
  exit 1
fi

if [[ ! -f "${LOCAL_FILE}" ]]; then
  echo "Missing local file: ${LOCAL_FILE}" >&2
  exit 1
fi

if [[ -n "${OHOS_HDC_WRAPPER:-}" ]]; then
  HDC_BASE=("${OHOS_HDC_WRAPPER}" -t "${DEVICE_ID}")
elif [[ -n "${OHOS_HDC_BIN:-}" ]]; then
  HDC_BASE=("${OHOS_HDC_BIN}" -t "${DEVICE_ID}")
elif command -v hdc_std >/dev/null 2>&1; then
  HDC_BASE=("$(command -v hdc_std)" -t "${DEVICE_ID}")
elif command -v hdc >/dev/null 2>&1; then
  HDC_BASE=("$(command -v hdc)" -t "${DEVICE_ID}")
elif [[ -x "${DEFAULT_WRAPPER}" ]]; then
  HDC_BASE=("${DEFAULT_WRAPPER}" -t "${DEVICE_ID}")
else
  echo "No hdc_std, hdc, wrapper, or explicit hdc binary found on PATH." >&2
  exit 1
fi

shell_quote() {
  local value="$1"
  printf "'%s'" "${value//\'/\'\"\'\"\'}"
}

remote_marker_expect() {
  local expected="$1"
  local condition="$2"
  local marker_path="${OHOS_REMOTE_MARKER:-/data/local/tmp/hdc-send-verify-${DEVICE_ID}.marker}"
  local quoted_marker
  quoted_marker="$(shell_quote "${marker_path}")"
  local quoted_expected
  quoted_expected="$(shell_quote "${expected}")"
  local quoted_missing
  quoted_missing="$(shell_quote "__REMOTE_MARKER_MISSING__")"
  local local_marker
  local_marker="$(mktemp /tmp/ohos-hdc-send-marker.XXXXXX)"

  set +e
  "${HDC_BASE[@]}" shell sh -c \
    "rm -f ${quoted_marker}; if ${condition}; then printf '%s\n' ${quoted_expected} > ${quoted_marker}; else printf '%s\n' ${quoted_missing} > ${quoted_marker}; fi"
  "${HDC_BASE[@]}" file recv "${marker_path}" "${local_marker}"
  set -e

  local output
  if [[ -f "${local_marker}" ]]; then
    output="$(cat "${local_marker}")"
  else
    output=""
  fi
  rm -f "${local_marker}"

  [[ "${output}" == *"${expected}"* ]]
}

wait_for_remote_path() {
  local remote_path="$1"
  local timeout_seconds="${2:-${HDC_VERIFY_TIMEOUT_SECONDS}}"
  local quoted_path
  quoted_path="$(shell_quote "${remote_path}")"
  local start_ts
  start_ts="$(date +%s)"

  while true; do
    if remote_marker_expect "__REMOTE_PATH_OK__" "[ -e ${quoted_path} ]"; then
      return 0
    fi

    if (( $(date +%s) - start_ts >= timeout_seconds )); then
      echo "Timed out waiting for remote path ${remote_path}" >&2
      return 1
    fi

    sleep 1
  done
}

ensure_remote_parent() {
  local remote_parent="$1"
  local quoted_parent
  quoted_parent="$(shell_quote "${remote_parent}")"
  remote_marker_expect "__REMOTE_PARENT_OK__" \
    "mkdir -p ${quoted_parent} && [ -d ${quoted_parent} ]"
}

send_remote_file_with_verify() {
  local local_file="$1"
  local remote_path="$2"
  local timeout_seconds="${3:-${HDC_VERIFY_TIMEOUT_SECONDS}}"
  local attempts="${4:-${HDC_RETRIES}}"
  local attempt
  local backoff_seconds="${HDC_BACKOFF_INITIAL_SECONDS}"

  for ((attempt = 1; attempt <= attempts; ++attempt)); do
    echo "send_attempt=${attempt}"
    if ! ensure_remote_parent "$(dirname "${remote_path}")"; then
      echo "remote_parent_verify_failed" >&2
      if (( attempt == attempts )); then
        break
      fi
      echo "retry_backoff_seconds=${backoff_seconds}" >&2
      sleep "${backoff_seconds}"
      backoff_seconds=$(( backoff_seconds * 2 ))
      if (( backoff_seconds > HDC_BACKOFF_MAX_SECONDS )); then
        backoff_seconds="${HDC_BACKOFF_MAX_SECONDS}"
      fi
      continue
    fi

    set +e
    "${HDC_BASE[@]}" file send "${local_file}" "${remote_path}"
    local send_status=$?
    set -e
    if [[ ${send_status} -ne 0 ]]; then
      echo "send_command_status=${send_status}" >&2
    fi

    if wait_for_remote_path "${remote_path}" "${timeout_seconds}"; then
      echo "hdc_send_verify_ok"
      echo "device_id=${DEVICE_ID}"
      echo "hdc_bin=${HDC_BASE[0]}"
      echo "local_file=${local_file}"
      echo "remote_path=${remote_path}"
      return 0
    fi

    if (( attempt == attempts )); then
      break
    fi

    echo "retry_backoff_seconds=${backoff_seconds}" >&2
    sleep "${backoff_seconds}"
    backoff_seconds=$(( backoff_seconds * 2 ))
    if (( backoff_seconds > HDC_BACKOFF_MAX_SECONDS )); then
      backoff_seconds="${HDC_BACKOFF_MAX_SECONDS}"
    fi
  done

  echo "Failed to send ${local_file} to ${remote_path} on device ${DEVICE_ID} after ${attempts} attempts." >&2
  return 1
}

case "${REMOTE_PATH}" in
  */*)
    REMOTE_PARENT="${REMOTE_PATH%/*}"
    if [[ -z "${REMOTE_PARENT}" ]]; then
      REMOTE_PARENT="/"
    fi
    ;;
  *)
    REMOTE_PARENT="."
    ;;
esac

main() {
  send_remote_file_with_verify "${LOCAL_FILE}" "${REMOTE_PATH}" "${HDC_VERIFY_TIMEOUT_SECONDS}" "${HDC_RETRIES}"
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  main
fi
