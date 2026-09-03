#!/usr/bin/env bash
# Deploy only the reviewed MDDS/RMW/gateway artifacts with a recoverable,
# hash-gated replacement.  This intentionally does not call deploy_ohos.sh:
# that broad deployment rewrites the whole ROS tree and would weaken the
# source-artifact-test binding of a verification run.
#
# The unit of deployment is the complete target list below, not an individual
# file: all local inputs are checked first, then all targets are staged and
# hashed, then every old target is retained as a verified backup.  Only then
# are replacements committed.  A commit failure restores every target whose
# replacement may have started, and verifies every restored hash.
set -uo pipefail

cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A="${BOARD_A:-3e01ff55454d202020104033bf453b00}"
BOARD_B="${BOARD_B:-3e01ff55454d202020104433991c3b00}"
DEVICE_DIR="${DEVICE_DIR:-/data/local/tmp/ros2}"
RUN_ID="${MDDS_DEPLOY_RUN_ID:-deploy_$(date +%Y%m%d_%H%M%S)_$RANDOM}"
RUN_NONCE="${MDDS_DEPLOY_RUN_NONCE:-nonce_${RANDOM}_${RANDOM}_$$}"
TEST_FAIL_AFTER_COMMITS="${MDDS_DEPLOY_TEST_FAIL_AFTER_COMMITS:-0}"

STAGE_ROOT="$DEVICE_DIR/.mdds-staging/$RUN_ID"
ROLLBACK_ROOT="$DEVICE_DIR/.mdds-rollbacks/$RUN_ID"
LOCK_DIR="$DEVICE_DIR/.mdds-activity-lock"

export MSYS2_ARG_CONV_EXCL='*'

declare -a TARGET_LABEL TARGET_BOARD TARGET_LOCAL TARGET_RELATIVE TARGET_KIND
declare -a TARGET_WANT TARGET_REMOTE TARGET_STAGE TARGET_BACKUP TARGET_OLD_SHA
declare -a TARGET_ATTEMPTED TARGET_COMMITTED LOCKED_BOARDS
# `declare -a` alone leaves an array unset under `set -u` on some Bash
# versions.  Early connection/configuration failures call release_all_locks()
# before the first successful acquire_lock(), so make every transaction array
# explicitly empty before any error path or trap can inspect it.
TARGET_LABEL=()
TARGET_BOARD=()
TARGET_LOCAL=()
TARGET_RELATIVE=()
TARGET_KIND=()
TARGET_WANT=()
TARGET_REMOTE=()
TARGET_STAGE=()
TARGET_BACKUP=()
TARGET_OLD_SHA=()
TARGET_ATTEMPTED=()
TARGET_COMMITTED=()
LOCKED_BOARDS=()
COMMIT_PHASE=0
ROLLBACK_RUNNING=0

shell() { "$HDC" -t "$1" shell "$2" </dev/null; }
send() { "$HDC" -t "$1" file send "$(cygpath -w "$2")" "$3" </dev/null >/dev/null; }
local_sha() { sha256sum "$1" | cut -d ' ' -f1; }

valid_sha256() {
  [[ "$1" =~ ^[0-9a-f]{64}$ ]]
}

# Values interpolated into remote shell strings must be deliberately narrow.
# DEVICE_DIR is also the root for staging/backup/lock paths, so reject broad,
# relative, whitespace-containing, and shell-active spellings rather than
# trying to quote arbitrary input.
validate_safe_posix_dir() {
  local value="$1"
  [[ "$value" =~ ^/([A-Za-z0-9][A-Za-z0-9._-]*)(/[A-Za-z0-9][A-Za-z0-9._-]*)*$ ]]
}

validate_safe_relative_path() {
  local value="$1"
  [[ "$value" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*(/[A-Za-z0-9][A-Za-z0-9._-]*)*$ ]]
}

