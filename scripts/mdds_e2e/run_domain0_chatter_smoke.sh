#!/usr/bin/env bash
# Production-domain-0 /chatter smoke gate.
#
# This is deliberately separate from run_mdds_gw.sh: it enters the production
# ROS and MDDS domain (0) and uses the deployed /chatter gateway profile rather
# than a run-unique topic/domain.  It must therefore fail closed before any
# payload if a potentially foreign ROS/MDDS/Cyclone process or DSoftBus session
# is visible.  It never kills a process unless that process first wrote this
# invocation's exact PID:start record.
#
# Topology, two sequential exact legs:
#   PC CycloneDDS -> gateway on board A -> MDDS/DSoftBus -> board B
#   board B MDDS/DSoftBus -> gateway on board A -> PC CycloneDDS
#
# Every leg sends exactly ten structured std_msgs/msg/ByteMultiArray samples on
# the literal /chatter topic.  The entire run token, direction, sequence and
# CRC32 are in the payload.  A static/old /chatter message cannot satisfy the
# result parser.
#
# Usage from ros2/ in Git Bash:
#   MDDS_RUN_ID=<id> MDDS_RUN_NONCE=<nonce> \
#     MDDS_DOMAIN0_LOGROOT=/c/mdds-v11/<run>/raw_logs/domain0 \
#     ./scripts/mdds_e2e/run_domain0_chatter_smoke.sh
#
# --validate-only performs no HDC/PC/device action.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/../.."
source "$PWD/scripts/lib/mdds_msys_env.sh" || {
  echo "ERROR: cannot load scripts/lib/mdds_msys_env.sh" >&2
  exit 2
}

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00
BOARD_B=3e01ff55454d202020104433991c3b00
DEVICE_DIR=/data/local/tmp/ros2
TOPIC=/chatter
COUNT=10
PAYLOAD_BYTES=512
PC_WS=/c/pixi_ws
PC_BAT_DIR="$(cygpath -w "$PWD/scripts/mdds_e2e/pc")"
PROBE_LOCAL=scripts/mdds_e2e/domain0_chatter_probe.py
PROBE_REMOTE="$DEVICE_DIR/mdds_e2e/domain0_chatter_probe.py"
REMOTE_GUARD_LOCAL=scripts/mdds_e2e/domain0_remote_guard.sh
REMOTE_GUARD="$DEVICE_DIR/mdds_e2e/domain0_remote_guard.sh"
REMOTE_SPAWN_LOCAL=scripts/mdds_e2e/domain0_remote_spawn.py
REMOTE_SPAWN="$DEVICE_DIR/mdds_e2e/domain0_remote_spawn.py"
REMOTE_GUARD_TEST_LOCAL=scripts/mdds_e2e/domain0_remote_guard_selftest.sh
REMOTE_GUARD_TEST="$DEVICE_DIR/mdds_e2e/domain0_remote_guard_selftest.sh"
REMOTE_PYTHON=/data/python312-rk3588a/usr/bin/python3.12
PC_BATCH_LOCAL=scripts/mdds_e2e/pc/domain0_chatter_probe.bat
PC_GUARD_LOCAL=scripts/mdds_e2e/pc/domain0_chatter_guard.ps1
PC_BATCH="$PC_BAT_DIR\\domain0_chatter_probe.bat"
PC_GUARD="$PC_BAT_DIR\\domain0_chatter_guard.ps1"
HOST_PYTHON="${MDDS_HOST_PYTHON:-$PWD/.pixi/envs/default/python.exe}"
CYCLONE_XML_LOCAL=scripts/mdds_e2e/cyclonedds_board_a.xml
CYCLONE_XML_REMOTE="$DEVICE_DIR/mdds_e2e/cyclonedds_board_a.xml"
RMW_PROFILE_LOCAL=install_ohos/share/rmw_mdds/config/ohos_dsoftbus.env
RMW_PROFILE_REMOTE="$DEVICE_DIR/share/rmw_mdds/config/ohos_dsoftbus.env"
GATEWAY_PROFILE_LOCAL=install_ohos/share/mdds_gateway/mdds_gateway_ohos_dsoftbus.conf
GATEWAY_PROFILE_REMOTE="$DEVICE_DIR/share/mdds_gateway/mdds_gateway_ohos_dsoftbus.conf"
GATEWAY_BIN="$DEVICE_DIR/lib/mdds_gateway/mdds_gateway"

LOGROOT="${MDDS_DOMAIN0_LOGROOT:-ohos_test_logs/mdds_domain0_chatter}"
SAFE_LOGROOT_RE='^([A-Za-z0-9][A-Za-z0-9._-]*)(/[A-Za-z0-9][A-Za-z0-9._-]*)*$|^/[A-Za-z]/[A-Za-z0-9][A-Za-z0-9._-]*(/[A-Za-z0-9][A-Za-z0-9._-]*)*$'
if [[ ! "$LOGROOT" =~ $SAFE_LOGROOT_RE ]]; then
  echo "ERROR: MDDS_DOMAIN0_LOGROOT must be a safe relative path or /c/... path" >&2
  exit 2
fi