validate_configuration() {
  if [[ ! "$RUN_ID" =~ ^[A-Za-z0-9_-]+$ ]]; then
    echo "ERROR: MDDS_DEPLOY_RUN_ID must contain only [A-Za-z0-9_-]" >&2
    return 1
  fi
  if [[ ! "$RUN_NONCE" =~ ^[A-Za-z0-9_-]+$ ]]; then
    echo "ERROR: MDDS_DEPLOY_RUN_NONCE must contain only [A-Za-z0-9_-]" >&2
    return 1
  fi
  if ! validate_safe_posix_dir "$DEVICE_DIR"; then
    echo "ERROR: DEVICE_DIR must be a non-root absolute POSIX path containing only safe components" >&2
    return 1
  fi
  if [[ ! "$BOARD_A" =~ ^[A-Za-z0-9_-]+$ || ! "$BOARD_B" =~ ^[A-Za-z0-9_-]+$ ]]; then
    echo "ERROR: BOARD_A and BOARD_B must contain only [A-Za-z0-9_-]" >&2
    return 1
  fi
  if [[ "$BOARD_A" = "$BOARD_B" ]]; then
    echo "ERROR: BOARD_A and BOARD_B must be distinct devices" >&2
    return 1
  fi
  if [[ ! "$TEST_FAIL_AFTER_COMMITS" =~ ^(0|[1-9][0-9]*)$ ]]; then
    echo "ERROR: MDDS_DEPLOY_TEST_FAIL_AFTER_COMMITS must be 0 or a positive decimal integer" >&2
    return 1
  fi
}

remote_regular_sha() {
  # remote_regular_sha <board> <absolute-safe-path>
  local board="$1" path="$2" got
  got="$(shell "$board" "if test -f '$path' && test ! -L '$path'; then sha256sum '$path' 2>/dev/null | cut -d ' ' -f1; fi" | tr -d '\r\n')"
  valid_sha256 "$got" || return 1
  printf '%s\n' "$got"
}

require_ready() {
  local board="$1" out
  out="$(shell "$board" 'printf MDDS_DEPLOY_HDC_READY' | tr -d '\r')"
  if [[ "$out" != "MDDS_DEPLOY_HDC_READY" ]]; then
    echo "ERROR: HDC target is not ready: $board" >&2
    return 1
  fi
}

# Do not overwrite mapped libraries or a live MDDS test/gateway.  The remote
# probe emits its sole success sentinel only after both ps -ef and the full
# /proc/*/maps scan succeed.  HDC does not reliably forward remote shell exit
# status, so an empty or partial reply must never be treated as quiescent.
# Build all process needles remotely from non-contiguous fragments: ps -ef can
# otherwise observe the hdc shell itself and falsely match its own command.
require_quiescent() {
  local board="$1" probe_out line last_line="" sentinel_count=0
  local live_process=0 malformed=0 mapped_pid
  if ! probe_out="$(shell "$board" "set -u
if ! test -d /proc || test -L /proc; then
  exit 41
fi
device_root='$DEVICE_DIR'
test_tree=\"\$device_root/tests/\"
gateway_path=\"\$device_root/lib/mdds_gateway/mdds_gateway\"
libmdds_path=\"\$device_root/lib/libmdds.so\"
librmw_path=\"\$device_root/lib/librmw_mdds.so\"
librmw_cyclonedds_path=\"\$device_root/lib/librmw_cyclonedds_cpp.so\"
known_dsoftbus=\$(printf '%s%s' dsoftbus _probe)
known_e2e=\$(printf '%s%s' mdds _e2e)
known_participant=\$(printf '%s%s' test_ participant)
if ! process_out=\$(ps -ef 2>/dev/null); then
  exit 42
fi
process_has_needle() {
  if printf '%s\\n' \"\$process_out\" | grep -F \"\$1\" >/dev/null 2>&1; then
    return 0
  else
    grep_rc=\$?
    if test \"\$grep_rc\" -eq 1; then
      return 1
    fi
    exit 47
  fi
}
if process_has_needle \"\$test_tree\"; then
  printf 'MDDS_DEPLOY_LIVE_PROCESS=TEST_TREE\\n'
fi
if process_has_needle \"\$gateway_path\"; then
  printf 'MDDS_DEPLOY_LIVE_PROCESS=GATEWAY\\n'
fi
if process_has_needle \"\$known_dsoftbus\"; then
  printf 'MDDS_DEPLOY_LIVE_PROCESS=KNOWN_TEST\\n'
fi
if process_has_needle \"\$known_e2e\"; then
  printf 'MDDS_DEPLOY_LIVE_PROCESS=KNOWN_TEST\\n'
fi
if process_has_needle \"\$known_participant\"; then
  printf 'MDDS_DEPLOY_LIVE_PROCESS=KNOWN_TEST\\n'
fi
for map in /proc/[0-9]*/maps; do
  if ! test -e \"\$map\"; then
    continue
  fi
  if ! test -r \"\$map\"; then
    exit 43
  fi
  mapped=0
  if grep -F \"\$libmdds_path\" \"\$map\" >/dev/null 2>&1; then
    mapped=1
  else
    grep_rc=\$?
    if test \"\$grep_rc\" -ne 1; then
      exit 44
    fi
  fi
  if grep -F \"\$librmw_path\" \"\$map\" >/dev/null 2>&1; then
    mapped=1
  else
    grep_rc=\$?
    if test \"\$grep_rc\" -ne 1; then
      exit 45
    fi
  fi
  if grep -F \"\$librmw_cyclonedds_path\" \"\$map\" >/dev/null 2>&1; then
    mapped=1
  else
    grep_rc=\$?
    if test \"\$grep_rc\" -ne 1; then
      exit 48
    fi
  fi
  if test \"\$mapped\" -eq 1; then
    pid=\${map#/proc/}
    pid=\${pid%/maps}
    case \"\$pid\" in
      ''|*[!0-9]*) exit 46 ;;
    esac
    printf 'MDDS_DEPLOY_MAPPED_PID=%s\\n' \"\$pid\"
  fi
done
printf 'MDDS_DEPLOY_QUIESCENCE_PROBE_OK\\n'" | tr -d '\r')"; then
    echo "ERROR: quiescence probe HDC/shell command failed on $board" >&2
    return 1
  fi

  # Reject HDC diagnostics, a truncated result, duplicate markers, or a
  # marker which was not the final record.  Raw ps output never crosses this
  # boundary: the remote side reduces it to fixed tokens first.
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ -z "$line" ]]; then
      malformed=1
      continue
    fi
    last_line="$line"
    case "$line" in
      MDDS_DEPLOY_QUIESCENCE_PROBE_OK)
        sentinel_count=$((sentinel_count + 1))
        ;;
      MDDS_DEPLOY_LIVE_PROCESS=TEST_TREE|MDDS_DEPLOY_LIVE_PROCESS=GATEWAY|MDDS_DEPLOY_LIVE_PROCESS=KNOWN_TEST)
        live_process=1
        ;;
      MDDS_DEPLOY_MAPPED_PID=*)
        mapped_pid="${line#MDDS_DEPLOY_MAPPED_PID=}"
        if [[ ! "$mapped_pid" =~ ^[0-9]+$ ]]; then
          malformed=1
        else
          live_process=1
        fi
        ;;
      *)
        malformed=1
        ;;
    esac
  done <<< "$probe_out"
  if (( malformed != 0 || sentinel_count != 1 )) || [[ "$last_line" != "MDDS_DEPLOY_QUIESCENCE_PROBE_OK" ]]; then
    echo "ERROR: quiescence probe on $board did not return one terminal success sentinel" >&2
    if [[ -n "$probe_out" ]]; then
      printf '%s\n' "$probe_out" >&2
    fi
    return 1
  fi
  if (( live_process != 0 )); then
    echo "ERROR: refusing targeted deploy while an MDDS verification process or library mapping is live on $board:" >&2
    printf '%s\n' "$probe_out" >&2
    return 1
  fi
}

require_all_quiescent() {
  # HDC can occasionally return an empty/partial shell reply while the board
  # remains healthy (notably during a /proc scan).  Never treat that as a
  # pass, but retry the same read-only probe a bounded number of times before
  # aborting the transaction.  A later commit still requires a fresh success
  # probe immediately before its atomic replacement.
  local board attempt
  for board in "$BOARD_A" "$BOARD_B"; do
    for attempt in 1 2 3; do
      if require_quiescent "$board"; then
        break
      fi
      if (( attempt == 3 )); then
        return 1
      fi
      printf 'DEPLOY_QUIESCENCE_RETRY board=%s attempt=%s next_attempt=%s\n' \
        "$board" "$attempt" "$((attempt + 1))" >&2
    done
  done
}