RUN_ID="${MDDS_RUN_ID:-domain0_chatter_$(date +%Y%m%d_%H%M%S)_$RANDOM}"
RUN_NONCE="${MDDS_RUN_NONCE:-n${RANDOM}p${RANDOM}x$$}"
case "$RUN_ID" in ''|*[!A-Za-z0-9_-]*) echo "ERROR: invalid MDDS_RUN_ID" >&2; exit 2 ;; esac
case "$RUN_NONCE" in ''|*[!A-Za-z0-9_-]*) echo "ERROR: invalid MDDS_RUN_NONCE" >&2; exit 2 ;; esac
RUN_TOKEN="d0_${RUN_ID}_${RUN_NONCE}"
if (( ${#RUN_TOKEN} > 160 )); then
  echo "ERROR: run token exceeds probe limit" >&2
  exit 2
fi
export MDDS_RUN_ID="$RUN_ID"
export MDDS_RUN_NONCE="$RUN_NONCE"
export MSYS2_ARG_CONV_EXCL='*'

LOGDIR="$LOGROOT/$RUN_ID"
REMOTE_LOGDIR="$DEVICE_DIR/mdds_domain0_chatter_runs/$RUN_ID"
REMOTE_OWNER="$REMOTE_LOGDIR/.d0_run_owner"
ACTIVITY_LOCK_DIR="$DEVICE_DIR/.mdds-activity-lock"
LOCAL_OWNER="$LOGDIR/.d0_pc_run_owner"
DIALER_DECISION_LOG="$LOGDIR/dialer_decisions.txt"

VALIDATE_ONLY=0
if [[ "${1:-}" = --validate-only && "$#" -eq 1 ]]; then
  VALIDATE_ONLY=1
elif [[ "$#" -ne 0 ]]; then
  echo "Usage: $0 [--validate-only]" >&2
  exit 2
fi

shell() { "$HDC" -t "$1" shell "$2" </dev/null; }
send_file() { "$HDC" -t "$1" file send "$(cygpath -w "$2")" "$3" </dev/null >/dev/null; }
sha_local() { sha256sum -- "$1" | cut -d ' ' -f1; }

remote_quote() {
  printf "'"
  printf '%s' "$1" | sed "s/'/'\\\\''/g"
  printf "'"
}

valid_sha() { [[ "$1" =~ ^[0-9a-fA-F]{64}$ ]]; }

sha_remote() { # <board> <absolute path>
  local got
  got="$(shell "$1" "if test -f $(remote_quote "$2") && test ! -L $(remote_quote "$2"); then sha256sum $(remote_quote "$2") 2>/dev/null | cut -d ' ' -f1; fi" | tr -d '\r\n')"
  valid_sha "$got" || return 1
  printf '%s\n' "${got,,}"
}

require_exact_line() { # <path> <literal>
  [ "$(grep -Fxc "$2" "$1" 2>/dev/null || true)" = 1 ]
}

validate_local_contract() {
  local failed=0 path
  for path in "$PROBE_LOCAL" "$REMOTE_GUARD_LOCAL" "$REMOTE_SPAWN_LOCAL" "$REMOTE_GUARD_TEST_LOCAL" "$CYCLONE_XML_LOCAL" "$RMW_PROFILE_LOCAL" \
              "$GATEWAY_PROFILE_LOCAL" "$PC_BATCH_LOCAL" "$PC_GUARD_LOCAL"; do
    if [ ! -f "$path" ]; then
      echo "D0_STATIC_CONTRACT missing=$path" >&2
      failed=1
    fi
  done
  require_exact_line "$GATEWAY_PROFILE_LOCAL" 'cyclone_domain_id = 0' || failed=1
  require_exact_line "$GATEWAY_PROFILE_LOCAL" 'mdds_domain_id = 0' || failed=1
  require_exact_line "$GATEWAY_PROFILE_LOCAL" 'mdds_transport = dsoftbus' || failed=1
  require_exact_line "$GATEWAY_PROFILE_LOCAL" 'topic = /chatter' || failed=1
  [ "$(grep -Ec '^[[:space:]]*topic[[:space:]]*=' "$GATEWAY_PROFILE_LOCAL" 2>/dev/null || true)" = 1 ] || failed=1
  grep -Fqx 'export RMW_IMPLEMENTATION=rmw_mdds' "$RMW_PROFILE_LOCAL" || failed=1
  grep -Fqx 'export MDDS_DEPLOYMENT_PROFILE=ohos_dsoftbus' "$RMW_PROFILE_LOCAL" || failed=1
  grep -Fqx 'set "RMW_IMPLEMENTATION=rmw_cyclonedds_cpp"' "$PC_BATCH_LOCAL" || failed=1
  grep -Fqx 'set "ROS_DOMAIN_ID=0"' "$PC_BATCH_LOCAL" || failed=1
  grep -Fq -- "--topic /chatter" "$PC_BATCH_LOCAL" || failed=1
  [ -x "$HOST_PYTHON" ] || { echo "D0_STATIC_CONTRACT missing_host_python=$HOST_PYTHON" >&2; failed=1; }
  [ "$failed" -ne 0 ] || "$HOST_PYTHON" "$PROBE_LOCAL" --self-test || failed=1
  if [ "$failed" -eq 0 ]; then
    echo "D0_STATIC_CONTRACT PASS topic=$TOPIC count=$COUNT payload_bytes=$PAYLOAD_BYTES"
    return 0
  fi
  echo "D0_STATIC_CONTRACT FAIL" >&2
  return 1
}

if [ "$VALIDATE_ONLY" -eq 1 ]; then
  validate_local_contract
  exit $?
fi

if [ -e "$LOGDIR" ]; then
  echo "ERROR: refusing to reuse an existing evidence directory: $LOGDIR" >&2
  exit 2
fi
mkdir -p "$LOGDIR/pc" || { echo "ERROR: cannot create $LOGDIR" >&2; exit 2; }
RUN_MARKER="$LOGDIR/.d0_run_marker"
touch "$RUN_MARKER" || { echo "ERROR: cannot create run marker" >&2; exit 2; }
printf 'RUN_ID=%s\nRUN_NONCE=%s\nRUN_TOKEN=%s\nTOPIC=%s\nCOUNT=%s\nPAYLOAD_BYTES=%s\n' \
  "$RUN_ID" "$RUN_NONCE" "$RUN_TOKEN" "$TOPIC" "$COUNT" "$PAYLOAD_BYTES" > "$LOGDIR/run_binding.txt"

declare -a ACTIVITY_LOCKED_BOARDS=()
declare -a BOARD_TRACKED=()
declare -a PC_TRACKED=()
LAUNCH_SEQUENCE=0

activity_lock_owner() {
  printf 'MDDS_ACTIVITY_LOCK MODE=DOMAIN0_CHATTER RUN_ID=%s NONCE=%s OWNER=run_domain0_chatter_smoke\n' "$RUN_ID" "$RUN_NONCE"
}

acquire_activity_lock() { # <board>
  local board="$1" owner out
  owner="$(activity_lock_owner)"
  out="$(shell "$board" "if (umask 077; mkdir $(remote_quote "$ACTIVITY_LOCK_DIR")) 2>/dev/null; then if (umask 077; set -C; printf '%s\\n' $(remote_quote "$owner") > $(remote_quote "$ACTIVITY_LOCK_DIR/owner")) 2>/dev/null && test -d $(remote_quote "$ACTIVITY_LOCK_DIR") && test ! -L $(remote_quote "$ACTIVITY_LOCK_DIR") && test -f $(remote_quote "$ACTIVITY_LOCK_DIR/owner") && test ! -L $(remote_quote "$ACTIVITY_LOCK_DIR/owner") && grep -Fqx $(remote_quote "$owner") $(remote_quote "$ACTIVITY_LOCK_DIR/owner"); then printf D0_LOCK_ACQUIRED; else printf D0_LOCK_OWNER_WRITE_FAILED; fi; else printf D0_LOCK_BUSY; fi" | tr -d '\r\n')"
  printf 'board=%s owner=%s result=%s\n' "$board" "$owner" "${out:-NO_MARKER}" >> "$LOGDIR/activity_locks.txt"
  if [ "$out" != D0_LOCK_ACQUIRED ]; then
    echo "ERROR: MDDS activity lock is occupied/malformed on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi
  ACTIVITY_LOCKED_BOARDS+=("$board")
}

release_activity_locks() {
  local i board owner out rc=0
  owner="$(activity_lock_owner)"
  for ((i=${#ACTIVITY_LOCKED_BOARDS[@]} - 1; i >= 0; --i)); do
    board="${ACTIVITY_LOCKED_BOARDS[$i]}"
    out="$(shell "$board" "if test -d $(remote_quote "$ACTIVITY_LOCK_DIR") && test ! -L $(remote_quote "$ACTIVITY_LOCK_DIR") && test -f $(remote_quote "$ACTIVITY_LOCK_DIR/owner") && test ! -L $(remote_quote "$ACTIVITY_LOCK_DIR/owner") && grep -Fqx $(remote_quote "$owner") $(remote_quote "$ACTIVITY_LOCK_DIR/owner"); then rm -f $(remote_quote "$ACTIVITY_LOCK_DIR/owner") && rmdir $(remote_quote "$ACTIVITY_LOCK_DIR") && printf D0_LOCK_RELEASED; else printf D0_LOCK_NOT_OWNED; fi" | tr -d '\r\n')"
    printf 'board=%s owner=%s release=%s\n' "$board" "$owner" "${out:-NO_MARKER}" >> "$LOGDIR/activity_locks.txt"
    [ "$out" = D0_LOCK_RELEASED ] || rc=1
  done
  ACTIVITY_LOCKED_BOARDS=()
  return "$rc"
}

ensure_remote_owner() { # <board>
  local board="$1" owner out
  owner="D0_RUN_OWNER RUN_ID=$RUN_ID NONCE=$RUN_NONCE"
  out="$(shell "$board" "dir=$(remote_quote "$REMOTE_LOGDIR"); owner=$(remote_quote "$REMOTE_OWNER"); line=$(remote_quote "$owner"); if ! mkdir -p \"\$dir\" || test ! -d \"\$dir\" || test -L \"\$dir\"; then printf D0_OWNER_DIR_FAILED; elif test -e \"\$owner\"; then if test -f \"\$owner\" && test ! -L \"\$owner\" && grep -Fqx \"\$line\" \"\$owner\"; then printf D0_OWNER_MATCH; else printf D0_OWNER_CONFLICT; fi; elif (set -C; umask 077; printf '%s\\n' \"\$line\" > \"\$owner\") 2>/dev/null && test -f \"\$owner\" && test ! -L \"\$owner\" && grep -Fqx \"\$line\" \"\$owner\"; then printf D0_OWNER_CREATED; else printf D0_OWNER_WRITE_FAILED; fi" | tr -d '\r\n')"
  printf 'board=%s result=%s\n' "$board" "${out:-NO_MARKER}" >> "$LOGDIR/remote_owners.txt"
  [[ "$out" = D0_OWNER_CREATED || "$out" = D0_OWNER_MATCH ]]
}

preflight_board() { # <board> <label>
  local board="$1" label="$2" scan out
  local output="$LOGDIR/domain0_${label}_preflight.log"
  scan='
device_root=$1
bad=0
printf "D0_BOARD_PREFLIGHT_BEGIN device_root=%s\\n" "$device_root"
if ! test -r /proc/net/unix; then echo D0_BOARD_PREFLIGHT_ERROR proc_net_unix_unreadable; exit 2; fi
if grep -Fq com.kaihong.mdds.d0 /proc/net/unix 2>/dev/null; then
  echo D0_BOARD_FOREIGN_SESSION_MARKER com.kaihong.mdds.d0
  bad=1
fi
for proc in /proc/[0-9]*; do
  test -r "$proc/maps" || continue
  if grep -Fq "$device_root/lib/libmdds.so" "$proc/maps" 2>/dev/null || grep -Fq "$device_root/lib/librmw_cyclonedds_cpp.so" "$proc/maps" 2>/dev/null; then
    pid=${proc#/proc/}
    state=$(cut -d " " -f3 "$proc/stat" 2>/dev/null || true)
    domain=default0
    if ! test -r "$proc/environ"; then
      domain=unreadable
    elif grep -a -q "ROS_DOMAIN_ID=0" "$proc/environ" 2>/dev/null; then
      domain=explicit0
    elif grep -a -q "ROS_DOMAIN_ID=" "$proc/environ" 2>/dev/null; then
      domain=nonzero
    fi
    case "$domain" in
      default0|explicit0|unreadable)
        echo "D0_BOARD_FOREIGN_PROCESS PID=$pid STATE=${state:-unknown} DOMAIN=$domain"
        bad=1
        ;;
      nonzero)
        echo "D0_BOARD_NONZERO_PROCESS PID=$pid STATE=${state:-unknown} DOMAIN=nonzero"
        ;;
    esac
  fi
done
if [ "$bad" -eq 0 ]; then echo D0_BOARD_PREFLIGHT_PASS; else echo D0_BOARD_PREFLIGHT_BLOCKED; fi
'
  out="$(shell "$board" "sh -c $(remote_quote "$scan") sh $(remote_quote "$DEVICE_DIR")" 2>&1 || true)"
  printf '%s\n' "$out" > "$output"
  grep -Fqx 'D0_BOARD_PREFLIGHT_PASS' "$output" && \
    ! grep -Eq '^D0_BOARD_(FOREIGN|PREFLIGHT_ERROR|PREFLIGHT_BLOCKED)' "$output"
}

preflight_pc() {
  local output="$LOGDIR/domain0_pc_preflight.log"
  powershell -NoProfile -NonInteractive -Command '
    $ErrorActionPreference = "Stop"
    $bad = 0
    $self = $PID
    $pattern = "(?i)(\\\\pixi_ws\\\\ros2-windows\\\\|\\bros2(\\.exe)?\\b|\\brclpy\\b|\\brmw_(cyclonedds|mdds)\\b|\\bmdds_gateway\\b|domain0_chatter_probe)"
    Write-Output "D0_PC_PREFLIGHT_BEGIN"
    $candidates = Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
      $_.ProcessId -ne $self -and $_.CommandLine -and $_.CommandLine -match $pattern
    }
    foreach ($candidate in $candidates) {
      $digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($candidate.CommandLine))).ToLowerInvariant()
      Write-Output "D0_PC_FOREIGN_PROCESS PID=$($candidate.ProcessId) NAME=$($candidate.Name) CMD_SHA256=$digest"
      $bad = 1
    }
    $udp = Get-NetUDPEndpoint -ErrorAction Stop | Where-Object { $_.LocalPort -ge 7400 -and $_.LocalPort -le 7499 }
    foreach ($entry in $udp) {
      Write-Output "D0_PC_FOREIGN_DDS_UDP LOCAL=$($entry.LocalAddress):$($entry.LocalPort) OWNING_PID=$($entry.OwningProcess)"
      $bad = 1
    }
    if ($bad -eq 0) { Write-Output "D0_PC_PREFLIGHT_PASS" } else { Write-Output "D0_PC_PREFLIGHT_BLOCKED"; exit 3 }
  ' > "$output" 2>&1 || true
  grep -Fqx 'D0_PC_PREFLIGHT_PASS' "$output" && \
    ! grep -Eq '^D0_PC_(FOREIGN|PREFLIGHT_BLOCKED|PREFLIGHT_ERROR)' "$output"
}

record_artifact() { # <name> <board> <local> <remote>
  local name="$1" board="$2" local_path="$3" remote_path="$4" want got
  want="$(sha_local "$local_path" 2>/dev/null || true)"
  got="$(sha_remote "$board" "$remote_path" 2>/dev/null || true)"
  printf 'name=%s board=%s local=%s remote=%s local_sha256=%s remote_sha256=%s result=%s\n' \
    "$name" "$board" "$local_path" "$remote_path" "${want:-MISSING}" "${got:-MISSING}" \
    "$([ "$want" = "$got" ] && valid_sha "$want" && echo MATCH || echo MISMATCH)" >> "$LOGDIR/artifact_hashes.txt"
  valid_sha "$want" && [ "$want" = "$got" ]
}

send_verified_helper() { # <board> <name> <local> <remote>
  local board="$1" name="$2" local_path="$3" remote_path="$4" want got
  want="$(sha_local "$local_path" 2>/dev/null || true)"
  valid_sha "$want" || return 1
  send_file "$board" "$local_path" "$remote_path" || return 1
  got="$(sha_remote "$board" "$remote_path" 2>/dev/null || true)"
  printf 'name=%s board=%s local=%s remote=%s sha256=%s remote_sha256=%s result=%s\n' \
    "$name" "$board" "$local_path" "$remote_path" "$want" "${got:-MISSING}" \
    "$([ "$want" = "$got" ] && echo MATCH || echo MISMATCH)" >> "$LOGDIR/helper_transfer_transcript.txt"
  [ "$want" = "$got" ]
}

verify_artifacts_and_helpers() {
  : > "$LOGDIR/artifact_hashes.txt"
  : > "$LOGDIR/helper_transfer_transcript.txt"
  validate_local_contract || return 1
  record_artifact libmdds.so "$BOARD_A" install_ohos/lib/libmdds.so "$DEVICE_DIR/lib/libmdds.so" || return 1
  record_artifact librmw_mdds.so "$BOARD_A" install_ohos/lib/librmw_mdds.so "$DEVICE_DIR/lib/librmw_mdds.so" || return 1
  record_artifact librmw_cyclonedds_cpp.so "$BOARD_A" install_ohos/lib/librmw_cyclonedds_cpp.so "$DEVICE_DIR/lib/librmw_cyclonedds_cpp.so" || return 1
  record_artifact rmw_mdds_dsoftbus_profile "$BOARD_A" "$RMW_PROFILE_LOCAL" "$RMW_PROFILE_REMOTE" || return 1
  record_artifact mdds_gateway "$BOARD_A" install_ohos/lib/mdds_gateway/mdds_gateway "$GATEWAY_BIN" || return 1
  record_artifact mdds_gateway_profile "$BOARD_A" "$GATEWAY_PROFILE_LOCAL" "$GATEWAY_PROFILE_REMOTE" || return 1
  record_artifact libmdds.so "$BOARD_B" install_ohos/lib/libmdds.so "$DEVICE_DIR/lib/libmdds.so" || return 1
  record_artifact librmw_mdds.so "$BOARD_B" install_ohos/lib/librmw_mdds.so "$DEVICE_DIR/lib/librmw_mdds.so" || return 1
  record_artifact rmw_mdds_dsoftbus_profile "$BOARD_B" "$RMW_PROFILE_LOCAL" "$RMW_PROFILE_REMOTE" || return 1
  send_verified_helper "$BOARD_B" domain0_chatter_probe.py "$PROBE_LOCAL" "$PROBE_REMOTE" || return 1
  send_verified_helper "$BOARD_A" domain0_remote_guard.sh "$REMOTE_GUARD_LOCAL" "$REMOTE_GUARD" || return 1
  send_verified_helper "$BOARD_B" domain0_remote_guard.sh "$REMOTE_GUARD_LOCAL" "$REMOTE_GUARD" || return 1
  send_verified_helper "$BOARD_A" domain0_remote_spawn.py "$REMOTE_SPAWN_LOCAL" "$REMOTE_SPAWN" || return 1
  send_verified_helper "$BOARD_B" domain0_remote_spawn.py "$REMOTE_SPAWN_LOCAL" "$REMOTE_SPAWN" || return 1
  send_verified_helper "$BOARD_A" domain0_remote_guard_selftest.sh "$REMOTE_GUARD_TEST_LOCAL" "$REMOTE_GUARD_TEST" || return 1
  send_verified_helper "$BOARD_B" domain0_remote_guard_selftest.sh "$REMOTE_GUARD_TEST_LOCAL" "$REMOTE_GUARD_TEST" || return 1
  send_verified_helper "$BOARD_A" cyclonedds_board_a.xml "$CYCLONE_XML_LOCAL" "$CYCLONE_XML_REMOTE" || return 1
  printf 'name=domain0_chatter_probe.py host_sha256=%s\n' "$(sha_local "$PROBE_LOCAL")" >> "$LOGDIR/artifact_hashes.txt"
  printf 'name=domain0_remote_guard.sh host_sha256=%s\n' "$(sha_local "$REMOTE_GUARD_LOCAL")" >> "$LOGDIR/artifact_hashes.txt"
  printf 'name=domain0_remote_spawn.py host_sha256=%s\n' "$(sha_local "$REMOTE_SPAWN_LOCAL")" >> "$LOGDIR/artifact_hashes.txt"
  printf 'name=domain0_remote_guard_selftest.sh host_sha256=%s\n' "$(sha_local "$REMOTE_GUARD_TEST_LOCAL")" >> "$LOGDIR/artifact_hashes.txt"
  printf 'name=run_domain0_chatter_smoke.sh host_sha256=%s\n' "$(sha_local "$0")" >> "$LOGDIR/artifact_hashes.txt"
  printf 'name=domain0_chatter_probe.bat host_sha256=%s\n' "$(sha_local "$PC_BATCH_LOCAL")" >> "$LOGDIR/artifact_hashes.txt"
  printf 'name=domain0_chatter_guard.ps1 host_sha256=%s\n' "$(sha_local "$PC_GUARD_LOCAL")" >> "$LOGDIR/artifact_hashes.txt"
  for path in "$PC_WS/shell_hook.bat" "$PC_WS/ros2-windows/setup.bat"; do
    if [ -f "$path" ]; then
      printf 'name=pc_%s host_sha256=%s\n' "$(basename "$path")" "$(sha_local "$path")" >> "$LOGDIR/artifact_hashes.txt"
    else
      printf 'name=pc_%s host_sha256=MISSING\n' "$(basename "$path")" >> "$LOGDIR/artifact_hashes.txt"
      return 1
    fi
  done
}

parse_board_record() { # <tag>, stdin -> PID:START:PGID:CHILD_PID:CHILD_START
  local tag="$1"
  tr -d '\r' | sed -n "s/^D0_LAUNCH_RECORD RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=$tag PID=\\([0-9][0-9]*\\) START=\\([0-9][0-9]*\\) PGID=\\([0-9][0-9]*\\) CHILD_PID=\\([0-9][0-9]*\\) CHILD_START=\\([0-9][0-9]*\\)$/\\1:\\2:\\3:\\4:\\5/p" | head -1
}

launch_board() { # <board> <env prefix> <payload> <log name>
  local board="$1" env_prefix="$2" payload="$3" log="$4" tag record owner out raw pair pid start pgid child_pid child_start
  ensure_remote_owner "$board" || return 1
  LAUNCH_SEQUENCE=$((LAUNCH_SEQUENCE + 1))
  tag="${LAUNCH_SEQUENCE}_${RANDOM}_$$"
  record="$REMOTE_LOGDIR/launch/${log}.${tag}.pid"
  owner="D0_RUN_OWNER RUN_ID=$RUN_ID NONCE=$RUN_NONCE"
  out="$(shell "$board" "mkdir -p $(remote_quote "$REMOTE_LOGDIR/launch") && chmod 700 $(remote_quote "$REMOTE_GUARD") && $(remote_quote "$REMOTE_PYTHON") $(remote_quote "$REMOTE_SPAWN") $(remote_quote "$REMOTE_GUARD") $(remote_quote "$REMOTE_OWNER") $(remote_quote "$record") $(remote_quote "$REMOTE_LOGDIR/$log") $(remote_quote "$RUN_ID") $(remote_quote "$RUN_NONCE") $(remote_quote "$tag") $(remote_quote "$env_prefix") $(remote_quote "$payload")" || true)"
  pair=""
  for _ in $(seq 1 30); do
    raw="$(shell "$board" "if test -f $(remote_quote "$record") && test ! -L $(remote_quote "$record"); then cat $(remote_quote "$record"); fi" || true)"
    pair="$(printf '%s' "$raw" | parse_board_record "$tag")"
    [[ "$pair" =~ ^[0-9]+:[0-9]+:[0-9]+:[0-9]+:[0-9]+$ ]] && break
    sleep 0.2
  done
  IFS=':' read -r pid start pgid child_pid child_start <<< "$pair"
  if ! [[ "$pid" =~ ^[0-9]+$ && "$start" =~ ^[0-9]+$ && "$pgid" =~ ^[0-9]+$ &&
          "$child_pid" =~ ^[0-9]+$ && "$child_start" =~ ^[0-9]+$ && "$pid" = "$pgid" ]]; then
    echo "ERROR: no exact board launch record for $log (HDC output: $(printf '%s' "$out" | head -1))" >&2
    return 1
  fi
  BOARD_TRACKED+=("$board|$pid|$start|$pgid|$child_pid|$child_start|$record|$tag|$log")
  printf 'board=%s pid=%s start=%s pgid=%s child_pid=%s child_start=%s record=%s tag=%s log=%s\n' \
    "$board" "$pid" "$start" "$pgid" "$child_pid" "$child_start" "$record" "$tag" "$log" >> "$LOGDIR/board_launch_records.txt"
}

board_record_matches() { # <board> <pid> <start> <pgid> <child_pid> <child_start> <record> <tag>
  local raw expected
  raw="$(shell "$1" "if test -f $(remote_quote "$7") && test ! -L $(remote_quote "$7"); then cat $(remote_quote "$7"); fi" || true)"
  expected="D0_LAUNCH_RECORD RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=$8 PID=$2 START=$3 PGID=$4 CHILD_PID=$5 CHILD_START=$6"
  printf '%s\n' "$raw" | tr -d '\r' | grep -Fqx "$expected"
}

stop_board_entry() { # board|pid|start|pgid|child_pid|child_start|record|tag|log
  local entry="$1" board pid start pgid child_pid child_start record tag log out
  IFS='|' read -r board pid start pgid child_pid child_start record tag log <<< "$entry"
  board_record_matches "$board" "$pid" "$start" "$pgid" "$child_pid" "$child_start" "$record" "$tag" || {
    echo "ERROR: refusing board cleanup with mismatched record: $entry" >&2
    return 1
  }
  out="$(shell "$board" "chmod 700 $(remote_quote "$REMOTE_GUARD") && $(remote_quote "$REMOTE_GUARD") --cleanup $(remote_quote "$record") $(remote_quote "$RUN_ID") $(remote_quote "$RUN_NONCE") $(remote_quote "$tag") $pid $start $pgid $child_pid $child_start" | tr -d '\r\n')"
  case "$out" in
    'D0_REMOTE_CLEANUP result=GONE'|'D0_REMOTE_CLEANUP result=STOPPED'|'D0_REMOTE_CLEANUP result=HARD_STOPPED') return 0 ;;
    *) echo "ERROR: board process-group cleanup failed: ${out:-NO_MARKER} entry=$entry" >&2; return 1 ;;
  esac
}