acquire_lock() {
  # The shared activity lock is also acquired by every board-test/DS/GW
  # launcher.  A lock conflict is fail-closed, so a test cannot begin while
  # this transaction is staging, backing up, committing, or rolling back.
  local board="$1" owner="MDDS_ACTIVITY_LOCK MODE=DEPLOY RUN_ID=$RUN_ID NONCE=$RUN_NONCE OWNER=deploy_mdds_final_artifacts" out
  out="$(shell "$board" "if (umask 077; mkdir '$LOCK_DIR') 2>/dev/null; then
    if (umask 077; set -C; printf '%s\\n' '$owner' > '$LOCK_DIR/owner') 2>/dev/null && \\
      test -d '$LOCK_DIR' && test ! -L '$LOCK_DIR' && \\
      test -f '$LOCK_DIR/owner' && test ! -L '$LOCK_DIR/owner' && \\
      test \"\$(cat '$LOCK_DIR/owner' 2>/dev/null)\" = '$owner'; then
      printf ACTIVITY_LOCK_ACQUIRED
    else
      printf ACTIVITY_LOCK_OWNER_WRITE_FAILED
    fi
  else
    printf ACTIVITY_LOCK_BUSY
  fi" | tr -d '\r\n')"
  if [[ "$out" != "ACTIVITY_LOCK_ACQUIRED" ]]; then
    echo "ERROR: cannot acquire MDDS activity lock on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi
  LOCKED_BOARDS+=("$board")
}

release_lock() {
  local board="$1" owner="MDDS_ACTIVITY_LOCK MODE=DEPLOY RUN_ID=$RUN_ID NONCE=$RUN_NONCE OWNER=deploy_mdds_final_artifacts" out
  out="$(shell "$board" "if test -d '$LOCK_DIR' && test ! -L '$LOCK_DIR' && \\
      test -f '$LOCK_DIR/owner' && test ! -L '$LOCK_DIR/owner' && \\
      test \"\$(cat '$LOCK_DIR/owner' 2>/dev/null)\" = '$owner'; then
    rm -f '$LOCK_DIR/owner' && rmdir '$LOCK_DIR' && printf ACTIVITY_LOCK_RELEASED
  else
    printf ACTIVITY_LOCK_NOT_OWNED
  fi" | tr -d '\r\n')"
  if [[ "$out" != "ACTIVITY_LOCK_RELEASED" ]]; then
    echo "ERROR: could not release MDDS activity lock on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi
}