ensure_pc_owner() {
  local line="D0_PC_RUN_OWNER RUN_ID=$RUN_ID NONCE=$RUN_NONCE"
  if [ -e "$LOCAL_OWNER" ]; then
    [ -f "$LOCAL_OWNER" ] && grep -Fqx "$line" "$LOCAL_OWNER"
    return
  fi
  (set -C; umask 077; printf '%s\n' "$line" > "$LOCAL_OWNER") 2>/dev/null || return 1
  [ -f "$LOCAL_OWNER" ] && grep -Fqx "$line" "$LOCAL_OWNER"
}

parse_pc_record() { # <tag>, stdin -> PID:START
  local tag="$1"
  tr -d '\r' | sed -n "s/^D0_PC_RECORD RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=$tag PID=\\([0-9][0-9]*\\) START=\\([0-9][0-9]*\\)$/\\1:\\2/p" | head -1
}

pc_start() { # <tag> <mode> <direction>
  local tag="$1" mode="$2" direction="$3" record status stdout stderr pair pid start guard
  [[ "$tag" =~ ^[A-Za-z0-9_-]+$ && "$mode" =~ ^(pub|sub)$ && "$direction" =~ ^(pc_to_b|b_to_pc)$ ]] || return 1
  ensure_pc_owner || return 1
  record="$LOGDIR/pc/${tag}.record"
  status="$LOGDIR/pc/${tag}.status"
  stdout="$LOGDIR/${tag}.log"
  stderr="$LOGDIR/${tag}.err"
  guard="$PC_GUARD"
  powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$guard" \
    -OwnerFile "$(cygpath -w "$LOCAL_OWNER")" -RecordFile "$(cygpath -w "$record")" \
    -StatusFile "$(cygpath -w "$status")" -BatchFile "$PC_BATCH" \
    -WorkingDirectory "$(cygpath -w "$PC_WS")" -StdoutFile "$(cygpath -w "$stdout")" \
    -StderrFile "$(cygpath -w "$stderr")" -RunId "$RUN_ID" -Nonce "$RUN_NONCE" -Tag "$tag" \
    -Role pc -Mode "$mode" -Direction "$direction" -Token "$RUN_TOKEN" \
    -Count "$COUNT" -PayloadBytes "$PAYLOAD_BYTES" \
    > "$LOGDIR/pc/${tag}.guard.stdout" 2> "$LOGDIR/pc/${tag}.guard.stderr" &
  for _ in $(seq 1 50); do
    pair="$(cat "$record" 2>/dev/null | parse_pc_record "$tag")"
    [[ "$pair" =~ ^[0-9]+:[0-9]+$ ]] && break
    [ -f "$status" ] && break
    sleep 0.2
  done
  pid=${pair%%:*}
  start=${pair#*:}
  if ! [[ "$pid" =~ ^[0-9]+$ && "$start" =~ ^[0-9]+$ ]]; then
    echo "ERROR: no exact PC guard record for $tag" >&2
    return 1
  fi
  PC_TRACKED+=("$pid|$start|$record|$status|$tag")
  printf 'pid=%s start=%s record=%s status=%s tag=%s stdout=%s stderr=%s\n' \
    "$pid" "$start" "$record" "$status" "$tag" "$stdout" "$stderr" >> "$LOGDIR/pc_launch_records.txt"
}

wait_pc_exit() { # <tag>
  local tag="$1" status expected
  status="$LOGDIR/pc/${tag}.status"
  expected="D0_PC_EXIT RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=$tag RC=0"
  for _ in $(seq 1 90); do
    if [ -f "$status" ]; then
      grep -Fqx "$expected" "$status" && return 0
      echo "ERROR: PC guard returned a nonzero/malformed status for $tag" >&2
      return 1
    fi
    sleep 1
  done
  echo "ERROR: PC guard did not terminate for $tag" >&2
  return 1
}

stop_pc_entry() { # pid|start|record|status|tag
  local entry="$1" pid start record status tag expected out
  IFS='|' read -r pid start record status tag <<< "$entry"
  expected="D0_PC_RECORD RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=$tag PID=$pid START=$start"
  grep -Fqx "$expected" "$record" 2>/dev/null || {
    echo "ERROR: refusing PC cleanup with mismatched record: $entry" >&2
    return 1
  }
  D0_PID="$pid"
  D0_START="$start"
  export D0_PID D0_START
  out="$(powershell -NoProfile -NonInteractive -Command '
    $p = Get-Process -Id ([int]$env:D0_PID) -ErrorAction SilentlyContinue
    if ($null -eq $p) { Write-Output GONE; exit 0 }
    if ($p.StartTime.ToUniversalTime().ToFileTimeUtc() -ne [int64]$env:D0_START) { Write-Output REUSED; exit 3 }
    & taskkill.exe /PID ([int]$env:D0_PID) /T /F | Out-Null
    if ($LASTEXITCODE -ne 0) {
      if ($null -eq (Get-Process -Id ([int]$env:D0_PID) -ErrorAction SilentlyContinue)) { Write-Output GONE; exit 0 }
      Write-Output SIGNAL_FAILED; exit 4
    }
    for ($i = 0; $i -lt 20; ++$i) { Start-Sleep -Milliseconds 100; if ($null -eq (Get-Process -Id ([int]$env:D0_PID) -ErrorAction SilentlyContinue)) { Write-Output STOPPED; exit 0 } }
    Write-Output LIVE; exit 5
  ' 2>&1 || true)"
  unset D0_PID D0_START
  case "$out" in *GONE*|*STOPPED*) return 0 ;; *) echo "ERROR: PC guard cleanup failed: $out" >&2; return 1 ;; esac
}

stop_all_owned() {
  local i rc=0 entry
  local -a board_remaining=()
  local -a pc_remaining=()
  for ((i=${#BOARD_TRACKED[@]} - 1; i >= 0; --i)); do
    entry="${BOARD_TRACKED[$i]}"
    if ! stop_board_entry "$entry"; then
      board_remaining+=("$entry")
      rc=1
    fi
  done
  BOARD_TRACKED=("${board_remaining[@]}")
  for ((i=${#PC_TRACKED[@]} - 1; i >= 0; --i)); do
    entry="${PC_TRACKED[$i]}"
    if ! stop_pc_entry "$entry"; then
      pc_remaining+=("$entry")
      rc=1
    fi
  done
  PC_TRACKED=("${pc_remaining[@]}")
  return "$rc"
}

cleanup() {
  local prior=$?
  trap - EXIT INT TERM
  if stop_all_owned >> "$LOGDIR/cleanup.txt" 2>&1; then
    release_activity_locks >> "$LOGDIR/cleanup.txt" 2>&1 || prior=1
  else
    prior=1
    printf 'D0_ACTIVITY_LOCK_RETAINED reason=owned_process_group_not_clean\n' >> "$LOGDIR/cleanup.txt"
  fi
  exit "$prior"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

pull_board_log() { # <board> <name>
  local board="$1" name="$2" output tmp
  output="$LOGDIR/$name"
  tmp="$output.tmp.$$.$RANDOM"
  shell "$board" "if test -f $(remote_quote "$REMOTE_LOGDIR/$name") && test ! -L $(remote_quote "$REMOTE_LOGDIR/$name") && grep -Fqx $(remote_quote "D0_RUN_ID=$RUN_ID") $(remote_quote "$REMOTE_LOGDIR/$name") && grep -Fqx $(remote_quote "D0_RUN_NONCE=$RUN_NONCE") $(remote_quote "$REMOTE_LOGDIR/$name"); then cat $(remote_quote "$REMOTE_LOGDIR/$name"); else echo D0_LOG_MISSING; fi" > "$tmp" 2>/dev/null || true
  grep -Fqx "D0_RUN_ID=$RUN_ID" "$tmp" && grep -Fqx "D0_RUN_NONCE=$RUN_NONCE" "$tmp" || {
    mv -f "$tmp" "$output.incomplete" 2>/dev/null || true
    echo "ERROR: cannot collect fresh board log $name" >&2
    return 1
  }
  mv -f "$tmp" "$output"
}

wait_gateway_ready() { # <gateway log>
  local log="$1" result
  for _ in $(seq 1 45); do
    result="$(shell "$BOARD_A" "if test -f $(remote_quote "$REMOTE_LOGDIR/$log") && grep -Fq 'mdds transports requested=[dsoftbus] active=[dsoftbus(' $(remote_quote "$REMOTE_LOGDIR/$log") && grep -Fq 'cyclone domain 0, mdds domain 0' $(remote_quote "$REMOTE_LOGDIR/$log"); then printf D0_GATEWAY_READY; elif test -f $(remote_quote "$REMOTE_LOGDIR/$log") && grep -Fq 'mdds_gateway up:' $(remote_quote "$REMOTE_LOGDIR/$log"); then printf D0_GATEWAY_WRONG; else printf D0_GATEWAY_WAIT; fi" | tr -d '\r\n')"
    printf 'result=%s\n' "${result:-NO_MARKER}" >> "$LOGDIR/gateway_ready_poll.txt"
    [ "$result" = D0_GATEWAY_READY ] && return 0
    [ "$result" = D0_GATEWAY_WRONG ] && return 1
    sleep 1
  done
  return 1
}

wait_board_result() { # <log> <role> <direction>
  local log="$1" role="$2" direction="$3" marker
  for _ in $(seq 1 90); do
    marker="$(shell "$BOARD_B" "if grep -Eq '^D0_CHATTER_(SUB|PUB)_RESULT role=$role direction=$direction token=$RUN_TOKEN ' $(remote_quote "$REMOTE_LOGDIR/$log") 2>/dev/null; then printf D0_BOARD_RESULT; else printf D0_BOARD_WAIT; fi" | tr -d '\r\n')"
    [ "$marker" = D0_BOARD_RESULT ] && return 0
    sleep 1
  done
  return 1
}

start_gateway() { # <log>
  local env
  env=". $DEVICE_DIR/env.sh; unset RMW_IMPLEMENTATION MDDS_DEPLOYMENT_PROFILE MDDS_TRANSPORT MDDS_UDP_PEER_ALLOW ROS_DOMAIN_ID; export CYCLONEDDS_URI=$CYCLONE_XML_REMOTE; export MDDS_DEBUG=1; export RCUTILS_LOGGING_BUFFERED_STREAM=0;"
  launch_board "$BOARD_A" "$env" "$GATEWAY_BIN -c $GATEWAY_PROFILE_REMOTE" "$1" || return 1
  wait_gateway_ready "$1"
}

start_board_probe() { # <mode> <direction> <log>
  local mode="$1" direction="$2" log="$3" env payload
  env=". $DEVICE_DIR/env.sh; . $RMW_PROFILE_REMOTE; export ROS_DOMAIN_ID=0; export MDDS_DEBUG=1;"
  payload="python3.12 $PROBE_REMOTE --role board_b --mode $mode --direction $direction --token $RUN_TOKEN --topic $TOPIC --count $COUNT --payload-bytes $PAYLOAD_BYTES --rate-hz 5 --match-timeout-s 45 --receive-timeout-s 45 --settle-ms 3000 --flush-s 3 --quiet-s 2"
  launch_board "$BOARD_B" "$env" "$payload" "$log"
}

assert_probe_pub() { # <log> <role> <direction>
  local path="$LOGDIR/$1" role="$2" direction="$3"
  grep -Fqx "D0_CHATTER_PUB_RESULT role=$role direction=$direction token=$RUN_TOKEN sent=$COUNT/$COUNT match_count=1 error=none result=PASS" "$path"
}

assert_probe_sub() { # <log> <role> <direction>
  local path="$LOGDIR/$1" role="$2" direction="$3"
  grep -Fqx "D0_CHATTER_SUB_RESULT role=$role direction=$direction token=$RUN_TOKEN received=$COUNT/$COUNT lost=0 reorder=0 crc=0 body_mismatch=0 token_mismatch=0 direction_mismatch=0 malformed=0 foreign=0 error=none result=PASS" "$path"
}

assert_dsoftbus_trace() { # <log>
  local path="$LOGDIR/$1"
  grep -Eq '\[mdds/dsoftbus\] Socket\(' "$path" && \
    grep -Eq '\[mdds/dsoftbus\] Listen\(fd=[0-9]+\)=0' "$path" && \
    grep -Eq '\[mdds/dsoftbus\] OnBind\(fd=[0-9]+ peer=' "$path" && \
    grep -Eq '\[mdds/dsoftbus\] SendBytes\(fd=[0-9]+ len=[0-9]+\)=0' "$path" && \
    grep -Eq '\[mdds/dsoftbus\] OnBytes\(fd=[0-9]+ len=[1-9][0-9]* peer=' "$path"
}

onbind_peer_id() { # <log>
  grep -E '\[mdds/dsoftbus\] OnBind\(fd=[0-9]+ peer=[0-9A-Fa-f]{64}\)' "$1" | \
    sed -n 's/.* peer=\([0-9A-Fa-f]\{64\}\)).*/\1/p' | sort -u
}

assert_dsoftbus_leg() { # <endpoint log> <gateway log> <direction>
  local endpoint="$LOGDIR/$1" gateway="$LOGDIR/$2" direction="$3"
  local endpoint_peer gateway_peer endpoint_local gateway_local
  local endpoint_binds gateway_binds active active_local active_peer active_success passive_binds
  assert_dsoftbus_trace "$1" && assert_dsoftbus_trace "$2" || return 1
  endpoint_peer="$(onbind_peer_id "$endpoint")"
  gateway_peer="$(onbind_peer_id "$gateway")"
  [[ "$endpoint_peer" =~ ^[0-9A-Fa-f]{64}$ && "$gateway_peer" =~ ^[0-9A-Fa-f]{64}$ ]] || return 1
  endpoint_local="$gateway_peer"
  gateway_local="$endpoint_peer"
  [ "$endpoint_local" != "$gateway_local" ] || return 1
  endpoint_binds="$(grep -Ec '\[mdds/dsoftbus\] BindAsync\(' "$endpoint" || true)"
  gateway_binds="$(grep -Ec '\[mdds/dsoftbus\] BindAsync\(' "$gateway" || true)"
  if [[ "$endpoint_local" > "$gateway_local" ]]; then
    active=endpoint
    active_local="$endpoint_local"
    active_peer="$gateway_local"
    active_success="$(grep -Ec "\\[mdds/dsoftbus\\] BindAsync\\(fd=[0-9]+ peer=$active_peer\\)=0" "$endpoint" || true)"
    passive_binds="$gateway_binds"
  else
    active=gateway
    active_local="$gateway_local"
    active_peer="$endpoint_local"
    active_success="$(grep -Ec "\\[mdds/dsoftbus\\] BindAsync\\(fd=[0-9]+ peer=$active_peer\\)=0" "$gateway" || true)"
    passive_binds="$endpoint_binds"
  fi
  printf 'D0_DIALER_DECISION direction=%s endpoint_local=%s gateway_local=%s active=%s active_success=%s endpoint_bind_calls=%s gateway_bind_calls=%s passive_bind_calls=%s result=%s\n' \
    "$direction" "$endpoint_local" "$gateway_local" "$active" "$active_success" "$endpoint_binds" "$gateway_binds" "$passive_binds" \
    "$([ "$active_success" -ge 1 ] && [ "$passive_binds" -eq 0 ] && echo PASS || echo FAIL)" >> "$DIALER_DECISION_LOG"
  [ "$active_success" -ge 1 ] && [ "$passive_binds" -eq 0 ]
}

assert_board_exit() { # <log>
  grep -Eq "^D0_REMOTE_EXIT RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=[A-Za-z0-9_]+ RC=0$" "$LOGDIR/$1"
}

assert_gateway_transport() { # <gateway log>
  grep -Fq 'mdds transports requested=[dsoftbus] active=[dsoftbus(' "$LOGDIR/$1" && \
    ! grep -Fq 'udp(' "$LOGDIR/$1"
}

assert_gateway_exact() { # <gateway log> <direction>
  local path="$LOGDIR/$1" direction="$2" line forwarded
  line="$(grep -F "$TOPIC final:" "$path" | tail -n 1)"
  [ -n "$line" ] || return 1
  case "$direction" in
    pc_to_b)
      forwarded="$(sed -n 's/.*cyclone->mdds=\([0-9][0-9]*\).*/\1/p' <<< "$line")"
      [ "$forwarded" = "$COUNT" ] || return 1
      grep -Fq 'c2m_terminal=0' <<< "$line" || return 1
      grep -Fq 'c2m_write_rejections=0' <<< "$line" || return 1
      grep -Fq 'c2m_callback_exceptions=0' <<< "$line" || return 1
      grep -Fq 'c2m_invalid_serialized_messages=0' <<< "$line" || return 1
      grep -Fq 'c2m_history_sample_rejections=0' <<< "$line" || return 1
      ;;
    b_to_pc)
      forwarded="$(sed -n 's/.*mdds->cyclone=\([0-9][0-9]*\).*/\1/p' <<< "$line")"
      [ "$forwarded" = "$COUNT" ] || return 1
      grep -Fq 'm2c_terminal=0' <<< "$line" || return 1
      grep -Fq 'm2c_messages_lost=0' <<< "$line" || return 1
      grep -Fq 'm2c_resource_drops=0' <<< "$line" || return 1
      ;;
    *) return 1 ;;
  esac
}

run_leg_pc_to_b() {
  local bad=0
  start_board_probe sub pc_to_b d0_pc_to_b_board_sub.log || bad=1
  [ "$bad" -eq 0 ] && start_gateway d0_pc_to_b_gateway.log || bad=1
  [ "$bad" -eq 0 ] && pc_start d0_pc_to_b_pc_pub pub pc_to_b || bad=1
  [ "$bad" -eq 0 ] && wait_pc_exit d0_pc_to_b_pc_pub || bad=1
  [ "$bad" -eq 0 ] && wait_board_result d0_pc_to_b_board_sub.log board_b pc_to_b || bad=1
  stop_all_owned || bad=1
  pull_board_log "$BOARD_B" d0_pc_to_b_board_sub.log || bad=1
  pull_board_log "$BOARD_A" d0_pc_to_b_gateway.log || bad=1
  assert_probe_pub d0_pc_to_b_pc_pub.log pc pc_to_b || bad=1
  assert_probe_sub d0_pc_to_b_board_sub.log board_b pc_to_b || bad=1
  assert_gateway_exact d0_pc_to_b_gateway.log pc_to_b || bad=1
  assert_gateway_transport d0_pc_to_b_gateway.log || bad=1
  assert_dsoftbus_leg d0_pc_to_b_board_sub.log d0_pc_to_b_gateway.log pc_to_b || bad=1
  assert_board_exit d0_pc_to_b_board_sub.log || bad=1
  assert_board_exit d0_pc_to_b_gateway.log || bad=1
  printf 'D0_LEG_RESULT direction=pc_to_b count=%s result=%s\n' "$COUNT" "$([ "$bad" -eq 0 ] && echo PASS || echo FAIL)" >> "$LOGDIR/domain0_machine_result.txt"
  [ "$bad" -eq 0 ]
}

run_leg_b_to_pc() {
  local bad=0
  pc_start d0_b_to_pc_pc_sub sub b_to_pc || bad=1
  [ "$bad" -eq 0 ] && start_gateway d0_b_to_pc_gateway.log || bad=1
  [ "$bad" -eq 0 ] && start_board_probe pub b_to_pc d0_b_to_pc_board_pub.log || bad=1
  [ "$bad" -eq 0 ] && wait_board_result d0_b_to_pc_board_pub.log board_b b_to_pc || bad=1
  [ "$bad" -eq 0 ] && wait_pc_exit d0_b_to_pc_pc_sub || bad=1
  stop_all_owned || bad=1
  pull_board_log "$BOARD_B" d0_b_to_pc_board_pub.log || bad=1
  pull_board_log "$BOARD_A" d0_b_to_pc_gateway.log || bad=1
  assert_probe_sub d0_b_to_pc_pc_sub.log pc b_to_pc || bad=1
  assert_probe_pub d0_b_to_pc_board_pub.log board_b b_to_pc || bad=1
  assert_gateway_exact d0_b_to_pc_gateway.log b_to_pc || bad=1
  assert_gateway_transport d0_b_to_pc_gateway.log || bad=1
  assert_dsoftbus_leg d0_b_to_pc_board_pub.log d0_b_to_pc_gateway.log b_to_pc || bad=1
  assert_board_exit d0_b_to_pc_board_pub.log || bad=1
  assert_board_exit d0_b_to_pc_gateway.log || bad=1
  printf 'D0_LEG_RESULT direction=b_to_pc count=%s result=%s\n' "$COUNT" "$([ "$bad" -eq 0 ] && echo PASS || echo FAIL)" >> "$LOGDIR/domain0_machine_result.txt"
  [ "$bad" -eq 0 ]
}

printf 'D0_DOMAIN0_SMOKE_BEGIN run_id=%s nonce=%s token=%s topic=%s\n' "$RUN_ID" "$RUN_NONCE" "$RUN_TOKEN" "$TOPIC"
validate_local_contract || exit 1
acquire_activity_lock "$BOARD_A" || exit 1
acquire_activity_lock "$BOARD_B" || exit 1
preflight_board "$BOARD_A" board_a || exit 1
preflight_board "$BOARD_B" board_b || exit 1
preflight_pc || exit 1
verify_artifacts_and_helpers || exit 1

: > "$LOGDIR/domain0_machine_result.txt"
: > "$DIALER_DECISION_LOG"
run_leg_pc_to_b || exit 1
run_leg_b_to_pc || exit 1
printf 'D0_DOMAIN0_SMOKE_RESULT run_id=%s nonce=%s topic=%s pc_to_b=PASS b_to_pc=PASS result=PASS\n' \
  "$RUN_ID" "$RUN_NONCE" "$TOPIC" | tee -a "$LOGDIR/domain0_machine_result.txt"