release_all_locks() {
  local index failed=0
  for ((index=${#LOCKED_BOARDS[@]} - 1; index >= 0; --index)); do
    release_lock "${LOCKED_BOARDS[$index]}" || failed=1
  done
  LOCKED_BOARDS=()
  return "$failed"
}

add_target() {
  # add_target <label> <board> <local-path> <relative-path> <exec|data>
  local label="$1" board="$2" local_path="$3" relative="$4" kind="$5" index
  if [[ ! "$label" =~ ^[A-Za-z0-9_.-]+$ ]] || ! validate_safe_relative_path "$relative" ||
    [[ "$kind" != "exec" && "$kind" != "data" ]]; then
    echo "ERROR: internal target declaration is unsafe: $label/$relative/$kind" >&2
    return 1
  fi
  for index in "${!TARGET_BOARD[@]}"; do
    if [[ "${TARGET_BOARD[$index]}" = "$board" && "${TARGET_RELATIVE[$index]}" = "$relative" ]]; then
      echo "ERROR: duplicate deployment target: $board:$relative" >&2
      return 1
    fi
  done
  TARGET_LABEL+=("$label")
  TARGET_BOARD+=("$board")
  TARGET_LOCAL+=("$local_path")
  TARGET_RELATIVE+=("$relative")
  TARGET_KIND+=("$kind")
  TARGET_WANT+=("")
  TARGET_REMOTE+=("$DEVICE_DIR/$relative")
  TARGET_STAGE+=("$STAGE_ROOT/$relative")
  TARGET_BACKUP+=("$ROLLBACK_ROOT/$relative")
  TARGET_OLD_SHA+=("")
  TARGET_ATTEMPTED+=(0)
  TARGET_COMMITTED+=(0)
}

verify_local_artifacts() {
  local index path want
  for index in "${!TARGET_LOCAL[@]}"; do
    path="${TARGET_LOCAL[$index]}"
    if [[ ! -f "$path" || -L "$path" ]]; then
      echo "ERROR: local artifact must be a non-symlink regular file: $path" >&2
      return 1
    fi
    want="$(local_sha "$path")"
    if ! valid_sha256 "$want"; then
      echo "ERROR: cannot hash local artifact: $path" >&2
      return 1
    fi
    TARGET_WANT[$index]="$want"
    printf 'DEPLOY_LOCAL_ARTIFACT name=%s board=%s path=%s sha256=%s\n' \
      "${TARGET_LABEL[$index]}" "${TARGET_BOARD[$index]}" "${TARGET_RELATIVE[$index]}" "$want"
  done
}

target_want_by_label() {
  # target_want_by_label <label> <board> <relative-path>
  # Keep reporting tied to the declared target rather than its incidental
  # array position: deployment targets evolve as runtime dependencies are
  # added.
  local label="$1" board="$2" relative="$3" index
  for index in "${!TARGET_LABEL[@]}"; do
    if [[ "${TARGET_LABEL[$index]}" = "$label" &&
      "${TARGET_BOARD[$index]}" = "$board" &&
      "${TARGET_RELATIVE[$index]}" = "$relative" ]]; then
      printf '%s' "${TARGET_WANT[$index]}"
      return 0
    fi
  done
  return 1
}

stage_target() {
  local index="$1" board="${TARGET_BOARD[$index]}" local_path="${TARGET_LOCAL[$index]}"
  local stage="${TARGET_STAGE[$index]}" parent want="${TARGET_WANT[$index]}" out got
  parent="$(dirname "$stage")"
  out="$(shell "$board" "if test -e '$stage' || test -L '$stage'; then
    printf DEPLOY_STAGE_EXISTS
  elif mkdir -p '$parent'; then
    printf DEPLOY_STAGE_READY
  else
    printf DEPLOY_STAGE_MKDIR_FAILED
  fi" | tr -d '\r\n')"
  if [[ "$out" != "DEPLOY_STAGE_READY" ]]; then
    echo "ERROR: staging precondition failed for $board:${TARGET_RELATIVE[$index]}: ${out:-NO_MARKER}" >&2
    return 1
  fi
  if ! send "$board" "$local_path" "$stage"; then
    echo "ERROR: failed to stage $board:${TARGET_RELATIVE[$index]}" >&2
    return 1
  fi
  if ! got="$(remote_regular_sha "$board" "$stage")" || [[ "$got" != "$want" ]]; then
    echo "ERROR: staged hash mismatch for $board:${TARGET_RELATIVE[$index]} local=$want remote=${got:-MISSING}" >&2
    return 1
  fi
  if [[ "${TARGET_KIND[$index]}" = "exec" ]]; then
    out="$(shell "$board" "if chmod 0755 '$stage' && test -x '$stage'; then printf DEPLOY_STAGE_MODE_OK; else printf DEPLOY_STAGE_MODE_FAILED; fi" | tr -d '\r\n')"
    if [[ "$out" != "DEPLOY_STAGE_MODE_OK" ]]; then
      echo "ERROR: could not mark staged executable on $board:${TARGET_RELATIVE[$index]}" >&2
      return 1
    fi
    if ! got="$(remote_regular_sha "$board" "$stage")" || [[ "$got" != "$want" ]]; then
      echo "ERROR: staged executable hash changed unexpectedly for $board:${TARGET_RELATIVE[$index]}" >&2
      return 1
    fi
  fi
  printf 'DEPLOY_STAGED name=%s board=%s path=%s sha256=%s\n' \
    "${TARGET_LABEL[$index]}" "$board" "${TARGET_RELATIVE[$index]}" "$want"
}

stage_all() {
  local index
  for index in "${!TARGET_LABEL[@]}"; do
    stage_target "$index" || return 1
  done
}

backup_target() {
  # Use a hard link for a create-only backup.  All paths live under
  # DEVICE_DIR, so the link and later mv are same-filesystem atomic operations.
  local index="$1" board="${TARGET_BOARD[$index]}" remote="${TARGET_REMOTE[$index]}"
  local backup="${TARGET_BACKUP[$index]}" backup_parent old got out
  backup_parent="$(dirname "$backup")"
  if ! old="$(remote_regular_sha "$board" "$remote")"; then
    echo "ERROR: deployed target is missing, non-regular, or unhashed: $board:${TARGET_RELATIVE[$index]}" >&2
    return 1
  fi
  out="$(shell "$board" "if test -e '$backup' || test -L '$backup'; then
    printf DEPLOY_BACKUP_EXISTS
  elif mkdir -p '$backup_parent' && ln '$remote' '$backup' 2>/dev/null; then
    got=\$(sha256sum '$backup' 2>/dev/null | cut -d ' ' -f1)
    if test \"\$got\" = '$old'; then printf DEPLOY_BACKUP_OK:\$got; else printf DEPLOY_BACKUP_HASH_BAD:\$got; fi
  else
    printf DEPLOY_BACKUP_CREATE_FAILED
  fi" | tr -d '\r\n')"
  if [[ "$out" != "DEPLOY_BACKUP_OK:$old" ]]; then
    echo "ERROR: backup failed for $board:${TARGET_RELATIVE[$index]}: ${out:-NO_MARKER}" >&2
    return 1
  fi
  if ! got="$(remote_regular_sha "$board" "$backup")" || [[ "$got" != "$old" ]]; then
    echo "ERROR: independently verified backup hash mismatch for $board:${TARGET_RELATIVE[$index]}" >&2
    return 1
  fi
  TARGET_OLD_SHA[$index]="$old"
  printf 'DEPLOY_BACKUP name=%s board=%s path=%s sha256=%s\n' \
    "${TARGET_LABEL[$index]}" "$board" "${TARGET_RELATIVE[$index]}" "$old"
}

backup_all() {
  local index
  for index in "${!TARGET_LABEL[@]}"; do
    backup_target "$index" || return 1
  done
}

commit_target() {
  local index="$1" board="${TARGET_BOARD[$index]}" remote="${TARGET_REMOTE[$index]}"
  local stage="${TARGET_STAGE[$index]}" want="${TARGET_WANT[$index]}" out
  # The activity lock prevents cooperating launchers from racing deployment;
  # repeat the /proc/argv check before *every* mv as a defense for external
  # launchers that do not yet participate in the lock protocol.
  if ! require_all_quiescent; then
    echo "ERROR: commit precondition failed before $board:${TARGET_RELATIVE[$index]}" >&2
    return 1
  fi
  # Mark before the remote mv: if HDC loses the result after replacement, this
  # target still enters the shared rollback path instead of becoming mixed.
  TARGET_ATTEMPTED[$index]=1
  out="$(shell "$board" "if test -f '$stage' && test ! -L '$stage'; then
    got=\$(sha256sum '$stage' 2>/dev/null | cut -d ' ' -f1)
    if test \"\$got\" = '$want' && mv -f '$stage' '$remote'; then
      got=\$(sha256sum '$remote' 2>/dev/null | cut -d ' ' -f1)
      if test \"\$got\" = '$want'; then printf DEPLOY_COMMIT_OK:\$got; else printf DEPLOY_POST_HASH_BAD:\$got; fi
    else
      printf DEPLOY_COMMIT_MV_FAILED
    fi
  else
    printf DEPLOY_COMMIT_STAGE_INVALID
  fi" | tr -d '\r\n')"
  if [[ "$out" != "DEPLOY_COMMIT_OK:$want" ]]; then
    echo "ERROR: commit failed for $board:${TARGET_RELATIVE[$index]}: ${out:-NO_MARKER}" >&2
    return 1
  fi
  TARGET_COMMITTED[$index]=1
  printf 'DEPLOY_COMMITTED name=%s board=%s path=%s sha256=%s backup_sha256=%s\n' \
    "${TARGET_LABEL[$index]}" "$board" "${TARGET_RELATIVE[$index]}" "$want" "${TARGET_OLD_SHA[$index]}"
}

commit_all() {
  local index committed_count=0
  for index in "${!TARGET_LABEL[@]}"; do
    commit_target "$index" || return 1
    committed_count=$((committed_count + 1))
    if [[ "$TEST_FAIL_AFTER_COMMITS" != "0" && "$TEST_FAIL_AFTER_COMMITS" = "$committed_count" ]]; then
      echo "DEPLOY_TEST_FAILPOINT phase=commit after_commits=$committed_count" >&2
      return 1
    fi
  done
}

rollback_target() {
  # Rollback is itself atomic: create a unique hard link to the verified backup
  # and mv it over the target.  Keep the original backup for forensic recovery.
  local index="$1" board="${TARGET_BOARD[$index]}" remote="${TARGET_REMOTE[$index]}"
  local backup="${TARGET_BACKUP[$index]}" old="${TARGET_OLD_SHA[$index]}"
  local restore="${remote}.mdds-restore-${RUN_ID}" out
  if [[ -z "$old" ]] || ! valid_sha256 "$old"; then
    echo "ERROR: no verified backup hash for rollback of $board:${TARGET_RELATIVE[$index]}" >&2
    return 1
  fi
  out="$(shell "$board" "if test -e '$restore' || test -L '$restore'; then
    printf DEPLOY_ROLLBACK_STAGE_EXISTS
  elif test -f '$backup' && test ! -L '$backup'; then
    got=\$(sha256sum '$backup' 2>/dev/null | cut -d ' ' -f1)
    if test \"\$got\" = '$old' && ln '$backup' '$restore' 2>/dev/null && mv -f '$restore' '$remote'; then
      target=\$(sha256sum '$remote' 2>/dev/null | cut -d ' ' -f1)
      saved=\$(sha256sum '$backup' 2>/dev/null | cut -d ' ' -f1)
      if test \"\$target\" = '$old' && test \"\$saved\" = '$old'; then printf DEPLOY_ROLLBACK_OK:\$target; else printf DEPLOY_ROLLBACK_HASH_BAD:\$target:\$saved; fi
    else
      printf DEPLOY_ROLLBACK_MV_FAILED
    fi
  else
    printf DEPLOY_ROLLBACK_BACKUP_INVALID
  fi" | tr -d '\r\n')"
  if [[ "$out" != "DEPLOY_ROLLBACK_OK:$old" ]]; then
    echo "ERROR: rollback failed for $board:${TARGET_RELATIVE[$index]}: ${out:-NO_MARKER}" >&2
    return 1
  fi
  printf 'DEPLOY_ROLLBACK_OK name=%s board=%s path=%s sha256=%s\n' \
    "${TARGET_LABEL[$index]}" "$board" "${TARGET_RELATIVE[$index]}" "$old"
}

rollback_attempted() {
  local index failed=0
  ROLLBACK_RUNNING=1
  for ((index=${#TARGET_LABEL[@]} - 1; index >= 0; --index)); do
    [[ "${TARGET_ATTEMPTED[$index]}" = "1" ]] || continue
    rollback_target "$index" || failed=1
  done
  ROLLBACK_RUNNING=0
  return "$failed"
}

mask_activity_signals() {
  # Do not let an interrupted rollback release the shared lock midway through
  # restoring a mixed deployment.  The caller restores these handlers only
  # after every attempted target has been hash-verified and locks are released.
  trap '' INT TERM HUP
}

restore_activity_signal_handlers() {
  trap 'handle_interrupt INT' INT
  trap 'handle_interrupt TERM' TERM
  trap 'handle_interrupt HUP' HUP
}

handle_interrupt() {
  local signal="$1" rollback_ok=1
  mask_activity_signals
  echo "ERROR: interrupted by $signal" >&2
  if (( COMMIT_PHASE == 1 && ROLLBACK_RUNNING == 0 )); then
    if ! rollback_attempted; then
      rollback_ok=0
      echo "ERROR: rollback incomplete after $signal; activity locks stay retained with backups for manual recovery" >&2
    fi
  fi
  if (( rollback_ok == 1 )); then
    release_all_locks || echo "ERROR: activity lock release incomplete after $signal" >&2
  else
    echo "ERROR: refusing to release activity locks after incomplete rollback" >&2
  fi
  exit 128
}

trap 'handle_interrupt INT' INT
trap 'handle_interrupt TERM' TERM
trap 'handle_interrupt HUP' HUP

main() {
  local board
  validate_configuration || return 2

  # The transaction includes profiles too: a new binary with an old profile
  # would be another form of mixed deployment.  Deployment does not execute
  # the profile; the explicit log line below records that boundary for the
  # evidence manifest and later gateway test records.
  add_target libmdds "$BOARD_A" "install_ohos/lib/libmdds.so" "lib/libmdds.so" data || return 2
  add_target librmw_mdds "$BOARD_A" "install_ohos/lib/librmw_mdds.so" "lib/librmw_mdds.so" data || return 2
  add_target librmw_cyclonedds_cpp "$BOARD_A" "install_ohos/lib/librmw_cyclonedds_cpp.so" "lib/librmw_cyclonedds_cpp.so" data || return 2
  add_target rmw_mdds_dsoftbus_profile "$BOARD_A" "install_ohos/share/rmw_mdds/config/ohos_dsoftbus.env" "share/rmw_mdds/config/ohos_dsoftbus.env" data || return 2
  add_target libmdds "$BOARD_B" "install_ohos/lib/libmdds.so" "lib/libmdds.so" data || return 2
  add_target librmw_mdds "$BOARD_B" "install_ohos/lib/librmw_mdds.so" "lib/librmw_mdds.so" data || return 2
  add_target rmw_mdds_dsoftbus_profile "$BOARD_B" "install_ohos/share/rmw_mdds/config/ohos_dsoftbus.env" "share/rmw_mdds/config/ohos_dsoftbus.env" data || return 2
  # Board A is the sole gateway host in the requested topology.
  add_target mdds_gateway "$BOARD_A" "install_ohos/lib/mdds_gateway/mdds_gateway" "lib/mdds_gateway/mdds_gateway" exec || return 2
  add_target mdds_gateway_profile "$BOARD_A" "install_ohos/share/mdds_gateway/mdds_gateway_ohos_dsoftbus.conf" "share/mdds_gateway/mdds_gateway_ohos_dsoftbus.conf" data || return 2

  printf 'DEPLOY_RUN_ID=%s\n' "$RUN_ID"
  printf 'DEPLOY_RUN_NONCE=%s\n' "$RUN_NONCE"
  printf 'DEPLOY_ACTIVITY_LOCK mode=DEPLOY owner=deploy_mdds_final_artifacts path=%s\n' "$LOCK_DIR"
  printf 'DEPLOY_TEST_FAIL_AFTER_COMMITS=%s\n' "$TEST_FAIL_AFTER_COMMITS"
  verify_local_artifacts || return 2

  for board in "$BOARD_A" "$BOARD_B"; do
    if ! require_ready "$board"; then
      release_all_locks || true
      return 3
    fi
    if ! acquire_lock "$board"; then
      release_all_locks || true
      return 3
    fi
    if ! require_quiescent "$board"; then
      release_all_locks || true
      return 3
    fi
  done

  if ! stage_all; then
    echo "DEPLOY_ABORT phase=staging run_id=$RUN_ID (no target was replaced)" >&2
    release_all_locks || true
    return 4
  fi
  if ! backup_all; then
    echo "DEPLOY_ABORT phase=backup run_id=$RUN_ID (no target was replaced)" >&2
    release_all_locks || true
    return 4
  fi

  # A test could have started after the first precondition check.  The lock
  # serializes deployers; this second non-destructive check fences test runs
  # immediately before the first replacement.
  for board in "$BOARD_A" "$BOARD_B"; do
    if ! require_quiescent "$board"; then
      echo "DEPLOY_ABORT phase=precommit-quiescence run_id=$RUN_ID (no target was replaced)" >&2
      release_all_locks || true
      return 4
    fi
  done

  COMMIT_PHASE=1
  if ! commit_all; then
    echo "DEPLOY_COMMIT_FAILED run_id=$RUN_ID; restoring every attempted target" >&2
    mask_activity_signals
    if rollback_attempted; then
      echo "DEPLOY_ROLLBACK_COMPLETE run_id=$RUN_ID" >&2
      COMMIT_PHASE=0
      release_all_locks || true
      restore_activity_signal_handlers
    else
      echo "ERROR: DEPLOY_ROLLBACK_INCOMPLETE run_id=$RUN_ID; activity locks remain with verified backups under $ROLLBACK_ROOT" >&2
      COMMIT_PHASE=0
      restore_activity_signal_handlers
      return 5
    fi
    return 5
  fi
  COMMIT_PHASE=0

  if ! release_all_locks; then
    echo "ERROR: deployment committed, but one or more deploy locks could not be released" >&2
    return 6
  fi
  local gateway_profile_sha
  if ! gateway_profile_sha="$(target_want_by_label mdds_gateway_profile "$BOARD_A" \
      "share/mdds_gateway/mdds_gateway_ohos_dsoftbus.conf")"; then
    echo "ERROR: gateway profile target disappeared from deployment plan" >&2
    return 6
  fi
  printf 'DEPLOY_GATEWAY_PROFILE board=%s path=%s sha256=%s runtime_status=NOT_EXECUTED_BY_DEPLOY\n' \
    "$BOARD_A" "share/mdds_gateway/mdds_gateway_ohos_dsoftbus.conf" "$gateway_profile_sha"
  printf 'DEPLOY_COMPLETE run_id=%s stage_root=%s rollback_root=%s\n' "$RUN_ID" "$STAGE_ROOT" "$ROLLBACK_ROOT"
}

main "$@"
