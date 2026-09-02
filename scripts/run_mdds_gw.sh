#!/usr/bin/env bash
# mdds_gateway (L3) e2e orchestrator: PC (rmw_cyclonedds_cpp) <-> mdds_gateway
# on board A <-> mdds domain (boards A/B, rmw_mdds). Scenarios GW-01..GW-08 of
# docs/designs/mdds_test_plan.md. The PC side runs generated .bat files under
# C:\pixi_ws (Jazzy binary install, unicast cyclonedds toward board A).
#
#   ./scripts/run_mdds_gw.sh [gw01 ... gw08 | all]
# default/all: gw01 gw02 gw03 gw04 gw05 gw06 gw07 gw08
#
# Every board process launched here gets a persistent run-scoped PID:start
# record before its best-effort stdout token is emitted. Cleanup never searches
# for a process by command line or image name: that is unsafe on shared
# boards/PCs.
set -uo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00
BOARD_B=3e01ff55454d202020104433991c3b00
DEVICE_DIR=/data/local/tmp/ros2
# Verification callers may place this run's unnormalised console and fetched
# logs outside the source worktree, so the later source snapshot does not
# recursively capture its own evidence directory.  Preserve the historical
# path for ordinary developer runs.
LOGROOT="${MDDS_GW_LOGROOT:-ohos_test_logs/mdds_gw}"
# Relative roots are component-safe; an absolute root is permitted only in the
# Git-Bash Windows-drive form (/c/... or /d/...).  This is deliberately checked
# before mkdir, pending-record bookkeeping, or any PowerShell/HDC invocation.
SAFE_LOGROOT_RE='^([A-Za-z0-9][A-Za-z0-9._-]*)(/[A-Za-z0-9][A-Za-z0-9._-]*)*$|^/[A-Za-z]/[A-Za-z0-9][A-Za-z0-9._-]*(/[A-Za-z0-9][A-Za-z0-9._-]*)*$'
if [[ ! "$LOGROOT" =~ $SAFE_LOGROOT_RE ]]; then
  echo "ERROR: MDDS_GW_LOGROOT must be a safe relative path or /c/... path (no whitespace, controls, colon, or . / .. components)" >&2
  exit 2
fi
RUN_ID="${MDDS_RUN_ID:-gw_$(date +%Y%m%d_%H%M%S)_$RANDOM}"
case "$RUN_ID" in
  ''|*[!A-Za-z0-9_-]*)
    echo "ERROR: MDDS_RUN_ID must contain only A-Z, a-z, 0-9, _ or -" >&2
    exit 2
    ;;
esac
# A run ID is intentionally human-selectable for evidence collection, so it is
# not sufficient as ownership proof.  The per-invocation nonce fences a stale
# or concurrent invocation that reuses an otherwise valid run ID.
RUN_NONCE="${MDDS_RUN_NONCE:-n${RANDOM}p${RANDOM}x$$}"
case "$RUN_NONCE" in
  ''|*[!A-Za-z0-9_-]*)
    echo "ERROR: MDDS_RUN_NONCE must contain only A-Z, a-z, 0-9, _ or -" >&2
    exit 2
    ;;
esac
export MDDS_RUN_ID="$RUN_ID"
export MDDS_RUN_NONCE="$RUN_NONCE"
LOGDIR="$LOGROOT/$RUN_ID"
REMOTE_LOGDIR="$DEVICE_DIR/mdds_gw_runs/$RUN_ID"
REMOTE_OWNER="$REMOTE_LOGDIR/.mdds_run_owner"
PC_OWNER_FILE="$LOGDIR/.mdds_pc_run_owner"
PC_WS=/c/pixi_ws
PC_BAT_DIR="$(cygpath -w "$PWD/scripts/mdds_e2e/pc")"
mkdir -p "$LOGDIR"

# Every run has an isolated topic namespace.  The IDs are constrained above to
# characters valid in ROS names and in the tiny config renderer below.  Fixed
# /chatter-style names would let an unrelated, lingering participant satisfy a
# listener-count assertion even when this run's gateway path was broken.
GW_TOPIC_PREFIX="/mdds_gw_${RUN_ID}_${RUN_NONCE}"
GW_TOPIC_CHATTER="$GW_TOPIC_PREFIX/chatter"
GW_TOPIC_CHATTER_BACK="$GW_TOPIC_PREFIX/chatter_back"
GW_TOPIC_SWEEP="$GW_TOPIC_PREFIX/sweep"
GW_TOPIC_LAT_REQ="$GW_TOPIC_PREFIX/lat_req"
GW_TOPIC_LAT_RSP="$GW_TOPIC_PREFIX/lat_rsp"
GW_CONFIG_TEMPLATE="scripts/mdds_e2e/mdds_gateway_test.conf"
GW_CONFIG_LOCAL="$LOGDIR/mdds_gateway_test.rendered.conf"
# GW e2e has a dedicated CycloneDDS domain.  Keep it distinct from production
# domain 0 and the DS-03 gateway proof (Cyclone 46, MDDS/DSoftBus 43).  The
# raw MDDS leg remains 44 so it cannot share a DSoftBus Socket session with
# either gate.
GW_CYCLONE_DOMAIN=47
GW_MDDS_DOMAIN=44

# Test-only launcher fault injection: simulate an HDC stdout/token loss after
# a successful remote launch.  launch() must recover from the persistent
# remote record.  Keep this constrained to 0/1 before interpolating it into a
# remote shell command.
SUPPRESS_LAUNCH_TOKEN="${MDDS_TEST_SUPPRESS_LAUNCH_TOKEN:-0}"
case "$SUPPRESS_LAUNCH_TOKEN" in
  0|1) ;;
  *) echo "ERROR: MDDS_TEST_SUPPRESS_LAUNCH_TOKEN must be 0 or 1" >&2; exit 2 ;;
esac
export MDDS_TEST_SUPPRESS_LAUNCH_TOKEN="$SUPPRESS_LAUNCH_TOKEN"

# These fault-injection switches exercise the record-first recovery path.  They
# are deliberately narrow: they affect only this launcher, and default to off.
DROP_FIRST_RECORD_READ="${MDDS_TEST_DROP_FIRST_RECORD_READ:-0}"
FAIL_RECORD_WRITE="${MDDS_TEST_FAIL_RECORD_WRITE:-0}"
# This test-only hook deliberately makes the post-record identity check fail.
# It must therefore exercise pc_cleanup_pending_record's valid-live-record
# branch, rather than merely the pre-record cancellation path.
FORCE_PC_POST_RECORD_MISMATCH="${MDDS_TEST_FORCE_PC_POST_RECORD_MISMATCH:-0}"
for injected in "$DROP_FIRST_RECORD_READ" "$FAIL_RECORD_WRITE" "$FORCE_PC_POST_RECORD_MISMATCH"; do
  case "$injected" in
    0|1) ;;
    *) echo "ERROR: record fault-injection values must be 0 or 1" >&2; exit 2 ;;
  esac
done
export MDDS_TEST_DROP_FIRST_RECORD_READ="$DROP_FIRST_RECORD_READ"
export MDDS_TEST_FAIL_RECORD_WRITE="$FAIL_RECORD_WRITE"
export MDDS_TEST_FORCE_PC_POST_RECORD_MISMATCH="$FORCE_PC_POST_RECORD_MISMATCH"

export MSYS2_ARG_CONV_EXCL='*'

# The gateway test uses an MDDS DSoftBus domain that is separate from its
# CycloneDDS/PC domain. Both board-side RMW nodes source the installed
# production profile, which selects DSoftBus only and rejects any legacy
# MDDS_TRANSPORT override. MDDS domain 44 and Cyclone domain 47 are
# deliberately isolated from the DSoftBus-only DS gate (43/46) and from the
# default application domain (0).
#
# The gateway must NOT have RMW_IMPLEMENTATION pinned by its environment: it
# pins rmw_cyclonedds_cpp internally and fails closed on a foreign RMW. Its
# raw MDDS participant is selected solely by mdds_gateway_test.conf.
RENVS=". $DEVICE_DIR/env.sh; . $DEVICE_DIR/share/rmw_mdds/config/ohos_dsoftbus.env; export MDDS_DEBUG=1; export ROS_DOMAIN_ID=$GW_MDDS_DOMAIN;"
# main.cpp obtains its Cyclone domain exclusively from the rendered config via
# InitOptions::set_domain_id().  Clear inherited ROS_DOMAIN_ID so an unrelated
# board process cannot make the intended boundary ambiguous in diagnostics.
GWENVS=". $DEVICE_DIR/env.sh; unset RMW_IMPLEMENTATION MDDS_DEPLOYMENT_PROFILE MDDS_TRANSPORT MDDS_UDP_PEER_ALLOW ROS_DOMAIN_ID; export CYCLONEDDS_URI=$DEVICE_DIR/mdds_e2e/cyclonedds_board_a.xml; export MDDS_DEBUG=1; export RCUTILS_LOGGING_BUFFERED_STREAM=0;"

# HDC shell's exit status is not a trustworthy board-process status. Every
# launcher writes a run-scoped PID:start record before emitting an optional
# stdout token, which lets the host recover after a token/pipe loss.
shell()  { "$HDC" -t "$1" shell "$2" </dev/null; }

ACTIVITY_LOCK_DIR="$DEVICE_DIR/.mdds-activity-lock"
declare -a ACTIVITY_LOCKED_BOARDS=()

activity_lock_owner() {
  printf 'MDDS_ACTIVITY_LOCK MODE=TEST RUN_ID=%s NONCE=%s OWNER=run_mdds_gw\n' "$RUN_ID" "$RUN_NONCE"
}

acquire_activity_lock() { # <board>; atomic fail-closed lock before any payload
  local board="$1" owner out
  owner="$(activity_lock_owner)"
  out="$(shell "$board" "if (umask 077; mkdir '$ACTIVITY_LOCK_DIR') 2>/dev/null; then if (umask 077; set -C; printf '%s\\n' '$owner' > '$ACTIVITY_LOCK_DIR/owner') 2>/dev/null && test -d '$ACTIVITY_LOCK_DIR' && test ! -L '$ACTIVITY_LOCK_DIR' && test -f '$ACTIVITY_LOCK_DIR/owner' && test ! -L '$ACTIVITY_LOCK_DIR/owner' && test \"\$(cat '$ACTIVITY_LOCK_DIR/owner' 2>/dev/null)\" = '$owner'; then printf MDDS_ACTIVITY_LOCK_ACQUIRED; else printf MDDS_ACTIVITY_LOCK_OWNER_WRITE_FAILED; fi; else printf MDDS_ACTIVITY_LOCK_BUSY; fi" | tr -d '\r\n')"
  printf 'board=%s owner=%s result=%s\n' "$board" "$owner" "${out:-NO_MARKER}" >> "$LOGDIR/activity_locks.txt"
  if [[ "$out" != "MDDS_ACTIVITY_LOCK_ACQUIRED" ]]; then
    echo "ERROR: MDDS activity lock is held, malformed, or could not be created on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi
  ACTIVITY_LOCKED_BOARDS+=("$board")
}

release_activity_lock() { # <board>; only delete this exact owner record
  local board="$1" owner out
  owner="$(activity_lock_owner)"
  out="$(shell "$board" "if test -d '$ACTIVITY_LOCK_DIR' && test ! -L '$ACTIVITY_LOCK_DIR' && test -f '$ACTIVITY_LOCK_DIR/owner' && test ! -L '$ACTIVITY_LOCK_DIR/owner' && test \"\$(cat '$ACTIVITY_LOCK_DIR/owner' 2>/dev/null)\" = '$owner'; then rm -f '$ACTIVITY_LOCK_DIR/owner' && rmdir '$ACTIVITY_LOCK_DIR' && printf MDDS_ACTIVITY_LOCK_RELEASED; else printf MDDS_ACTIVITY_LOCK_NOT_OWNED; fi" | tr -d '\r\n')"
  printf 'board=%s owner=%s release=%s\n' "$board" "$owner" "${out:-NO_MARKER}" >> "$LOGDIR/activity_locks.txt"
  if [[ "$out" != "MDDS_ACTIVITY_LOCK_RELEASED" ]]; then
    echo "ERROR: could not release this run's MDDS activity lock on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi
}

release_activity_locks() {
  local index rc=0
  for ((index=${#ACTIVITY_LOCKED_BOARDS[@]} - 1; index >= 0; --index)); do
    release_activity_lock "${ACTIVITY_LOCKED_BOARDS[$index]}" || rc=1
  done
  ACTIVITY_LOCKED_BOARDS=()
  return "$rc"
}

acquire_activity_locks() {
  local board
  # Fixed A -> B ordering prevents two dual-board gates from deadlocking.
  for board in "$BOARD_A" "$BOARD_B"; do
    if ! acquire_activity_lock "$board"; then
      release_activity_locks || true
      return 1
    fi
  done
}

TRACKED=""       # space-separated board:pid:proc-start:remote-record:launch-tag entries
# Added before each HDC launch transaction. If stdout and its first record read
# are both lost after a child starts, EXIT can still recover only this run's
# remote PID:start record rather than searching for a process by command line.
PENDING_LAUNCH_RECORDS="" # space-separated board:remote-record:launch-tag entries
PENDING_PC_RECORDS=""     # space-separated host-local Windows guard-record:launch-tag entries
LAST_PID=""
GW_PID=""
PC_TRACKED=""    # space-separated Windows guard record paths
REMOTE_OWNERS="" # boards whose immutable run-id+nonce owner record matched
LAUNCH_SEQUENCE=0
RECORD_READ_DROPPED=0

parse_pid_token() { # stdin -> one REMOTE_PID=pid:start token, if any
  tr -d '\r' | sed -n 's/.*REMOTE_PID=\([0-9][0-9]*:[0-9][0-9]*\).*/\1/p' | head -1
}

parse_pid_record() { # parse_pid_record <launch-tag>; stdin -> exact pid:start
  local tag="$1"
  tr -d '\r' | sed -n "s/^MDDS_LAUNCH_RECORD RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=$tag PID=\\([0-9][0-9]*\\) START=\\([0-9][0-9]*\\)$/\\1:\\2/p" | head -1
}

parse_launch_status() { # parse_launch_status <launch-tag>; stdout -> terminal state
  local tag="$1"
  tr -d '\r' | sed -n "s/^MDDS_LAUNCH_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=$tag STATE=\\(CANCELLED_PREEXEC\\|RECORD_WRITE_FAILED\\|INTENT_INVALID\\)$/\\1/p" | head -1
}

parse_pc_record() { # parse_pc_record <launch-tag>; stdin -> exact guard pid:start
  local tag="$1"
  # A guard is eligible for cleanup only after it has installed the
  # KILL_ON_JOB_CLOSE containment contract.  Older six-field records are
  # deliberately rejected rather than falling back to best-effort /T cleanup.
  tr -d '\r' | sed -n "s/^MDDS_PC_RECORD RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=$tag PID=\\([0-9][0-9]*\\) START=\\([0-9][0-9]*\\) JOB=KILL_ON_CLOSE$/\\1:\\2/p" | head -1
}

parse_pc_job_proof() { # parse_pc_job_proof <launch-tag>; stdin -> root-pid:root-start:child-pid:child-start
  local tag="$1"
  tr -d '\r' | sed -n "s/^MDDS_PC_JOB_PROOF RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=$tag ROOT=\\([0-9][0-9]*\\):\\([0-9][0-9]*\\) CHILD=\\([0-9][0-9]*\\):\\([0-9][0-9]*\\) FLAGS=KILL_ON_CLOSE$/\\1:\\2:\\3:\\4/p" | head -1
}

parse_pc_status() { # parse_pc_status <launch-tag>; stdout -> terminal state
  local tag="$1"
  tr -d '\r' | sed -n "s/^MDDS_PC_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE TAG=$tag STATE=\\(CANCELLED_PRECMD\\|RECORD_WRITE_FAILED\\|INTENT_INVALID\\|JOB_ASSIGNMENT_FAILED\\)$/\\1/p" | head -1
}

# Quote one argument for the non-interactive board /bin/sh command line.  The
# payload itself is passed as an argument to the board-local guard, not spliced
# into the outer HDC shell command.
remote_sh_quote() {
  printf "'"
  printf '%s' "$1" | sed "s/'/'\\\\''/g"
  printf "'"
}

add_pending_launch_record() { # <board> <remote-record-path> <launch-tag>
  PENDING_LAUNCH_RECORDS="$PENDING_LAUNCH_RECORDS $1:$2:$3"
}

remove_pending_launch_record() { # <exact board:remote-record-path>
  local wanted="$1" pair kept=""
  for pair in $PENDING_LAUNCH_RECORDS; do
    [ "$pair" = "$wanted" ] || kept="$kept $pair"
  done
  PENDING_LAUNCH_RECORDS="$kept"
}

add_pending_pc_record() { # <host-local record path> <launch-tag>
  PENDING_PC_RECORDS="$PENDING_PC_RECORDS $1:$2"
}

remove_pending_pc_record() { # <exact host-local record path>
  local wanted="$1" record kept=""
  for record in $PENDING_PC_RECORDS; do
    [ "$record" = "$wanted" ] || kept="$kept $record"
  done
  PENDING_PC_RECORDS="$kept"
}

ensure_remote_owner() { # <board>
  local board="$1" seen out owner_script
  for seen in $REMOTE_OWNERS; do
    [ "$seen" = "$board" ] && return 0
  done
  owner_script='
dir=$1
owner=$2
run_id=$3
nonce=$4
expected="MDDS_RUN_OWNER RUN_ID=$run_id NONCE=$nonce"
matches_line() {
  path=$1
  line=$2
  test -f "$path" && test ! -L "$path" || return 1
  # Target /bin/sh has no pipefail: a failing sha256sum can otherwise leave
  # both pipeline outputs empty while cut itself exits zero.  Require two
  # complete lowercase SHA-256 values before treating a record as exact.
  expected_sha=$(printf "%s\\n" "$line" | sha256sum 2>/dev/null | cut -d " " -f1)
  actual_sha=$(sha256sum "$path" 2>/dev/null | cut -d " " -f1)
  [ "${#expected_sha}" -eq 64 ] && [ "${#actual_sha}" -eq 64 ] || return 1
  case "$expected_sha$actual_sha" in *[!0-9a-f]*) return 1 ;; esac
  [ "$actual_sha" = "$expected_sha" ]
}
mkdir -p "$dir" || exit 70
if (set -C; umask 077; printf "%s\\n" "$expected" > "$owner") 2>/dev/null; then
  echo GW_OWNER_CREATED
elif matches_line "$owner" "$expected"; then
  echo GW_OWNER_MATCHED
else
  echo GW_OWNER_CONFLICT
  exit 71
fi
'
  out=$(shell "$board" "sh -c $(remote_sh_quote "$owner_script") sh $(remote_sh_quote "$REMOTE_LOGDIR") $(remote_sh_quote "$REMOTE_OWNER") $(remote_sh_quote "$RUN_ID") $(remote_sh_quote "$RUN_NONCE")" || true)
  case "$out" in
    *GW_OWNER_CREATED*|*GW_OWNER_MATCHED*)
      REMOTE_OWNERS="$REMOTE_OWNERS $board"
      printf 'board=%s remote_owner=%s run_id=%s nonce=%s result=%s\n' \
        "$board" "$REMOTE_OWNER" "$RUN_ID" "$RUN_NONCE" \
        "$(printf '%s' "$out" | tr -d '\r\n')" >> "$LOGDIR/owner_records.txt"
      return 0
      ;;
    *)
      echo "   ERROR: remote run ownership conflict or write failure on $board: $(printf '%s' "$out" | tr -d '\r\n')" >&2
      return 1
      ;;
  esac
}

read_remote_launch_record() { # <board> <remote-record-path>; stdout is record only
  shell "$1" "if test -f $(remote_sh_quote "$2"); then cat $(remote_sh_quote "$2"); fi" || true
}

read_remote_launch_status() { # <board> <remote-status-path>; stdout is status only
  shell "$1" "if test -f $(remote_sh_quote "$2"); then cat $(remote_sh_quote "$2"); fi" || true
}

drop_first_record_read_if_requested() { # <scope> <record>
  if [ "$DROP_FIRST_RECORD_READ" = 1 ] && [ "$RECORD_READ_DROPPED" = 0 ]; then
    RECORD_READ_DROPPED=1
    printf 'scope=%s record=%s injected=drop-first-read\n' "$1" "$2" >> "$LOGDIR/launch_record_read_faults.txt"
    return 0
  fi
  return 1
}

ensure_remote_launch_intent() { # <board> <intent-path> <launch-tag>
  local board="$1" intent_path="$2" tag="$3" out intent_script
  # Intent is immutable and exists before PENDING/guard dispatch.  It binds a
  # record/status path to this exact run, nonce, and launch transaction.
  intent_script='
intent_path=$1
run_id=$2
nonce=$3
tag=$4
intent_line="MDDS_LAUNCH_INTENT RUN_ID=$run_id NONCE=$nonce TAG=$tag"
matches_line() {
  path=$1
  line=$2
  test -f "$path" && test ! -L "$path" || return 1
  # Target /bin/sh has no pipefail: a failing sha256sum can otherwise leave
  # both pipeline outputs empty while cut itself exits zero.  Require two
  # complete lowercase SHA-256 values before treating a record as exact.
  expected_sha=$(printf "%s\\n" "$line" | sha256sum 2>/dev/null | cut -d " " -f1)
  actual_sha=$(sha256sum "$path" 2>/dev/null | cut -d " " -f1)
  [ "${#expected_sha}" -eq 64 ] && [ "${#actual_sha}" -eq 64 ] || return 1
  case "$expected_sha$actual_sha" in *[!0-9a-f]*) return 1 ;; esac
  [ "$actual_sha" = "$expected_sha" ]
}
mkdir -p "$(dirname "$intent_path")" || { echo INTENT_WRITE_FAILED; exit 70; }
if (set -C; umask 077; printf "%s\\n" "$intent_line" > "$intent_path") 2>/dev/null; then
  echo INTENT_CREATED
elif matches_line "$intent_path" "$intent_line"; then
  echo INTENT_MATCHED
else
  echo INTENT_CONFLICT
  exit 71
fi
'
  out=$(shell "$board" "sh -c $(remote_sh_quote "$intent_script") sh $(remote_sh_quote "$intent_path") $(remote_sh_quote "$RUN_ID") $(remote_sh_quote "$RUN_NONCE") $(remote_sh_quote "$tag")" || true)
  case "$out" in
    *INTENT_CREATED*|*INTENT_MATCHED*)
      printf 'board=%s remote_intent=%s run_id=%s nonce=%s tag=%s result=%s\n' \
        "$board" "$intent_path" "$RUN_ID" "$RUN_NONCE" "$tag" \
        "$(printf '%s' "$out" | tr -d '\r\n')" >> "$LOGDIR/launch_intents.txt"
      return 0
      ;;
    *)
      echo "   ERROR: immutable remote launch intent was not established: $board:$intent_path" >&2
      return 1
      ;;
  esac
}

launch() { # launch <board> <env-prefix> <cmd> <log>; remote pid -> LAST_PID
  LAST_PID=""
  local board="$1" env_prefix="$2" payload="$3" log="$4"
  local out raw record token pid start record_path intent_path status_path cancel_path record_source pending status
  local guard_script q_guard q_intent q_record q_status q_cancel q_log q_env q_payload q_run q_nonce q_tag q_fail q_suppress
  local attempt launch_tag
  ensure_remote_owner "$board" || return 1
  LAUNCH_SEQUENCE=$((LAUNCH_SEQUENCE + 1))
  launch_tag="${LAUNCH_SEQUENCE}_${RANDOM}_$$"
  record_path="$REMOTE_LOGDIR/launch/${log}.${launch_tag}.pid"
  intent_path="${record_path}.intent"
  status_path="${record_path}.status"
  cancel_path="${record_path}.cancel"
  # Intent is durable before PENDING/dispatch.  A late or replayed HDC command
  # cannot run a payload unless it first proves this exact launch transaction.
  ensure_remote_launch_intent "$board" "$intent_path" "$launch_tag" || return 1
  pending="$board:$record_path:$launch_tag"
  add_pending_launch_record "$board" "$record_path" "$launch_tag"
  guard_script='
intent_path=$1
record_path=$2
status_path=$3
cancel_path=$4
log_path=$5
run_id=$6
nonce=$7
tag=$8
shift 8
env_prefix=$1
payload=$2
fail_record_write=$3
suppress_token=$4
intent_line="MDDS_LAUNCH_INTENT RUN_ID=$run_id NONCE=$nonce TAG=$tag"
cancel_line="MDDS_LAUNCH_CANCEL RUN_ID=$run_id NONCE=$nonce"
matches_line() {
  path=$1
  line=$2
  test -f "$path" && test ! -L "$path" || return 1
  # Target /bin/sh has no pipefail: a failing sha256sum can otherwise leave
  # both pipeline outputs empty while cut itself exits zero.  Require two
  # complete lowercase SHA-256 values before treating a record as exact.
  expected_sha=$(printf "%s\\n" "$line" | sha256sum 2>/dev/null | cut -d " " -f1)
  actual_sha=$(sha256sum "$path" 2>/dev/null | cut -d " " -f1)
  [ "${#expected_sha}" -eq 64 ] && [ "${#actual_sha}" -eq 64 ] || return 1
  case "$expected_sha$actual_sha" in *[!0-9a-f]*) return 1 ;; esac
  [ "$actual_sha" = "$expected_sha" ]
}
write_terminal_status() {
  state=$1
  status_line="MDDS_LAUNCH_STATUS RUN_ID=$run_id NONCE=$nonce TAG=$tag STATE=$state"
  if (set -C; umask 077; printf "%s\\n" "$status_line" > "$status_path") 2>/dev/null; then return 0; fi
  matches_line "$status_path" "$status_line"
}
cancel_requested() {
  if test ! -e "$cancel_path"; then return 1; fi
  matches_line "$cancel_path" "$cancel_line" && return 0
  return 2
}
if ! matches_line "$intent_path" "$intent_line"; then
  write_terminal_status INTENT_INVALID || true
  exit 70
fi
cancel_requested; cancel_rc=$?
if [ "$cancel_rc" = 0 ]; then write_terminal_status CANCELLED_PREEXEC || true; exit 0; fi
if [ "$cancel_rc" != 1 ]; then write_terminal_status INTENT_INVALID || true; exit 72; fi
printf "GW_RUN_ID=%s\\nGW_RUN_NONCE=%s\\n" "$run_id" "$nonce" > "$log_path" || exit 70
start=$(cut -d " " -f22 /proc/$$/stat 2>/dev/null)
case "$start" in ""|*[!0-9]*) echo REMOTE_LAUNCH_INVALID_START >&2; exit 70 ;; esac
record_line="MDDS_LAUNCH_RECORD RUN_ID=$run_id NONCE=$nonce TAG=$tag PID=$$ START=$start"
if [ "$fail_record_write" = 1 ]; then
  write_terminal_status RECORD_WRITE_FAILED || true
  exit 70
fi
# The record is create-only.  Replaying this HDC command must never replace a
# signed first guard record and thereby start a second untracked payload.
if test -e "$record_path"; then echo GW_LAUNCH_RECORD_ALREADY_EXISTS >&2; exit 70; fi
if ! (set -C; umask 077; printf "%s\\n" "$record_line" > "$record_path") 2>/dev/null; then
  if test -e "$record_path"; then echo GW_LAUNCH_RECORD_ALREADY_EXISTS >&2; else write_terminal_status RECORD_WRITE_FAILED || true; fi
  exit 70
fi
if ! matches_line "$record_path" "$record_line"; then
  write_terminal_status RECORD_WRITE_FAILED || true
  exit 70
fi
cancel_requested; cancel_rc=$?
if [ "$cancel_rc" = 0 ]; then write_terminal_status CANCELLED_PREEXEC || true; exit 0; fi
if [ "$cancel_rc" != 1 ]; then write_terminal_status INTENT_INVALID || true; exit 72; fi
if [ "$suppress_token" != 1 ]; then printf "REMOTE_PID=%s:%s\\n" "$$" "$start"; fi
# exec preserves the recorded PID through the final command: cleanup never
# kills a shell while leaving an unrecorded payload child behind.
exec sh -c "$env_prefix exec $payload" >> "$log_path" 2>&1 < /dev/null
'
  q_guard=$(remote_sh_quote "$guard_script")
  q_intent=$(remote_sh_quote "$intent_path")
  q_record=$(remote_sh_quote "$record_path")
  q_status=$(remote_sh_quote "$status_path")
  q_cancel=$(remote_sh_quote "$cancel_path")
  q_log=$(remote_sh_quote "$REMOTE_LOGDIR/$log")
  q_env=$(remote_sh_quote "$env_prefix")
  q_payload=$(remote_sh_quote "$payload")
  q_run=$(remote_sh_quote "$RUN_ID")
  q_nonce=$(remote_sh_quote "$RUN_NONCE")
  q_tag=$(remote_sh_quote "$launch_tag")
  q_fail=$(remote_sh_quote "$FAIL_RECORD_WRITE")
  q_suppress=$(remote_sh_quote "$SUPPRESS_LAUNCH_TOKEN")
  out=$(shell "$board" "mkdir -p $(remote_sh_quote "$REMOTE_LOGDIR/launch"); nohup sh -c $q_guard sh $q_intent $q_record $q_status $q_cancel $q_log $q_run $q_nonce $q_tag $q_env $q_payload $q_fail $q_suppress </dev/null &" || true)
  token=$(printf '%s' "$out" | parse_pid_token)
  record=""
  status=""
  for attempt in $(seq 1 10); do
    if drop_first_record_read_if_requested remote "$record_path"; then raw=""; else raw=$(read_remote_launch_record "$board" "$record_path"); fi
    record=$(printf '%s' "$raw" | parse_pid_record "$launch_tag")
    [[ "$record" =~ ^[0-9]+:[0-9]+$ ]] && break
    raw=$(read_remote_launch_status "$board" "$status_path")
    status=$(printf '%s' "$raw" | parse_launch_status "$launch_tag")
    [ -n "$status" ] && break
    sleep 0.2
  done
  pid=${record%%:*}
  start=${record#*:}
  if ! [[ "$pid" =~ ^[0-9]+$ && "$start" =~ ^[0-9]+$ ]]; then
    if [ -n "$status" ]; then
      echo "   ERROR: remote launch acknowledged terminal pre-exec state=$status for [$payload]" >&2
    else
      echo "   ERROR: no exact run-owned persistent launch record/status for [$payload]; hdc output: $(printf '%s' "$out" | tr -d '\r' | head -3)" >&2
    fi
    # Keep PENDING.  Cleanup accepts a missing record only after it sees an
    # identity-fenced guard terminal status; silence remains unresolved.
    return 1
  fi
  if [[ "$token" =~ ^[0-9]+:[0-9]+$ ]] && [ "$token" != "$record" ]; then
    echo "   ERROR: launch token disagrees with exact persistent record for [$payload]" >&2
    return 1
  fi
  record_source=remote-record
  [[ "$token" = "$record" ]] && record_source=stdout+remote-record
  # Tracking comes before PENDING removal.  An EXIT in this handoff can at
  # worst run duplicate, identity-fenced cleanup; it cannot orphan a process.
  TRACKED="$TRACKED $board:$pid:$start:$record_path:$launch_tag"
  remove_pending_launch_record "$pending"
  printf 'board=%s pid=%s start=%s source=%s remote_intent=%s remote_record=%s remote_status=%s run_id=%s nonce=%s tag=%s log=%s\n' \
    "$board" "$pid" "$start" "$record_source" "$intent_path" "$record_path" "$status_path" "$RUN_ID" "$RUN_NONCE" "$launch_tag" "$log" >> "$LOGDIR/launch_records.txt"
  if [ "$record_source" = remote-record ]; then
    echo "   recovered launch identity from persistent remote record: $board:$pid:$start" >&2
  fi
  LAST_PID=$pid
}

cleanup_pending_launch_record() { # <board> <remote-record-path> <launch-tag>
  local board="$1" record_path="$2" tag="$3" intent_path="${2}.intent" status_path="${2}.status" cancel_path="${2}.cancel" out cleanup_script
  # A missing record is deliberately *not* success.  It becomes safe only when
  # the immutable intent and an exact guard-written terminal status prove that
  # this launch stopped before exec.
  cleanup_script='
intent_path=$1
record_path=$2
status_path=$3
cancel_path=$4
run_id=$5
nonce=$6
tag=$7
intent_line="MDDS_LAUNCH_INTENT RUN_ID=$run_id NONCE=$nonce TAG=$tag"
cancel_line="MDDS_LAUNCH_CANCEL RUN_ID=$run_id NONCE=$nonce"
matches_line() {
  path=$1
  line=$2
  test -f "$path" && test ! -L "$path" || return 1
  # Target /bin/sh has no pipefail: a failing sha256sum can otherwise leave
  # both pipeline outputs empty while cut itself exits zero.  Require two
  # complete lowercase SHA-256 values before treating a record as exact.
  expected_sha=$(printf "%s\\n" "$line" | sha256sum 2>/dev/null | cut -d " " -f1)
  actual_sha=$(sha256sum "$path" 2>/dev/null | cut -d " " -f1)
  [ "${#expected_sha}" -eq 64 ] && [ "${#actual_sha}" -eq 64 ] || return 1
  case "$expected_sha$actual_sha" in *[!0-9a-f]*) return 1 ;; esac
  [ "$actual_sha" = "$expected_sha" ]
}
terminal_status() {
  for state in CANCELLED_PREEXEC RECORD_WRITE_FAILED INTENT_INVALID; do
    status_line="MDDS_LAUNCH_STATUS RUN_ID=$run_id NONCE=$nonce TAG=$tag STATE=$state"
    if matches_line "$status_path" "$status_line"; then
      printf "%s\\n" "$state"
      return 0
    fi
  done
  return 1
}
if ! matches_line "$intent_path" "$intent_line"; then
  echo PENDING_INTENT_INVALID
  exit 70
fi
mkdir -p "$(dirname "$record_path")" || { echo PENDING_CANCEL_WRITE_FAILED; exit 70; }
if test -e "$cancel_path"; then
  if ! matches_line "$cancel_path" "$cancel_line"; then echo PENDING_CANCEL_CONFLICT; exit 70; fi
elif ! (set -C; umask 077; printf "%s\\n" "$cancel_line" > "$cancel_path") 2>/dev/null; then
  if ! matches_line "$cancel_path" "$cancel_line"; then echo PENDING_CANCEL_WRITE_FAILED; exit 70; fi
fi
if ! matches_line "$cancel_path" "$cancel_line"; then echo PENDING_CANCEL_VERIFY_FAILED; exit 70; fi
saw_record=0
invalid=0
for i in 1 2 3 4 5; do
  if test ! -f "$record_path"; then
    status=$(terminal_status || true)
    if [ -n "$status" ]; then echo "PENDING_RECORD_STATUS_$status"; exit 0; fi
    sleep 1
    continue
  fi
  saw_record=1
  record=$(sed -n '1p' "$record_path" 2>/dev/null || true)
  set -f
  old_ifs=$IFS
  IFS=" "
  set -- $record
  IFS=$old_ifs
  if [ "$#" -ne 6 ] || [ "$1" != MDDS_LAUNCH_RECORD ] || [ "$2" != "RUN_ID=$run_id" ] || [ "$3" != "NONCE=$nonce" ] || [ "$4" != "TAG=$tag" ]; then invalid=1; sleep 1; continue; fi
  pid=${5#PID=}
  start=${6#START=}
  if [ "$5" != "PID=$pid" ] || [ "$6" != "START=$start" ]; then invalid=1; sleep 1; continue; fi
  case "$pid" in ""|*[!0-9]*) invalid=1; sleep 1; continue ;; esac
  case "$start" in ""|*[!0-9]*) invalid=1; sleep 1; continue ;; esac
  record_line="MDDS_LAUNCH_RECORD RUN_ID=$run_id NONCE=$nonce TAG=$tag PID=$pid START=$start"
  if ! matches_line "$record_path" "$record_line"; then invalid=1; sleep 1; continue; fi
  if test ! -r /proc/$pid/stat; then echo PENDING_RECORD_GONE; exit 0; fi
  current=$(cut -d " " -f22 /proc/$pid/stat 2>/dev/null)
  state=$(cut -d " " -f3 /proc/$pid/stat 2>/dev/null)
  if [ "$current" != "$start" ]; then echo PENDING_RECORD_REUSED; exit 73; fi
  if [ "$state" = Z ]; then echo PENDING_RECORD_GONE; exit 0; fi
  kill "$pid" 2>/dev/null || { echo PENDING_RECORD_SIGNAL_FAILED; exit 74; }
  sleep 2
  if test ! -r /proc/$pid/stat || [ "$(cut -d " " -f3 /proc/$pid/stat 2>/dev/null)" = Z ]; then echo PENDING_RECORD_STOPPED; exit 0; fi
  kill -9 "$pid" 2>/dev/null || { echo PENDING_RECORD_SIGNAL_FAILED; exit 74; }
  sleep 1
  if test ! -r /proc/$pid/stat || [ "$(cut -d " " -f3 /proc/$pid/stat 2>/dev/null)" = Z ]; then echo PENDING_RECORD_STOPPED; exit 0; fi
  echo PENDING_RECORD_LIVE
  exit 75
done
if [ "$saw_record" = 0 ]; then echo PENDING_RECORD_UNRESOLVED; exit 76; fi
if [ "$invalid" = 1 ]; then echo PENDING_RECORD_INVALID; else echo PENDING_RECORD_UNRESOLVED; fi
exit 76
'
  out=$(shell "$board" "sh -c $(remote_sh_quote "$cleanup_script") sh $(remote_sh_quote "$intent_path") $(remote_sh_quote "$record_path") $(remote_sh_quote "$status_path") $(remote_sh_quote "$cancel_path") $(remote_sh_quote "$RUN_ID") $(remote_sh_quote "$RUN_NONCE") $(remote_sh_quote "$tag")" || true)
  printf 'board=%s remote_intent=%s remote_record=%s remote_status=%s cancel=%s tag=%s result=%s\n' "$board" "$intent_path" "$record_path" "$status_path" "$cancel_path" "$tag" \
    "$(printf '%s' "$out" | tr -d '\r\n')" >> "$LOGDIR/pending_cleanup_records.txt"
  case "$out" in
    *PENDING_RECORD_STATUS_CANCELLED_PREEXEC*|*PENDING_RECORD_STATUS_RECORD_WRITE_FAILED*|*PENDING_RECORD_STATUS_INTENT_INVALID*|*PENDING_RECORD_GONE*|*PENDING_RECORD_STOPPED*)
      return 0 ;;
    *)
      echo "   ERROR: pending launch record could not be safely recovered: $board:$record_path" >&2
      return 1 ;;
  esac
}

cleanup_pending_launch_records() {
  local snapshot="$PENDING_LAUNCH_RECORDS" pair board record_path tag rest kept="" rc=0
  for pair in $snapshot; do
    board=${pair%%:*}
    rest=${pair#*:}
    record_path=${rest%:*}
    tag=${rest##*:}
    if ! cleanup_pending_launch_record "$board" "$record_path" "$tag"; then
      kept="$kept $pair"
      rc=1
    fi
  done
  PENDING_LAUNCH_RECORDS="$kept"
  return "$rc"
}

rbg() { # rbg <board> <cmd> <log>
  launch "$1" "$RENVS" "$2" "$3"
}

remote_record_state() { # remote_record_state <board> <pid> <proc-start>
  local board="$1" pid="$2" start="$3" result
  result=$(shell "$board" "if test ! -r /proc/$pid/stat; then echo GONE; else current=\$(cut -d ' ' -f22 /proc/$pid/stat); state=\$(cut -d ' ' -f3 /proc/$pid/stat); if [ \"\$current\" != '$start' ]; then echo REUSED; elif [ \"\$state\" = Z ]; then echo GONE; else echo LIVE; fi; fi" | tr -d '\r')
  case "$result" in
    *GONE*) echo GONE ;;
    *LIVE*) echo LIVE ;;
    *) echo REUSED ;;
  esac
}

tracked_record_matches() { # <board> <remote-record-path> <launch-tag> <pid> <proc-start>
  local raw got
  raw=$(shell "$1" "if test -f $(remote_sh_quote "$2"); then cat $(remote_sh_quote "$2"); fi" || true)
  got=$(printf '%s' "$raw" | parse_pid_record "$3")
  [ "$got" = "$4:$5" ]
}

remove_tracked_record() { # <exact board:pid:start:remote-record:launch-tag>
  local wanted="$1" pair kept=""
  for pair in $TRACKED; do
    [ "$pair" = "$wanted" ] || kept="$kept $pair"
  done
  TRACKED="$kept"
}

kill_recorded() { # kill_recorded <board> <pid>
  local board="$1" pid="$2" pair="" b candidate_pid start record_path tag state grace=1
  [[ "$pid" =~ ^[0-9]+$ ]] || { echo "   ERROR: refusing non-numeric pid: $pid" >&2; return 1; }
  for pair in $TRACKED; do
    b=$(printf '%s' "$pair" | cut -d: -f1)
    candidate_pid=$(printf '%s' "$pair" | cut -d: -f2)
    if [ "$b" = "$board" ] && [ "$candidate_pid" = "$pid" ]; then break; fi
    pair=""
  done
  if [ -z "$pair" ]; then
    echo "   ERROR: refusing untracked process $board:$pid" >&2
    return 1
  fi
  start=$(printf '%s' "$pair" | cut -d: -f3)
  record_path=$(printf '%s' "$pair" | cut -d: -f4)
  tag=$(printf '%s' "$pair" | cut -d: -f5)
  if ! tracked_record_matches "$board" "$record_path" "$tag" "$pid" "$start"; then
    echo "   ERROR: retained cleanup record $pair no longer matches this run-id/nonce; refusing to kill" >&2
    return 1
  fi
  state=$(remote_record_state "$board" "$pid" "$start")
  case "$state" in
    GONE) remove_tracked_record "$pair"; return 0 ;;
    REUSED)
      echo "   ERROR: retained cleanup record $pair has a reused PID; refusing to kill" >&2
      return 1 ;;
  esac
  # The gateway performs a coordinated participant/thread shutdown; leave it
  # enough time to complete before escalating to SIGKILL.
  [ "$board" = "$BOARD_A" ] && [ "$pid" = "$GW_PID" ] && grace=5
  shell "$board" "kill $pid 2>/dev/null" >/dev/null || {
    echo "   ERROR: failed to signal owned process $pair" >&2
    return 1
  }
  sleep "$grace"
  state=$(remote_record_state "$board" "$pid" "$start")
  if [ "$state" = LIVE ]; then
    echo "   WARN: $pair survived SIGTERM; sending SIGKILL" >&2
    shell "$board" "kill -9 $pid 2>/dev/null" >/dev/null || {
      echo "   ERROR: failed to SIGKILL owned process $pair" >&2
      return 1
    }
    sleep 1
    state=$(remote_record_state "$board" "$pid" "$start")
  fi
  if [ "$state" != GONE ]; then
    echo "   ERROR: owned process $pair did not exit (state=$state); retaining record" >&2
    return 1
  fi
  remove_tracked_record "$pair"
}

stopall() {
  local snapshot="$TRACKED" pair b pid rc=0
  for pair in $snapshot; do
    b=$(printf '%s' "$pair" | cut -d: -f1)
    pid=$(printf '%s' "$pair" | cut -d: -f2)
    kill_recorded "$b" "$pid" || rc=1
  done
  [ -z "$TRACKED" ] && GW_PID=""
  return "$rc"
}

kill_gw() {
  [ -n "$GW_PID" ] || return 0
  kill_recorded "$BOARD_A" "$GW_PID" || return 1
  GW_PID=""
}

ensure_pc_owner() {
  local out
  export MDDS_PC_OWNER_FILE="$(cygpath -w "$PC_OWNER_FILE")"
  export MDDS_PC_OWNER_EXPECTED="MDDS_PC_RUN_OWNER RUN_ID=$RUN_ID NONCE=$RUN_NONCE"
  out=$(powershell -NoProfile -NonInteractive -Command '
    $expected = $env:MDDS_PC_OWNER_EXPECTED
    try {
      $fs = [System.IO.File]::Open($env:MDDS_PC_OWNER_FILE, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
      try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("$expected$([Environment]::NewLine)")
        $fs.Write($bytes, 0, $bytes.Length)
        $fs.Flush()
      } finally { $fs.Dispose() }
      Write-Output "GW_PC_OWNER_CREATED"
    } catch [System.IO.IOException] {
      if ((Test-Path -LiteralPath $env:MDDS_PC_OWNER_FILE) -and
          (([System.IO.File]::ReadAllText($env:MDDS_PC_OWNER_FILE).Trim()) -eq $expected)) {
        Write-Output "GW_PC_OWNER_MATCHED"
      } else {
        Write-Output "GW_PC_OWNER_CONFLICT"
        exit 71
      }
    } catch {
      Write-Output "GW_PC_OWNER_WRITE_FAILED"
      Write-Error $_
      exit 70
    }
  ' 2>&1 || true)
  unset MDDS_PC_OWNER_FILE MDDS_PC_OWNER_EXPECTED
  case "$out" in
    *GW_PC_OWNER_CREATED*|*GW_PC_OWNER_MATCHED*)
      printf 'scope=pc owner=%s run_id=%s nonce=%s result=%s\n' \
        "$PC_OWNER_FILE" "$RUN_ID" "$RUN_NONCE" "$(printf '%s' "$out" | tr -d '\r\n')" >> "$LOGDIR/owner_records.txt"
      return 0
      ;;
    *)
      echo "   ERROR: PC run ownership conflict or write failure: $(printf '%s' "$out" | tr -d '\r\n')" >&2
      return 1
      ;;
  esac
}

read_pc_launch_record() { # <host-local record path>; stdout is record only
  export MDDS_PC_RECORD="$(cygpath -w "$1")"
  powershell -NoProfile -NonInteractive -Command '
    if (Test-Path -LiteralPath $env:MDDS_PC_RECORD) {
      [Console]::Out.Write([System.IO.File]::ReadAllText($env:MDDS_PC_RECORD))
    }
  ' 2>/dev/null || true
  unset MDDS_PC_RECORD
}

read_pc_job_proof() { # <host-local Job Object proof path>; stdout is proof only
  export MDDS_PC_JOB_PROOF="$(cygpath -w "$1")"
  powershell -NoProfile -NonInteractive -Command '
    if (Test-Path -LiteralPath $env:MDDS_PC_JOB_PROOF) {
      [Console]::Out.Write([System.IO.File]::ReadAllText($env:MDDS_PC_JOB_PROOF))
    }
  ' 2>/dev/null || true
  unset MDDS_PC_JOB_PROOF
}

read_pc_launch_status() { # <host-local status path>; stdout is status only
  export MDDS_PC_STATUS="$(cygpath -w "$1")"
  powershell -NoProfile -NonInteractive -Command '
    if (Test-Path -LiteralPath $env:MDDS_PC_STATUS) {
      [Console]::Out.Write([System.IO.File]::ReadAllText($env:MDDS_PC_STATUS))
    }
  ' 2>/dev/null || true
  unset MDDS_PC_STATUS
}

ensure_pc_launch_intent() { # <host-local record path> <launch-tag>
  local record_file="$1" tag="$2" intent_file="${1}.intent" out
  export MDDS_PC_INTENT="$(cygpath -w "$intent_file")"
  export MDDS_PC_EXPECTED_RUN="$RUN_ID"
  export MDDS_PC_EXPECTED_NONCE="$RUN_NONCE"
  export MDDS_PC_LAUNCH_TAG="$tag"
  out=$(powershell -NoProfile -NonInteractive -Command '
    $expected = "MDDS_PC_INTENT RUN_ID=$env:MDDS_PC_EXPECTED_RUN NONCE=$env:MDDS_PC_EXPECTED_NONCE TAG=$env:MDDS_PC_LAUNCH_TAG"
    try {
      $fs = [System.IO.File]::Open($env:MDDS_PC_INTENT, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
      try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("$expected$([Environment]::NewLine)")
        $fs.Write($bytes, 0, $bytes.Length)
        $fs.Flush()
      } finally { $fs.Dispose() }
      Write-Output "GW_PC_INTENT_CREATED"
    } catch [System.IO.IOException] {
      if ((Test-Path -LiteralPath $env:MDDS_PC_INTENT) -and
          (([System.IO.File]::ReadAllText($env:MDDS_PC_INTENT).Trim()) -eq $expected)) {
        Write-Output "GW_PC_INTENT_MATCHED"
      } else {
        Write-Output "GW_PC_INTENT_CONFLICT"
        exit 71
      }
    } catch {
      Write-Output "GW_PC_INTENT_WRITE_FAILED"
      Write-Error $_
      exit 70
    }
  ' 2>&1 || true)
  unset MDDS_PC_INTENT MDDS_PC_EXPECTED_RUN MDDS_PC_EXPECTED_NONCE MDDS_PC_LAUNCH_TAG
  case "$out" in
    *GW_PC_INTENT_CREATED*|*GW_PC_INTENT_MATCHED*)
      printf 'scope=pc local_intent=%s run_id=%s nonce=%s tag=%s result=%s\n' \
        "$intent_file" "$RUN_ID" "$RUN_NONCE" "$tag" "$(printf '%s' "$out" | tr -d '\r\n')" >> "$LOGDIR/launch_intents.txt"
      return 0
      ;;
    *)
      echo "   ERROR: immutable PC launch intent was not established: $intent_file" >&2
      return 1
      ;;
  esac
}

pc_stop_recorded() { # <host-local exact Windows guard record path> <launch-tag>
  local record_file="$1" tag="$2" out
  export MDDS_PC_RECORD="$(cygpath -w "$record_file")"
  export MDDS_PC_EXPECTED_RUN="$RUN_ID"
  export MDDS_PC_EXPECTED_NONCE="$RUN_NONCE"
  export MDDS_PC_LAUNCH_TAG="$tag"
  out=$(powershell -NoProfile -NonInteractive -Command '
    try {
      if (-not (Test-Path -LiteralPath $env:MDDS_PC_RECORD)) { Write-Output "PC_RECORD_MISSING"; exit 2 }
      $record = [System.IO.File]::ReadAllText($env:MDDS_PC_RECORD).Trim()
      $parts = $record -split " "
      if ($parts.Count -ne 7 -or $parts[0] -ne "MDDS_PC_RECORD" -or
          $parts[1] -ne "RUN_ID=$env:MDDS_PC_EXPECTED_RUN" -or
          $parts[2] -ne "NONCE=$env:MDDS_PC_EXPECTED_NONCE" -or
          $parts[3] -ne "TAG=$env:MDDS_PC_LAUNCH_TAG" -or
          -not $parts[4].StartsWith("PID=") -or -not $parts[5].StartsWith("START=") -or
          $parts[6] -ne "JOB=KILL_ON_CLOSE") {
        Write-Output "PC_RECORD_INVALID"
        exit 2
      }
      $recordPid = [int]$parts[4].Substring(4)
      $start = [int64]$parts[5].Substring(6)
      if ($parts[4] -ne "PID=$recordPid" -or $parts[5] -ne "START=$start") { Write-Output "PC_RECORD_INVALID"; exit 2 }
      # Keep one exact OS handle from creation-time comparison through
      # termination.  Reopening taskkill by numeric PID after a Get-Process
      # check would permit a PID-reuse race against an unrelated process.
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class MddsPcExactProcess {
  [StructLayout(LayoutKind.Sequential)] public struct FileTime {
    public uint LowDateTime;
    public uint HighDateTime;
  }
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern IntPtr OpenProcess(uint access, [MarshalAs(UnmanagedType.Bool)] bool inheritHandle, int processId);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetProcessTimes(IntPtr process, out FileTime creation, out FileTime exit, out FileTime kernel, out FileTime user);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool TerminateProcess(IntPtr process, uint exitCode);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool CloseHandle(IntPtr handle);
  public static bool TryGetCreationFileTime(IntPtr process, out long fileTime) {
    FileTime creation;
    FileTime exit;
    FileTime kernel;
    FileTime user;
    bool ok = GetProcessTimes(process, out creation, out exit, out kernel, out user);
    fileTime = (long)(((ulong)creation.HighDateTime << 32) | creation.LowDateTime);
    return ok;
  }
}
"@
      [uint32]$access = 0x00101001 # PROCESS_TERMINATE | QUERY_LIMITED | SYNCHRONIZE
      $processHandle = [MddsPcExactProcess]::OpenProcess($access, $false, $recordPid)
      if ($processHandle -eq [IntPtr]::Zero) {
        $openError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($openError -eq 87) { Write-Output "PC_RECORD_GONE_JOB_CLOSED"; exit 0 }
        Write-Output "PC_RECORD_OPEN_FAILED win32=$openError"
        exit 4
      }
      $resultMessage = "PC_RECORD_LIVE"
      $resultCode = 5
      try {
        [int64]$handleStart = 0
        if (-not [MddsPcExactProcess]::TryGetCreationFileTime($processHandle, [ref]$handleStart)) {
          $resultMessage = "PC_RECORD_GET_TIMES_FAILED win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
          $resultCode = 4
        } elseif ($handleStart -ne $start) {
          $resultMessage = "PC_RECORD_REUSED"
          $resultCode = 3
        } else {
          $terminateOk = [MddsPcExactProcess]::TerminateProcess($processHandle, 0)
          $terminateError = 0
          if (-not $terminateOk) { $terminateError = [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
          $waitResult = [MddsPcExactProcess]::WaitForSingleObject($processHandle, 3000)
          if ($waitResult -eq 0) {
            $resultMessage = "PC_RECORD_STOPPED_JOB_CLOSED terminate_ok=$terminateOk terminate_win32=$terminateError"
            $resultCode = 0
          } elseif (-not $terminateOk) {
            $resultMessage = "PC_RECORD_SIGNAL_FAILED win32=$terminateError"
            $resultCode = 4
          } elseif ($waitResult -eq 258) {
            $resultMessage = "PC_RECORD_LIVE"
            $resultCode = 5
          } else {
            $resultMessage = "PC_RECORD_WAIT_FAILED win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
            $resultCode = 4
          }
        }
      } finally {
        [MddsPcExactProcess]::CloseHandle($processHandle) | Out-Null
      }
      Write-Output $resultMessage
      exit $resultCode
    } catch {
      Write-Output "PC_RECORD_INVALID"
      Write-Error $_
      exit 2
    }
  ' 2>&1 || true)
  unset MDDS_PC_RECORD MDDS_PC_EXPECTED_RUN MDDS_PC_EXPECTED_NONCE MDDS_PC_LAUNCH_TAG
  printf 'local_record=%s tag=%s result=%s\n' "$record_file" "$tag" \
    "$(printf '%s' "$out" | tr -d '\r\n')" >> "$LOGDIR/pc_cleanup_records.txt"
  case "$out" in
    *PC_RECORD_GONE_JOB_CLOSED*|*PC_RECORD_STOPPED_JOB_CLOSED*) return 0 ;;
    *) return 1 ;;
  esac
}

pc_stopall() {
  local pair record_file tag kept="" rc=0
  for pair in $PC_TRACKED; do
    record_file=${pair%:*}
    tag=${pair##*:}
    if ! pc_stop_recorded "$record_file" "$tag"; then
      echo "   ERROR: retained PC cleanup record $record_file tag=$tag" >&2
      kept="$kept $pair"
      rc=1
    fi
  done
  PC_TRACKED="$kept"
  return "$rc"
}

pc_cleanup_pending_record() { # <host-local Windows guard record path> <launch-tag>
  local record_file="$1" tag="$2" intent_file="${1}.intent" status_file="${1}.status" cancel_file="${1}.cancel" out
  export MDDS_PC_RECORD="$(cygpath -w "$record_file")"
  export MDDS_PC_INTENT="$(cygpath -w "$intent_file")"
  export MDDS_PC_STATUS="$(cygpath -w "$status_file")"
  export MDDS_PC_CANCEL="$(cygpath -w "$cancel_file")"
  export MDDS_PC_EXPECTED_RUN="$RUN_ID"
  export MDDS_PC_EXPECTED_NONCE="$RUN_NONCE"
  export MDDS_PC_LAUNCH_TAG="$tag"
  out=$(powershell -NoProfile -NonInteractive -Command '
    $intentLine = "MDDS_PC_INTENT RUN_ID=$env:MDDS_PC_EXPECTED_RUN NONCE=$env:MDDS_PC_EXPECTED_NONCE TAG=$env:MDDS_PC_LAUNCH_TAG"
    $cancelLine = "MDDS_PC_CANCEL RUN_ID=$env:MDDS_PC_EXPECTED_RUN NONCE=$env:MDDS_PC_EXPECTED_NONCE"
    function Ensure-CreateOnly([string]$path, [string]$content) {
      try {
        $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        try {
          $bytes = [System.Text.Encoding]::UTF8.GetBytes("$content$([Environment]::NewLine)")
          $fs.Write($bytes, 0, $bytes.Length)
          $fs.Flush()
        } finally { $fs.Dispose() }
        return $true
      } catch [System.IO.IOException] {
        return ((Test-Path -LiteralPath $path) -and (([System.IO.File]::ReadAllText($path).Trim()) -eq $content))
      } catch { return $false }
    }
    function Get-TerminalStatus {
      if (-not (Test-Path -LiteralPath $env:MDDS_PC_STATUS)) { return $null }
      try { $parts = ([System.IO.File]::ReadAllText($env:MDDS_PC_STATUS).Trim()) -split " " } catch { return $null }
      if ($parts.Count -ne 5 -or $parts[0] -ne "MDDS_PC_STATUS" -or
          $parts[1] -ne "RUN_ID=$env:MDDS_PC_EXPECTED_RUN" -or
          $parts[2] -ne "NONCE=$env:MDDS_PC_EXPECTED_NONCE" -or
          $parts[3] -ne "TAG=$env:MDDS_PC_LAUNCH_TAG" -or -not $parts[4].StartsWith("STATE=")) { return $null }
      $state = $parts[4].Substring(6)
      if ($parts[4] -ne "STATE=$state") { return $null }
      if ($state -in @("CANCELLED_PRECMD", "RECORD_WRITE_FAILED", "INTENT_INVALID", "JOB_ASSIGNMENT_FAILED")) { return $state }
      return $null
    }
    if ((-not (Test-Path -LiteralPath $env:MDDS_PC_INTENT)) -or
        (([System.IO.File]::ReadAllText($env:MDDS_PC_INTENT).Trim()) -ne $intentLine)) {
      Write-Output "PENDING_PC_INTENT_INVALID"
      exit 70
    }
    if (-not (Ensure-CreateOnly $env:MDDS_PC_CANCEL $cancelLine)) {
      Write-Output "PENDING_PC_CANCEL_WRITE_FAILED"
      exit 70
    }
    if (([System.IO.File]::ReadAllText($env:MDDS_PC_CANCEL).Trim()) -ne $cancelLine) {
      Write-Output "PENDING_PC_CANCEL_VERIFY_FAILED"
      exit 70
    }
    $sawRecord = $false
    $invalid = $false
    for ($i = 0; $i -lt 10; ++$i) {
      if (-not (Test-Path -LiteralPath $env:MDDS_PC_RECORD)) {
        $terminal = Get-TerminalStatus
        if ($null -ne $terminal) { Write-Output "PENDING_PC_STATUS_$terminal"; exit 0 }
        Start-Sleep -Milliseconds 150
        continue
      }
      $sawRecord = $true
      try { $record = [System.IO.File]::ReadAllText($env:MDDS_PC_RECORD).Trim() } catch { $invalid = $true; Start-Sleep -Milliseconds 150; continue }
      $parts = $record -split " "
      if ($parts.Count -ne 7 -or $parts[0] -ne "MDDS_PC_RECORD" -or
          $parts[1] -ne "RUN_ID=$env:MDDS_PC_EXPECTED_RUN" -or
          $parts[2] -ne "NONCE=$env:MDDS_PC_EXPECTED_NONCE" -or
          $parts[3] -ne "TAG=$env:MDDS_PC_LAUNCH_TAG" -or
          -not $parts[4].StartsWith("PID=") -or -not $parts[5].StartsWith("START=") -or
          $parts[6] -ne "JOB=KILL_ON_CLOSE") {
        $invalid = $true; Start-Sleep -Milliseconds 150; continue
      }
      try { $recordPid = [int]$parts[4].Substring(4); $start = [int64]$parts[5].Substring(6) } catch { $invalid = $true; Start-Sleep -Milliseconds 150; continue }
      if ($parts[4] -ne "PID=$recordPid" -or $parts[5] -ne "START=$start") { $invalid = $true; Start-Sleep -Milliseconds 150; continue }
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class MddsPcExactProcess {
  [StructLayout(LayoutKind.Sequential)] public struct FileTime {
    public uint LowDateTime;
    public uint HighDateTime;
  }
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern IntPtr OpenProcess(uint access, [MarshalAs(UnmanagedType.Bool)] bool inheritHandle, int processId);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool GetProcessTimes(IntPtr process, out FileTime creation, out FileTime exit, out FileTime kernel, out FileTime user);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool TerminateProcess(IntPtr process, uint exitCode);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool CloseHandle(IntPtr handle);
  public static bool TryGetCreationFileTime(IntPtr process, out long fileTime) {
    FileTime creation;
    FileTime exit;
    FileTime kernel;
    FileTime user;
    bool ok = GetProcessTimes(process, out creation, out exit, out kernel, out user);
    fileTime = (long)(((ulong)creation.HighDateTime << 32) | creation.LowDateTime);
    return ok;
  }
}
"@
      [uint32]$access = 0x00101001
      $processHandle = [MddsPcExactProcess]::OpenProcess($access, $false, $recordPid)
      if ($processHandle -eq [IntPtr]::Zero) {
        $openError = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($openError -eq 87) { Write-Output "PENDING_PC_RECORD_GONE_JOB_CLOSED"; exit 0 }
        Write-Output "PENDING_PC_RECORD_OPEN_FAILED win32=$openError"
        exit 74
      }
      $resultMessage = "PENDING_PC_RECORD_LIVE"
      $resultCode = 75
      try {
        [int64]$handleStart = 0
        if (-not [MddsPcExactProcess]::TryGetCreationFileTime($processHandle, [ref]$handleStart)) {
          $resultMessage = "PENDING_PC_RECORD_GET_TIMES_FAILED win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
          $resultCode = 74
        } elseif ($handleStart -ne $start) {
          $resultMessage = "PENDING_PC_RECORD_REUSED"
          $resultCode = 73
        } else {
          $terminateOk = [MddsPcExactProcess]::TerminateProcess($processHandle, 0)
          $terminateError = 0
          if (-not $terminateOk) { $terminateError = [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
          $waitResult = [MddsPcExactProcess]::WaitForSingleObject($processHandle, 3000)
          if ($waitResult -eq 0) {
            $resultMessage = "PENDING_PC_RECORD_STOPPED_JOB_CLOSED terminate_ok=$terminateOk terminate_win32=$terminateError"
            $resultCode = 0
          } elseif (-not $terminateOk) {
            $resultMessage = "PENDING_PC_RECORD_SIGNAL_FAILED win32=$terminateError"
            $resultCode = 74
          } elseif ($waitResult -eq 258) {
            $resultMessage = "PENDING_PC_RECORD_LIVE"
            $resultCode = 75
          } else {
            $resultMessage = "PENDING_PC_RECORD_WAIT_FAILED win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
            $resultCode = 74
          }
        }
      } finally {
        [MddsPcExactProcess]::CloseHandle($processHandle) | Out-Null
      }
      Write-Output $resultMessage
      exit $resultCode
    }
    if (-not $sawRecord) { Write-Output "PENDING_PC_RECORD_UNRESOLVED"; exit 76 }
    if ($invalid) { Write-Output "PENDING_PC_RECORD_INVALID" } else { Write-Output "PENDING_PC_RECORD_UNRESOLVED" }
    exit 76
  ' 2>&1 || true)
  unset MDDS_PC_RECORD MDDS_PC_INTENT MDDS_PC_STATUS MDDS_PC_CANCEL MDDS_PC_EXPECTED_RUN MDDS_PC_EXPECTED_NONCE MDDS_PC_LAUNCH_TAG
  printf 'local_intent=%s local_record=%s local_status=%s cancel=%s tag=%s result=%s\n' "$intent_file" "$record_file" "$status_file" "$cancel_file" "$tag" \
    "$(printf '%s' "$out" | tr -d '\r\n')" >> "$LOGDIR/pending_cleanup_records.txt"
  case "$out" in
    *PENDING_PC_STATUS_CANCELLED_PRECMD*|*PENDING_PC_STATUS_RECORD_WRITE_FAILED*|*PENDING_PC_STATUS_INTENT_INVALID*|*PENDING_PC_STATUS_JOB_ASSIGNMENT_FAILED*|*PENDING_PC_RECORD_GONE_JOB_CLOSED*|*PENDING_PC_RECORD_STOPPED_JOB_CLOSED*)
      return 0 ;;
    *)
      echo "   ERROR: pending PC launch record could not be safely recovered: $record_file" >&2
      return 1 ;;
  esac
}

pc_cleanup_pending_records() {
  local snapshot="$PENDING_PC_RECORDS" pair record_file tag kept="" rc=0
  for pair in $snapshot; do
    record_file=${pair%:*}
    tag=${pair##*:}
    if ! pc_cleanup_pending_record "$record_file" "$tag"; then
      kept="$kept $pair"
      rc=1
    fi
  done
  PENDING_PC_RECORDS="$kept"
  return "$rc"
}

pc_assert_job_proof_child_gone() { # <host-local Job Object proof path> <launch-tag>
  local proof_file="$1" tag="$2" out
  export MDDS_PC_JOB_PROOF="$(cygpath -w "$proof_file")"
  export MDDS_PC_EXPECTED_RUN="$RUN_ID"
  export MDDS_PC_EXPECTED_NONCE="$RUN_NONCE"
  export MDDS_PC_LAUNCH_TAG="$tag"
  out=$(powershell -NoProfile -NonInteractive -Command '
    try {
      if (-not (Test-Path -LiteralPath $env:MDDS_PC_JOB_PROOF)) { Write-Output "PC_JOB_CHILD_PROOF_MISSING"; exit 2 }
      $proof = [System.IO.File]::ReadAllText($env:MDDS_PC_JOB_PROOF).Trim()
      $parts = $proof -split " "
      if ($parts.Count -ne 7 -or $parts[0] -ne "MDDS_PC_JOB_PROOF" -or
          $parts[1] -ne "RUN_ID=$env:MDDS_PC_EXPECTED_RUN" -or
          $parts[2] -ne "NONCE=$env:MDDS_PC_EXPECTED_NONCE" -or
          $parts[3] -ne "TAG=$env:MDDS_PC_LAUNCH_TAG" -or
          -not $parts[4].StartsWith("ROOT=") -or -not $parts[5].StartsWith("CHILD=") -or
          $parts[6] -ne "FLAGS=KILL_ON_CLOSE") {
        Write-Output "PC_JOB_CHILD_PROOF_INVALID"; exit 2
      }
      if ($parts[4] -notmatch "^ROOT=([0-9]+):([0-9]+)$") {
        Write-Output "PC_JOB_CHILD_PROOF_INVALID"; exit 2
      }
      if ($parts[5] -notmatch "^CHILD=([0-9]+):([0-9]+)$") {
        Write-Output "PC_JOB_CHILD_PROOF_INVALID"; exit 2
      }
      $childPid = [int]$Matches[1]
      $childStart = [int64]$Matches[2]
      $child = Get-Process -Id $childPid -ErrorAction SilentlyContinue
      if ($null -eq $child) { Write-Output "PC_JOB_CHILD_GONE"; exit 0 }
      if ($child.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $childStart) { Write-Output "PC_JOB_CHILD_REUSED"; exit 3 }
      Write-Output "PC_JOB_CHILD_LIVE"
      exit 4
    } catch {
      Write-Output "PC_JOB_CHILD_PROOF_INVALID"
      Write-Error $_
      exit 2
    }
  ' 2>&1 || true)
  unset MDDS_PC_JOB_PROOF MDDS_PC_EXPECTED_RUN MDDS_PC_EXPECTED_NONCE MDDS_PC_LAUNCH_TAG
  printf 'job_proof=%s tag=%s result=%s\n' "$proof_file" "$tag" \
    "$(printf '%s' "$out" | tr -d '\r\n')" >> "$LOGDIR/pc_cleanup_records.txt"
  case "$out" in
    *PC_JOB_CHILD_GONE*) return 0 ;;
    *) return 1 ;;
  esac
}

gw_reset() {
  local rc=0
  cleanup_pending_launch_records || rc=1
  pc_cleanup_pending_records || rc=1
  pc_stopall || rc=1
  stopall || rc=1
  return "$rc"
}

pull() { # pull <board> <log>
  local tmp="$LOGDIR/$2.raw" clean="$LOGDIR/$2.clean" out="$LOGDIR/$2"
  shell "$1" "if test -f '$REMOTE_LOGDIR/$2' && grep -Fqx 'GW_RUN_ID=$RUN_ID' '$REMOTE_LOGDIR/$2' && grep -Fqx 'GW_RUN_NONCE=$RUN_NONCE' '$REMOTE_LOGDIR/$2'; then echo GW_LOG_BEGIN; cat '$REMOTE_LOGDIR/$2'; else echo GW_LOG_MISSING; fi" > "$tmp" 2>/dev/null || true
  tr -d '\r' < "$tmp" > "$clean"
  if ! grep -q '^GW_LOG_BEGIN$' "$clean" 2>/dev/null; then
    echo "   ERROR: missing fresh log $2 on board $1" >&2
    rm -f "$tmp" "$clean"
    return 1
  fi
  sed '/^GW_LOG_BEGIN$/d;/^GW_RUN_ID=/d;/^GW_RUN_NONCE=/d' "$clean" > "$out"
  rm -f "$tmp" "$clean"
  if [ ! -s "$out" ]; then
    echo "   ERROR: fresh log has no process output: $2 on board $1" >&2
    rm -f "$out"
    return 1
  fi
}

# The gateway suite is evidence about the binaries it actually executes, not
# merely the current source tree.  Libraries run on both boards; the gateway is
# deployed and used only on board A in this topology.
sha256_local() { sha256sum "$1" | cut -d ' ' -f1; }
sha256_remote() { shell "$1" "sha256sum '$2' 2>/dev/null | cut -d ' ' -f1" | tr -d '\r\n'; }

# Test helpers and their configuration are executable test inputs.  Hashing the
# deployed libraries alone is therefore insufficient: a stale or altered Python
# helper/XML/profile could otherwise make a green run describe different tests.
# HDC shell exit status is not a board verdict, so every remote probe prints one
# exact sentinel and the host rejects anything else (including silence/noise).
helper_remote_dir_ready() { # <board>
  local board="$1" out
  out=$(shell "$board" "if test -d '$DEVICE_DIR' && test ! -L '$DEVICE_DIR' && test ! -L '$DEVICE_DIR/mdds_e2e' && mkdir -p '$DEVICE_DIR/mdds_e2e' 2>/dev/null && test -d '$DEVICE_DIR/mdds_e2e' && test ! -L '$DEVICE_DIR/mdds_e2e'; then printf MDDS_HELPER_DIR_READY; else printf MDDS_HELPER_DIR_INVALID; fi" || true)
  out=$(printf '%s' "$out" | tr -d '\r\n')
  [ "$out" = MDDS_HELPER_DIR_READY ]
}

helper_remote_target_ready() { # <board> <remote-file>
  local board="$1" remote="$2" out
  out=$(shell "$board" "if test -d '$DEVICE_DIR' && test ! -L '$DEVICE_DIR' && test -d '$DEVICE_DIR/mdds_e2e' && test ! -L '$DEVICE_DIR/mdds_e2e' && { test ! -e '$remote' || { test -f '$remote' && test ! -L '$remote'; }; }; then printf MDDS_HELPER_TARGET_READY; else printf MDDS_HELPER_TARGET_INVALID; fi" || true)
  out=$(printf '%s' "$out" | tr -d '\r\n')
  [ "$out" = MDDS_HELPER_TARGET_READY ]
}

helper_local_sha256() { # <local-file>; stdout = strict lower-case SHA-256
  local local_path="$1" sum
  if [ ! -f "$local_path" ] || [ -L "$local_path" ]; then
    echo "ERROR: test helper is not a regular non-symlink local file: $local_path" >&2
    return 1
  fi
  sum=$(sha256sum -- "$local_path" 2>/dev/null | cut -d ' ' -f1) || return 1
  if ! [[ "$sum" =~ ^[0-9A-Fa-f]{64}$ ]]; then
    echo "ERROR: could not derive a strict SHA-256 for test helper: $local_path" >&2
    return 1
  fi
  printf '%s\n' "${sum,,}"
}

helper_remote_sha256() { # <board> <remote-file>; stdout = strict lower-case SHA-256
  local board="$1" remote="$2" out
  out=$(shell "$board" "if test -f '$remote' && test ! -L '$remote'; then sum=\$(sha256sum '$remote' 2>/dev/null | cut -d ' ' -f1); printf 'MDDS_HELPER_SHA256=%s' \"\$sum\"; else printf MDDS_HELPER_NOT_REGULAR; fi" || true)
  out=$(printf '%s' "$out" | tr -d '\r\n')
  if ! [[ "$out" =~ ^MDDS_HELPER_SHA256=([0-9A-Fa-f]{64})$ ]]; then
    echo "ERROR: remote test-helper SHA probe failed for $board:$remote: ${out:-NO_SENTINEL}" >&2
    return 1
  fi
  printf '%s\n' "${BASH_REMATCH[1],,}"
}

render_gateway_config() {
  # The template is source-controlled, while the rendered config is a
  # run-scoped executable input.  Record and transfer the latter, not a
  # generic /chatter config, so stale external publishers cannot pollute the
  # pass criteria.
  local tmp topic
  if [ ! -f "$GW_CONFIG_TEMPLATE" ] || [ -L "$GW_CONFIG_TEMPLATE" ]; then
    echo "ERROR: missing or symlinked gateway config template: $GW_CONFIG_TEMPLATE" >&2
    return 1
  fi
  tmp="$GW_CONFIG_LOCAL.tmp.$$"
  rm -f "$tmp"
  if ! sed \
    -e "s|@GW_TOPIC_CHATTER@|$GW_TOPIC_CHATTER|g" \
    -e "s|@GW_TOPIC_CHATTER_BACK@|$GW_TOPIC_CHATTER_BACK|g" \
    -e "s|@GW_TOPIC_SWEEP@|$GW_TOPIC_SWEEP|g" \
    -e "s|@GW_TOPIC_LAT_REQ@|$GW_TOPIC_LAT_REQ|g" \
    -e "s|@GW_TOPIC_LAT_RSP@|$GW_TOPIC_LAT_RSP|g" \
    "$GW_CONFIG_TEMPLATE" > "$tmp"; then
    rm -f "$tmp"
    echo "ERROR: could not render gateway config" >&2
    return 1
  fi
  if grep -Eq '@GW_TOPIC_(CHATTER|CHATTER_BACK|SWEEP|LAT_REQ|LAT_RSP)@' "$tmp"; then
    rm -f "$tmp"
    echo "ERROR: gateway config retained an unexpanded topic placeholder" >&2
    return 1
  fi
  for topic in "$GW_TOPIC_CHATTER" "$GW_TOPIC_CHATTER_BACK" "$GW_TOPIC_SWEEP" \
    "$GW_TOPIC_LAT_REQ" "$GW_TOPIC_LAT_RSP"; do
    if [ "$(grep -Fxc "topic = $topic" "$tmp")" -ne 1 ]; then
      rm -f "$tmp"
      echo "ERROR: rendered gateway config is missing or duplicates topic $topic" >&2
      return 1
    fi
  done
  # Fail closed if a source edit made the test PC and gateway domains diverge,
  # or duplicated either key.  The generated configuration is the exact
  # input hashed and deployed below, so this catches configuration drift
  # before a board or PC process is launched.
  if [ "$(grep -Ec '^[[:space:]]*cyclone_domain_id[[:space:]]*=' "$tmp")" -ne 1 ] || \
     [ "$(grep -Ec '^[[:space:]]*mdds_domain_id[[:space:]]*=' "$tmp")" -ne 1 ] || \
     [ "$(grep -Fxc "cyclone_domain_id = $GW_CYCLONE_DOMAIN" "$tmp")" -ne 1 ] || \
     [ "$(grep -Fxc "mdds_domain_id = $GW_MDDS_DOMAIN" "$tmp")" -ne 1 ]; then
    rm -f "$tmp"
    echo "ERROR: gateway test config must contain exactly cyclone_domain_id=$GW_CYCLONE_DOMAIN and mdds_domain_id=$GW_MDDS_DOMAIN" >&2
    return 1
  fi
  mv -f "$tmp" "$GW_CONFIG_LOCAL"
}

record_helper_transfer() { # <label> <board> <local> <remote> <local-sha|-> <remote-sha|-> <result>
  printf 'label=%s board=%s local=%s remote=%s local_sha256=%s remote_sha256=%s result=%s\n' \
    "$1" "$2" "$3" "$4" "$5" "$6" "$7" >> "$LOGDIR/helper_transfer_transcript.txt"
}

send_verified_helper() { # <board> <label> <local-file> <remote-file>
  local board="$1" label="$2" local_path="$3" remote="$4" want got
  if ! want=$(helper_local_sha256 "$local_path"); then
    record_helper_transfer "$label" "$board" "$local_path" "$remote" - - LOCAL_NOT_REGULAR || true
    return 1
  fi
  if ! helper_remote_dir_ready "$board"; then
    record_helper_transfer "$label" "$board" "$local_path" "$remote" "$want" - REMOTE_DIR_INVALID || true
    echo "ERROR: remote helper directory is not a regular directory on $board" >&2
    return 1
  fi
  if ! helper_remote_target_ready "$board" "$remote"; then
    record_helper_transfer "$label" "$board" "$local_path" "$remote" "$want" - REMOTE_TARGET_INVALID || true
    echo "ERROR: remote helper target is not a regular non-symlink file path on $board: $remote" >&2
    return 1
  fi
  if ! "$HDC" -t "$board" file send "$(cygpath -w "$local_path")" "$remote" </dev/null >/dev/null; then
    record_helper_transfer "$label" "$board" "$local_path" "$remote" "$want" - HDC_SEND_FAILED || true
    echo "ERROR: failed to send verified test helper $label to $board" >&2
    return 1
  fi
  if ! got=$(helper_remote_sha256 "$board" "$remote"); then
    record_helper_transfer "$label" "$board" "$local_path" "$remote" "$want" - REMOTE_HASH_INVALID || true
    return 1
  fi
  if [ "$got" != "$want" ]; then
    record_helper_transfer "$label" "$board" "$local_path" "$remote" "$want" "$got" SHA256_MISMATCH || true
    echo "ERROR: test-helper SHA-256 mismatch for $label on $board" >&2
    return 1
  fi
  record_helper_transfer "$label" "$board" "$local_path" "$remote" "$want" "$got" VERIFIED || return 1
}

verify_final_artifacts() {
  local local_path name board remote want got
  : > "$LOGDIR/artifact_hashes.txt"
  # The board-side RMW launch environment is part of the executable test
  # configuration: it selects the strict DSoftBus-only deployment profile.
  # Treat it as an artifact so a stale/modified profile cannot turn a green
  # gateway run into evidence for a different transport configuration.
  for name in libmdds.so librmw_mdds.so ohos_dsoftbus.env mdds_gateway; do
    case "$name" in
      libmdds.so)
        local_path="install_ohos/lib/libmdds.so"
        remote="$DEVICE_DIR/lib/libmdds.so"
        ;;
      librmw_mdds.so)
        local_path="install_ohos/lib/librmw_mdds.so"
        remote="$DEVICE_DIR/lib/librmw_mdds.so"
        ;;
      ohos_dsoftbus.env)
        local_path="install_ohos/share/rmw_mdds/config/ohos_dsoftbus.env"
        remote="$DEVICE_DIR/share/rmw_mdds/config/ohos_dsoftbus.env"
        ;;
      mdds_gateway)
        local_path="install_ohos/lib/mdds_gateway/mdds_gateway"
        remote="$DEVICE_DIR/lib/mdds_gateway/mdds_gateway"
        ;;
    esac
    if [ ! -f "$local_path" ]; then
      echo "missing local final artifact: $local_path" | tee -a "$LOGDIR/artifact_hashes.txt"
      return 1
    fi
    want=$(sha256_local "$local_path")
    # Board B never runs mdds_gateway in the configured A-gateway topology.
    for board in "$BOARD_A" $([ "$name" = mdds_gateway ] || echo "$BOARD_B"); do
      got=$(sha256_remote "$board" "$remote")
      printf '%s board=%s local=%s remote=%s\n' "$name" "$board" "$want" "${got:-MISSING}" \
        | tee -a "$LOGDIR/artifact_hashes.txt"
      if [ -z "$got" ] || [ "$got" != "$want" ]; then
        echo "artifact mismatch: $name on $board" | tee -a "$LOGDIR/artifact_hashes.txt"
        return 1
      fi
    done
  done
}

# A successful data assertion alone is not DSoftBus-only evidence: a stale
# profile could activate UDP alongside DSoftBus.  Each board-B RMW process
# writes this startup line, so reject the log unless it proves that DSoftBus is
# active and UDP is absent for that exact process.
assert_rmw_dsoftbus_only_log() { # <local log basename>
  local log="$1"
  grep -Eq 'mdds transports active:.*dsoftbus\(' "$LOGDIR/$log" && \
    ! grep -Eq 'mdds transports active:.*udp\(' "$LOGDIR/$log"
}

# Gateway startup is independently polled before each scenario.  Retaining the
# same assertion on the pulled log makes the final evidence self-contained.
assert_gateway_dsoftbus_only_log() { # <local log basename>
  local log="$1"
  grep -Fq 'mdds transports requested=[dsoftbus] active=[dsoftbus(' "$LOGDIR/$log" && \
    ! grep -Fq 'udp(' "$LOGDIR/$log"
}

# A count observed by a downstream PC node is not sufficient evidence that the
# reliable mdds -> CycloneDDS relay stayed healthy.  A TopicBridge deliberately
# exits after an ACK-fence timeout, ingress loss, or publish exception; the
# ownership-fenced reset below correctly treats an already-gone process as
# cleaned up.  Therefore every scenario that exercises this direction must
# inspect the final per-topic counters before it can pass.
#
# assert_gateway_m2c_healthy <gateway-log> <topic> <min-forwarded> <min-ack-batches>
assert_gateway_m2c_healthy() {
  local log="$1" topic="$2" min_forwarded="$3" min_ack_batches="$4"
  local path final_line forwarded ack_batches ack_timeouts messages_lost resource_drops terminal forward_exceptions required_batches
  if ! [[ "$log" =~ ^[A-Za-z0-9._-]+\.log$ && "$topic" =~ ^/[A-Za-z0-9_/-]+$ && \
          "$min_forwarded" =~ ^[1-9][0-9]*$ && "$min_ack_batches" =~ ^[1-9][0-9]*$ ]]; then
    echo "   ERROR: invalid gateway mdds->cyclone health assertion arguments" >&2
    return 1
  fi
  path="$LOGDIR/$log"
  final_line=$(grep -F "$topic final:" "$path" 2>/dev/null | tail -n 1)
  if [ -z "$final_line" ]; then
    echo "   $log: missing final mdds->cyclone counters for $topic" >&2
    return 1
  fi
  forwarded=$(sed -n 's/.*mdds->cyclone=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  ack_batches=$(sed -n 's/.*m2c_ack_batches=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  ack_timeouts=$(sed -n 's/.*m2c_ack_timeouts=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  messages_lost=$(sed -n 's/.*m2c_messages_lost=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  resource_drops=$(sed -n 's/.*m2c_resource_drops=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  terminal=$(sed -n 's/.*m2c_terminal=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  forward_exceptions=$(sed -n 's/.*m2c_forward_exceptions=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  if ! [[ "$forwarded" =~ ^[0-9]+$ && "$ack_batches" =~ ^[0-9]+$ && \
          "$ack_timeouts" =~ ^[0-9]+$ && "$messages_lost" =~ ^[0-9]+$ && \
          "$resource_drops" =~ ^[0-9]+$ && "$terminal" =~ ^[0-9]+$ && \
          "$forward_exceptions" =~ ^[0-9]+$ ]]; then
    echo "   $log: malformed final mdds->cyclone counters for $topic" >&2
    return 1
  fi
  # The bridge fences every eight publishes and fences the remaining tail on
  # shutdown.  Require the final counter to cover every sample it reports,
  # not merely the scenario's minimum observed at the PC.
  required_batches=$(((forwarded + 7) / 8))
  if (( forwarded < min_forwarded || ack_batches < min_ack_batches || ack_batches < required_batches ||
        ack_timeouts != 0 || messages_lost != 0 || resource_drops != 0 ||
        terminal != 0 || forward_exceptions != 0 )); then
    echo "   $log: unhealthy mdds->cyclone final for $topic (ACK batches=$ack_batches, need >=$required_batches): $final_line" >&2
    return 1
  fi
  if grep -Eq 'terminal bridge failure|mdds->cyclone ACK fence (failed|timed out)|mdds->cyclone ingress loss|mdds->cyclone publish failed|mdds->cyclone forwarding thread failed|mdds_gateway: terminal executor failure' "$path"; then
    echo "   $log: terminal mdds->cyclone failure was logged" >&2
    return 1
  fi
  return 0
}

# The reverse direction is also fail-closed.  The gateway's MDDS writer uses
# finite KEEP_ALL history: a write rejection, callback exception, malformed
# serialized message, pre-activation drop, or history-cap pressure is terminal
# rather than a silent KEEP_LAST overwrite.  `gw_reset` correctly accepts an
# already-gone owned process, so threshold data evidence alone cannot prove a
# healthy C->M relay.
#
# assert_gateway_c2m_healthy <gateway-log> <topic> <min-forwarded>
assert_gateway_c2m_healthy() {
  local log="$1" topic="$2" min_forwarded="$3"
  local path final_line forwarded terminal write_rejections callback_exceptions invalid_messages pre_activation sample_rejections byte_rejections
  if ! [[ "$log" =~ ^[A-Za-z0-9._-]+\.log$ && "$topic" =~ ^/[A-Za-z0-9_/-]+$ && \
          "$min_forwarded" =~ ^[1-9][0-9]*$ ]]; then
    echo "   ERROR: invalid gateway cyclone->mdds health assertion arguments" >&2
    return 1
  fi
  path="$LOGDIR/$log"
  final_line=$(grep -F "$topic final:" "$path" 2>/dev/null | tail -n 1)
  if [ -z "$final_line" ]; then
    echo "   $log: missing final cyclone->mdds counters for $topic" >&2
    return 1
  fi
  forwarded=$(sed -n 's/.*cyclone->mdds=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  terminal=$(sed -n 's/.*c2m_terminal=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  write_rejections=$(sed -n 's/.*c2m_write_rejections=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  callback_exceptions=$(sed -n 's/.*c2m_callback_exceptions=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  invalid_messages=$(sed -n 's/.*c2m_invalid_serialized_messages=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  pre_activation=$(sed -n 's/.*c2m_pre_activation_drops=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  sample_rejections=$(sed -n 's/.*c2m_history_sample_rejections=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  byte_rejections=$(sed -n 's/.*c2m_history_byte_rejections=\([0-9][0-9]*\).*/\1/p' <<< "$final_line")
  if ! [[ "$forwarded" =~ ^[0-9]+$ && "$terminal" =~ ^[0-9]+$ && \
          "$write_rejections" =~ ^[0-9]+$ && "$callback_exceptions" =~ ^[0-9]+$ && \
          "$invalid_messages" =~ ^[0-9]+$ && "$pre_activation" =~ ^[0-9]+$ && \
          "$sample_rejections" =~ ^[0-9]+$ && "$byte_rejections" =~ ^[0-9]+$ ]]; then
    echo "   $log: malformed final cyclone->mdds counters for $topic" >&2
    return 1
  fi
  if (( forwarded < min_forwarded || terminal != 0 || write_rejections != 0 ||
        callback_exceptions != 0 || invalid_messages != 0 || pre_activation != 0 ||
        sample_rejections != 0 || byte_rejections != 0 )); then
    echo "   $log: unhealthy cyclone->mdds final for $topic: $final_line" >&2
    return 1
  fi
  if grep -Eq 'cyclone->mdds terminal failure|mdds_gateway: terminal executor failure|terminal bridge failure' "$path"; then
    echo "   $log: terminal cyclone->mdds failure was logged" >&2
    return 1
  fi
  return 0
}

# Backend-selection text proves intent, while this trace proves the selected
# backend actually crossed the native DSoftBus Socket/Bytes API boundary.  The
# DSoftBus implementation logs these calls only under MDDS_DEBUG=1, which this
# runner pins for both the gateway and board-B RMW processes.
assert_dsoftbus_socket_bytes_trace() { # <local log basename>
  local log="$1"
  grep -Eq '\[mdds/dsoftbus\] Socket\(' "$LOGDIR/$log" && \
    grep -Eq '\[mdds/dsoftbus\] Listen\(fd=[0-9]+\)=0' "$LOGDIR/$log" && \
    grep -Eq '\[mdds/dsoftbus\] BindAsync\(fd=[0-9]+ peer=.*\)=0' "$LOGDIR/$log" && \
    grep -Eq '\[mdds/dsoftbus\] OnBind\(fd=[0-9]+ peer=' "$LOGDIR/$log" && \
    grep -Eq '\[mdds/dsoftbus\] SendBytes\(fd=[0-9]+ len=[0-9]+\)=0' "$LOGDIR/$log" && \
    grep -Eq '\[mdds/dsoftbus\] OnBytes\(fd=[0-9]+ len=[1-9][0-9]* peer=' "$LOGDIR/$log"
}

push_gw_files() {
  : > "$LOGDIR/helper_transfer_transcript.txt" || return 1
  render_gateway_config || return 1
  local board file
  for board in "$BOARD_A" "$BOARD_B"; do
    for file in board_sweep.py publish_constant.py gw_dsoftbus_probe.py; do
      send_verified_helper "$board" "$file" "scripts/mdds_e2e/$file" \
        "$DEVICE_DIR/mdds_e2e/$file" || return 1
    done
  done
  for file in cyclonedds_board_a.xml; do
    send_verified_helper "$BOARD_A" "$file" "scripts/mdds_e2e/$file" \
      "$DEVICE_DIR/mdds_e2e/$file" || return 1
  done
  send_verified_helper "$BOARD_A" mdds_gateway_test.rendered.conf "$GW_CONFIG_LOCAL" \
    "$DEVICE_DIR/mdds_e2e/mdds_gateway_test.conf" || return 1
  local template_sha config_sha
  template_sha=$(helper_local_sha256 "$GW_CONFIG_TEMPLATE") || return 1
  config_sha=$(helper_local_sha256 "$GW_CONFIG_LOCAL") || return 1
  {
    printf 'template=%s sha256=%s\n' "$GW_CONFIG_TEMPLATE" "$template_sha"
    printf 'rendered=%s sha256=%s mdds_transport=dsoftbus cyclone_domain_id=%s mdds_domain_id=%s\n' \
      "$GW_CONFIG_LOCAL" "$config_sha" "$GW_CYCLONE_DOMAIN" "$GW_MDDS_DOMAIN"
    printf 'topics chatter=%s chatter_back=%s sweep=%s lat_req=%s lat_rsp=%s\n' \
      "$GW_TOPIC_CHATTER" "$GW_TOPIC_CHATTER_BACK" "$GW_TOPIC_SWEEP" \
      "$GW_TOPIC_LAT_REQ" "$GW_TOPIC_LAT_RSP"
  } > "$LOGDIR/gateway_config_evidence.txt"
}

wait_gateway_dsoftbus_ready() { # <gateway-log-name>
  local log="$1" attempt status expected_domain
  expected_domain="cyclone domain $GW_CYCLONE_DOMAIN, mdds domain $GW_MDDS_DOMAIN"
  for attempt in $(seq 1 30); do
    status=$(shell "$BOARD_A" "if test -f '$REMOTE_LOGDIR/$log' && grep -Fq 'mdds transports requested=[dsoftbus] active=[dsoftbus(' '$REMOTE_LOGDIR/$log' && ! grep -Fq 'udp(' '$REMOTE_LOGDIR/$log' && grep -Fq '$expected_domain' '$REMOTE_LOGDIR/$log'; then printf MDDS_GW_READY; elif test -f '$REMOTE_LOGDIR/$log' && grep -Fq 'mdds_gateway up:' '$REMOTE_LOGDIR/$log' && ! grep -Fq '$expected_domain' '$REMOTE_LOGDIR/$log'; then printf MDDS_GW_DOMAIN_WRONG; elif test -f '$REMOTE_LOGDIR/$log' && grep -Fq 'mdds_gateway up:' '$REMOTE_LOGDIR/$log' && grep -Fq 'mdds transports requested=' '$REMOTE_LOGDIR/$log'; then printf MDDS_GW_DSOFTBUS_WRONG; else printf MDDS_GW_WAIT; fi" || true)
    status=$(printf '%s' "$status" | tr -d '\r\n')
    printf 'log=%s attempt=%s result=%s\n' "$log" "$attempt" "${status:-NO_SENTINEL}" \
      >> "$LOGDIR/gateway_transport_evidence.txt"
    case "$status" in
      MDDS_GW_READY)
        return 0
        ;;
      MDDS_GW_DOMAIN_WRONG)
        echo "ERROR: gateway reported a domain other than Cyclone $GW_CYCLONE_DOMAIN / MDDS $GW_MDDS_DOMAIN in $log" >&2
        return 1
        ;;
      MDDS_GW_DSOFTBUS_WRONG)
        echo "ERROR: gateway reported a non-DSoftBus-only active transport in $log" >&2
        return 1
        ;;
    esac
    sleep 1
  done
  echo "ERROR: gateway did not report active DSoftBus-only transport on Cyclone $GW_CYCLONE_DOMAIN / MDDS $GW_MDDS_DOMAIN in $log" >&2
  return 1
}

# start_gateway <timeout_s> <log>
start_gateway() {
  # Do not wrap the gateway in timeout: exact cleanup must own the gateway
  # itself, otherwise a timeout parent can leave a child behind.
  launch "$BOARD_A" "$GWENVS" \
    "$DEVICE_DIR/lib/mdds_gateway/mdds_gateway -c $DEVICE_DIR/mdds_e2e/mdds_gateway_test.conf" "$2" || return 1
  GW_PID=$LAST_PID
  wait_gateway_dsoftbus_ready "$2" || return 1
}

# pc_start <bat> <log> [bat-args...]
pc_start() {
  local bat="$1" log="$2"; shift 2
  # Keep MSYS2_ARG_CONV_EXCL='*' (script-global, needed for hdc): with
  # conversion disabled, `cmd /c` reaches cmd literally (the //c escape would
  # NOT be undone).  That does not cover MSYS environment conversion: a
  # run-unique absolute ROS topic stored in MDDS_PC_ARGS would otherwise be
  # rewritten to C:/Program Files/Git/... at the Bash -> PowerShell boundary.
  # Exclude only that inherited payload for the guard spawn below.
  local token_file err_file record_file job_proof_file intent_file status_file cancel_file token pair local_record job_proof source raw status attempt bootstrap_shell_pid launch_tag pending msys_env_conv_excl proof_root_pid proof_root_start proof_child_pid proof_child_start
  ensure_pc_owner || return 1
  export MDDS_PC_BATCH="$PC_BAT_DIR\\$bat"
  export MDDS_PC_ARGS="$*"
  export MDDS_PC_STDOUT="$(cygpath -aw "$LOGDIR/$log")"
  export MDDS_PC_STDERR="$(cygpath -aw "$LOGDIR/$log.err")"
  export MDDS_PC_WORKDIR="$(cygpath -w "$PC_WS")"
  token_file="$LOGDIR/$log.pc_guard_token.raw"
  err_file="$LOGDIR/$log.pc_guard_error.raw"
  : > "$token_file" || return 1
  : > "$err_file" || return 1
  record_file=$(mktemp "$LOGDIR/.pc_record_$$.$RANDOM.XXXXXX") || {
    return 1
  }
  # mktemp gives us a collision-free path but creates the file.  The absence of
  # the record before the guard's durable write is required for cancellation to
  # mean "the guard has not reached Start-Process yet".
  rm -f "$record_file"
  LAUNCH_SEQUENCE=$((LAUNCH_SEQUENCE + 1))
  launch_tag="pc${LAUNCH_SEQUENCE}_${RANDOM}_$$"
  intent_file="${record_file}.intent"
  status_file="${record_file}.status"
  cancel_file="${record_file}.cancel"
  job_proof_file="${record_file}.job"
  # Intent is immutable before PENDING or the bootstrap process.  A delayed
  # local replay cannot reach cmd.exe without this exact transaction proof.
  ensure_pc_launch_intent "$record_file" "$launch_tag" || return 1
  pending="$record_file:$launch_tag"
  add_pending_pc_record "$record_file" "$launch_tag"
  export MDDS_PC_RECORD="$(cygpath -w "$record_file")"
  export MDDS_PC_INTENT="$(cygpath -w "$intent_file")"
  export MDDS_PC_STATUS="$(cygpath -w "$status_file")"
  export MDDS_PC_CANCEL="$(cygpath -w "$cancel_file")"
  export MDDS_PC_JOB_PROOF="$(cygpath -w "$job_proof_file")"
  export MDDS_PC_EXPECTED_RUN="$RUN_ID"
  export MDDS_PC_EXPECTED_NONCE="$RUN_NONCE"
  export MDDS_PC_LAUNCH_TAG="$launch_tag"
  export MDDS_PC_FAIL_RECORD_WRITE="$FAIL_RECORD_WRITE"
  export MDDS_PC_SUPPRESS_TOKEN="$SUPPRESS_LAUNCH_TOKEN"
  msys_env_conv_excl="${MSYS2_ENV_CONV_EXCL:-}"
  case ";${msys_env_conv_excl};" in
    *';MDDS_PC_ARGS;'*) ;;
    *) msys_env_conv_excl="${msys_env_conv_excl:+${msys_env_conv_excl};}MDDS_PC_ARGS" ;;
  esac
  # This PowerShell process is the tracked root.  Before it writes a record or
  # starts cmd.exe, it joins a non-inheritable KILL_ON_JOB_CLOSE Job Object.
  # Ordinary CreateProcess descendants then inherit the job, so later cleanup
  # terminates the identity-fenced root only and lets the kernel close the
  # complete child tree.  If job setup is unavailable, launch fails closed.
  MSYS2_ENV_CONV_EXCL="$msys_env_conv_excl" \
  powershell -NoProfile -NonInteractive -Command '
    function Get-CancelState {
      if (-not (Test-Path -LiteralPath $env:MDDS_PC_CANCEL)) { return "NONE" }
      $actual = [System.IO.File]::ReadAllText($env:MDDS_PC_CANCEL).Trim()
      $expected = "MDDS_PC_CANCEL RUN_ID=$env:MDDS_PC_EXPECTED_RUN NONCE=$env:MDDS_PC_EXPECTED_NONCE"
      if ($actual -eq $expected) { return "REQUESTED" }
      return "INVALID"
    }
    function Write-TerminalStatus([string]$state) {
      $line = "MDDS_PC_STATUS RUN_ID=$env:MDDS_PC_EXPECTED_RUN NONCE=$env:MDDS_PC_EXPECTED_NONCE TAG=$env:MDDS_PC_LAUNCH_TAG STATE=$state"
      try {
        $fs = [System.IO.File]::Open($env:MDDS_PC_STATUS, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        try {
          $bytes = [System.Text.Encoding]::UTF8.GetBytes("$line$([Environment]::NewLine)")
          $fs.Write($bytes, 0, $bytes.Length)
          $fs.Flush()
        } finally { $fs.Dispose() }
        return
      } catch [System.IO.IOException] {
        if ((Test-Path -LiteralPath $env:MDDS_PC_STATUS) -and
            (([System.IO.File]::ReadAllText($env:MDDS_PC_STATUS).Trim()) -eq $line)) { return }
        throw "terminal-status collision"
      }
    }
    try {
      $intent = "MDDS_PC_INTENT RUN_ID=$env:MDDS_PC_EXPECTED_RUN NONCE=$env:MDDS_PC_EXPECTED_NONCE TAG=$env:MDDS_PC_LAUNCH_TAG"
      if ((-not (Test-Path -LiteralPath $env:MDDS_PC_INTENT)) -or
          (([System.IO.File]::ReadAllText($env:MDDS_PC_INTENT).Trim()) -ne $intent)) {
        Write-TerminalStatus "INTENT_INVALID"
        exit 70
      }
      $cancelState = Get-CancelState
      if ($cancelState -eq "REQUESTED") { Write-TerminalStatus "CANCELLED_PRECMD"; exit 0 }
      if ($cancelState -ne "NONE") { Write-TerminalStatus "INTENT_INVALID"; exit 72 }
      # KILL_ON_JOB_CLOSE is the process-tree ownership boundary for this
      # launcher.  Create an anonymous (therefore non-inheritable) job and put
      # the guard itself in it *before* cmd.exe can exist.  Normal
      # CreateProcess descendants inherit the job; breakaway flags are neither
      # requested nor accepted here.
      try {
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class MddsGatewayPcJob {
  [StructLayout(LayoutKind.Sequential)] public struct BasicLimitInformation {
    public long PerProcessUserTimeLimit;
    public long PerJobUserTimeLimit;
    public uint LimitFlags;
    public UIntPtr MinimumWorkingSetSize;
    public UIntPtr MaximumWorkingSetSize;
    public uint ActiveProcessLimit;
    public IntPtr Affinity;
    public uint PriorityClass;
    public uint SchedulingClass;
  }
  [StructLayout(LayoutKind.Sequential)] public struct IoCounters {
    public ulong ReadOperationCount;
    public ulong WriteOperationCount;
    public ulong OtherOperationCount;
    public ulong ReadTransferCount;
    public ulong WriteTransferCount;
    public ulong OtherTransferCount;
  }
  [StructLayout(LayoutKind.Sequential)] public struct ExtendedLimitInformation {
    public BasicLimitInformation BasicLimitInformation;
    public IoCounters IoInfo;
    public UIntPtr ProcessMemoryLimit;
    public UIntPtr JobMemoryLimit;
    public UIntPtr PeakProcessMemoryUsed;
    public UIntPtr PeakJobMemoryUsed;
  }
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern IntPtr CreateJobObject(IntPtr attributes, string name);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool SetInformationJobObject(IntPtr job, int informationClass, IntPtr information, uint informationLength);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool QueryInformationJobObject(IntPtr job, int informationClass, IntPtr information, uint informationLength, IntPtr returnLength);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
  [DllImport("kernel32.dll", SetLastError = true)]
  [return: MarshalAs(UnmanagedType.Bool)]
  public static extern bool IsProcessInJob(IntPtr process, IntPtr job, [MarshalAs(UnmanagedType.Bool)] out bool result);
}
"@
        $job = [MddsGatewayPcJob]::CreateJobObject([IntPtr]::Zero, $null)
        if ($job -eq [IntPtr]::Zero) { throw "CreateJobObject win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
        $jobInfo = New-Object MddsGatewayPcJob+ExtendedLimitInformation
        # BasicLimitInformation is a value type; PowerShell mutates a boxed
        # nested copy unless it is assigned back to its parent structure.
        $basicLimits = $jobInfo.BasicLimitInformation
        $basicLimits.LimitFlags = [uint32]0x00002000 # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        $jobInfo.BasicLimitInformation = $basicLimits
        $jobInfoSize = [Runtime.InteropServices.Marshal]::SizeOf($jobInfo)
        $jobInfoPtr = [Runtime.InteropServices.Marshal]::AllocHGlobal($jobInfoSize)
        try {
          [Runtime.InteropServices.Marshal]::StructureToPtr($jobInfo, $jobInfoPtr, $false)
          if (-not [MddsGatewayPcJob]::SetInformationJobObject($job, 9, $jobInfoPtr, [uint32]$jobInfoSize)) {
            throw "SetInformationJobObject win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
          }
          [Runtime.InteropServices.Marshal]::StructureToPtr((New-Object MddsGatewayPcJob+ExtendedLimitInformation), $jobInfoPtr, $false)
          if (-not [MddsGatewayPcJob]::QueryInformationJobObject($job, 9, $jobInfoPtr, [uint32]$jobInfoSize, [IntPtr]::Zero)) {
            throw "QueryInformationJobObject win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
          }
          $jobActual = [Runtime.InteropServices.Marshal]::PtrToStructure($jobInfoPtr, [type][MddsGatewayPcJob+ExtendedLimitInformation])
          $jobFlags = [uint32]$jobActual.BasicLimitInformation.LimitFlags
          if ((($jobFlags -band [uint32]0x00002000) -eq 0) -or (($jobFlags -band [uint32]0x00001800) -ne 0)) {
            throw "Job Object flags rejected: 0x$($jobFlags.ToString("X8"))"
          }
          $self = Get-Process -Id $PID -ErrorAction Stop
          if (-not [MddsGatewayPcJob]::AssignProcessToJobObject($job, $self.Handle)) {
            throw "AssignProcessToJobObject(self) win32=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
          }
          [bool]$selfInJob = $false
          if ((-not [MddsGatewayPcJob]::IsProcessInJob($self.Handle, $job, [ref]$selfInJob)) -or (-not $selfInJob)) {
            throw "IsProcessInJob(self) rejected the containment contract"
          }
          # Do not CloseHandle($job): this anonymous, non-inheritable handle
          # must remain owned solely by the guard until it exits.  Its final
          # close is what atomically tears down normal job descendants.
        } finally {
          [Runtime.InteropServices.Marshal]::FreeHGlobal($jobInfoPtr)
        }
      } catch {
        # This happens before durable record creation and before cmd.exe, so a
        # terminal status is a safe proof that no PC workload was launched.
        Write-TerminalStatus "JOB_ASSIGNMENT_FAILED"
        Write-Error $_
        exit 70
      }
      $self = Get-Process -Id $PID -ErrorAction Stop
      $start = $self.StartTime.ToUniversalTime().ToFileTimeUtc()
      $record = "MDDS_PC_RECORD RUN_ID=$env:MDDS_PC_EXPECTED_RUN NONCE=$env:MDDS_PC_EXPECTED_NONCE TAG=$env:MDDS_PC_LAUNCH_TAG PID=$PID START=$start JOB=KILL_ON_CLOSE"
      if ($env:MDDS_PC_FAIL_RECORD_WRITE -eq "1") { Write-TerminalStatus "RECORD_WRITE_FAILED"; exit 70 }
      # CreateNew is deliberate: a replay of the bootstrap command must fail
      # before cmd.exe is started rather than overwrite the first guard record.
      if (Test-Path -LiteralPath $env:MDDS_PC_RECORD) { throw "PC launch record already exists" }
      try {
        $recordStream = [System.IO.File]::Open($env:MDDS_PC_RECORD, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
      } catch [System.IO.IOException] {
        if (Test-Path -LiteralPath $env:MDDS_PC_RECORD) { throw "PC launch record already exists" }
        Write-TerminalStatus "RECORD_WRITE_FAILED"
        exit 70
      }
      try {
        $recordBytes = [System.Text.Encoding]::UTF8.GetBytes("$record$([Environment]::NewLine)")
        $recordStream.Write($recordBytes, 0, $recordBytes.Length)
        $recordStream.Flush()
      } finally { $recordStream.Dispose() }
      if (([System.IO.File]::ReadAllText($env:MDDS_PC_RECORD).Trim()) -ne $record) { Write-TerminalStatus "RECORD_WRITE_FAILED"; exit 70 }
      $cancelState = Get-CancelState
      if ($cancelState -eq "REQUESTED") { Write-TerminalStatus "CANCELLED_PRECMD"; exit 0 }
      if ($cancelState -ne "NONE") { Write-TerminalStatus "INTENT_INVALID"; exit 72 }
      $cmd = [string]::Concat([char]99,[char]97,[char]108,[char]108,[char]32,[char]34,$env:MDDS_PC_BATCH,[char]34,[char]32,$env:MDDS_PC_ARGS)
      $child = Start-Process -FilePath "cmd.exe" -ArgumentList @("/c", $cmd) -WorkingDirectory $env:MDDS_PC_WORKDIR -RedirectStandardOutput $env:MDDS_PC_STDOUT -RedirectStandardError $env:MDDS_PC_STDERR -WindowStyle Hidden -PassThru
      $child.Refresh()
      [bool]$childInJob = $false
      if ((-not [MddsGatewayPcJob]::IsProcessInJob($child.Handle, $job, [ref]$childInJob)) -or (-not $childInJob)) {
        & taskkill.exe /PID $child.Id /T /F | Out-Null
        throw "IsProcessInJob(cmd.exe) rejected the containment contract"
      }
      $childStart = $child.StartTime.ToUniversalTime().ToFileTimeUtc()
      $jobProof = "MDDS_PC_JOB_PROOF RUN_ID=$env:MDDS_PC_EXPECTED_RUN NONCE=$env:MDDS_PC_EXPECTED_NONCE TAG=$env:MDDS_PC_LAUNCH_TAG ROOT=${PID}:$start CHILD=$($child.Id):$childStart FLAGS=KILL_ON_CLOSE"
      if (Test-Path -LiteralPath $env:MDDS_PC_JOB_PROOF) { throw "PC Job Object proof already exists" }
      $proofStream = [System.IO.File]::Open($env:MDDS_PC_JOB_PROOF, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
      try {
        $proofBytes = [System.Text.Encoding]::UTF8.GetBytes("$jobProof$([Environment]::NewLine)")
        $proofStream.Write($proofBytes, 0, $proofBytes.Length)
        $proofStream.Flush()
      } finally { $proofStream.Dispose() }
      if (([System.IO.File]::ReadAllText($env:MDDS_PC_JOB_PROOF).Trim()) -ne $jobProof) { throw "PC Job Object proof write failed" }
      if ($env:MDDS_PC_SUPPRESS_TOKEN -ne "1") { Write-Output "GW_PC_ROOT=${PID}:$start" }
      $child.WaitForExit()
      $child.Refresh()
      exit $child.ExitCode
    } catch {
      Write-Error $_
      exit 70
    }
  ' >"$token_file" 2>"$err_file" &
  bootstrap_shell_pid=$!
  unset MDDS_PC_RECORD MDDS_PC_INTENT MDDS_PC_STATUS MDDS_PC_CANCEL MDDS_PC_JOB_PROOF MDDS_PC_EXPECTED_RUN MDDS_PC_EXPECTED_NONCE MDDS_PC_LAUNCH_TAG MDDS_PC_FAIL_RECORD_WRITE MDDS_PC_SUPPRESS_TOKEN
  pair=""
  job_proof=""
  status=""
  for attempt in $(seq 1 20); do
    if drop_first_record_read_if_requested pc "$record_file"; then raw=""; else raw=$(read_pc_launch_record "$record_file"); fi
    pair=$(printf '%s' "$raw" | parse_pc_record "$launch_tag")
    raw=$(read_pc_job_proof "$job_proof_file")
    job_proof=$(printf '%s' "$raw" | parse_pc_job_proof "$launch_tag")
    if [[ "$pair" =~ ^[0-9]+:[0-9]+$ && "$job_proof" =~ ^[0-9]+:[0-9]+:[0-9]+:[0-9]+$ ]]; then
      IFS=: read -r proof_root_pid proof_root_start proof_child_pid proof_child_start <<< "$job_proof"
      [ "$pair" = "$proof_root_pid:$proof_root_start" ] && break
    fi
    raw=$(read_pc_launch_status "$status_file")
    status=$(printf '%s' "$raw" | parse_pc_status "$launch_tag")
    [ -n "$status" ] && break
    sleep 0.2
  done
  token=$(tr -d '\r\n' < "$token_file")
  pair=$(echo "$token" | sed -n 's/^GW_PC_ROOT=\([0-9][0-9]*:[0-9][0-9]*\)$/\1/p')
  raw=$(read_pc_launch_record "$record_file")
  local_record=$(printf '%s' "$raw" | parse_pc_record "$launch_tag")
  raw=$(read_pc_job_proof "$job_proof_file")
  job_proof=$(printf '%s' "$raw" | parse_pc_job_proof "$launch_tag")
  IFS=: read -r proof_root_pid proof_root_start proof_child_pid proof_child_start <<< "$job_proof"
  if [ "$FORCE_PC_POST_RECORD_MISMATCH" = 1 ]; then
    # A distinct syntactically valid identity is enough to force the normal
    # mismatch recovery branch without fabricating a record or touching an
    # unrelated process.  The pending record remains authoritative.
    pair="0:0"
    printf 'record=%s job_proof=%s tag=%s injected=force-post-record-mismatch\n' \
      "$record_file" "$job_proof_file" "$launch_tag" >> "$LOGDIR/pc_launch_faults.txt"
  fi
  if ! [[ "$local_record" =~ ^[0-9]+:[0-9]+$ && "$job_proof" =~ ^[0-9]+:[0-9]+:[0-9]+:[0-9]+$ &&
          "$proof_root_pid" =~ ^[0-9]+$ && "$proof_root_start" =~ ^[0-9]+$ &&
          "$proof_child_pid" =~ ^[0-9]+$ && "$proof_child_start" =~ ^[0-9]+$ ]] ||
     [ "$local_record" != "$proof_root_pid:$proof_root_start" ]; then
    if [ -n "$status" ]; then
      echo "   ERROR: PC launch acknowledged terminal pre-command state=$status for $bat" >&2
    else
      echo "   ERROR: no exact run-owned PC guard record/Job Object proof for $bat (bootstrap shell pid=$bootstrap_shell_pid)" >&2
    fi
    pc_cleanup_pending_record "$record_file" "$launch_tag" && remove_pending_pc_record "$pending"
    return 1
  fi
  if [[ "$pair" =~ ^[0-9]+:[0-9]+$ ]] && [ "$pair" != "$local_record" ]; then
    echo "   ERROR: PC launch token disagrees with exact persistent guard record for $bat" >&2
    pc_cleanup_pending_record "$record_file" "$launch_tag" && remove_pending_pc_record "$pending"
    return 1
  fi
  source=local-record
  [[ "$pair" = "$local_record" ]] && source=stdout+local-record
  # Track before removing PENDING: signal delivery in this handoff remains
  # conservative and can never turn a live guard into an unowned one.
  PC_TRACKED="$PC_TRACKED $record_file:$launch_tag"
  remove_pending_pc_record "$pending"
  printf 'pid=%s start=%s child_pid=%s child_start=%s source=%s local_intent=%s local_record=%s job_proof=%s local_status=%s run_id=%s nonce=%s tag=%s bootstrap_shell_pid=%s log=%s\n' \
    "${local_record%%:*}" "${local_record#*:}" "$proof_child_pid" "$proof_child_start" "$source" "$intent_file" "$record_file" "$job_proof_file" "$status_file" "$RUN_ID" "$RUN_NONCE" "$launch_tag" "$bootstrap_shell_pid" "$log" >> "$LOGDIR/pc_launch_records.txt"
  if [ "$source" = local-record ]; then
    echo "   recovered PC launch identity from persistent local record: $local_record" >&2
  fi
}

pass=0; fail=0; failed_ids=()
verdict() { # verdict <ID> <0|1> [detail]
  if [ "$2" -eq 0 ]; then echo "$1 PASS $3"; pass=$((pass+1));
  else echo "$1 FAIL $3"; fail=$((fail+1)); failed_ids+=("$1"); fi
}
heard_count() {
  grep -h "I heard" "$LOGDIR/$1" "$LOGDIR/$1.err" 2>/dev/null | wc -l | tr -d ' '
}
dup_max() { # highest per-message delivery count in a listener log (0 = none heard)
  local n
  n=$(grep -h -o "I heard: \[[^]]*\]" "$LOGDIR/$1" "$LOGDIR/$1.err" 2>/dev/null | sort | uniq -c | awk '{print $1}' | sort -rn | head -1)
  echo "${n:-0}"
}

# --- scenarios ---------------------------------------------------------------

s_gw01() {
  # PC talker -> gateway -> board B rmw_mdds listener. The MDDS
  # leg is DSoftBus Socket/Bytes; putting the listener on A would require a
  # second same-domain DSoftBus session beside the gateway and is not a valid
  # DSoftBus topology.
  gw_reset || return 1
  rbg "$BOARD_B" "\$ROS2_LISTENER --ros-args -r chatter:=$GW_TOPIC_CHATTER" gw01_b_listener.log || return 1
  start_gateway 120 gw01_gw.log || return 1
  sleep 6
  pc_start gw_pc_talker.bat gw01_pc_talker.log "$GW_TOPIC_CHATTER" || return 1
  sleep 30
  gw_reset || return 1
  pull "$BOARD_B" gw01_b_listener.log || return 1
  pull "$BOARD_A" gw01_gw.log || return 1
  local n transport_ok=0; n=$(heard_count gw01_b_listener.log)
  assert_rmw_dsoftbus_only_log gw01_b_listener.log && \
    assert_gateway_dsoftbus_only_log gw01_gw.log && \
    assert_gateway_c2m_healthy gw01_gw.log "$GW_TOPIC_CHATTER" 15 && \
    assert_dsoftbus_socket_bytes_trace gw01_b_listener.log && \
    assert_dsoftbus_socket_bytes_trace gw01_gw.log || transport_ok=1
  [ "$n" -ge 15 ] && [ "$transport_ok" -eq 0 ]
  verdict "GW-01" $? "PC->B over DSoftBus heard=$n (>=15), transport_only=$transport_ok"
}

s_gw02() {
  # board B rmw_mdds talker -> gateway -> PC listener
  gw_reset || return 1
  pc_start gw_pc_listener.bat gw02_pc_listener.log "$GW_TOPIC_CHATTER" || return 1
  start_gateway 120 gw02_gw.log || return 1
  sleep 6
  rbg "$BOARD_B" "\$ROS2_TALKER --ros-args -r chatter:=$GW_TOPIC_CHATTER" gw02_talker.log || return 1
  sleep 30
  gw_reset || return 1
  pull "$BOARD_B" gw02_talker.log || return 1
  pull "$BOARD_A" gw02_gw.log || return 1
  local n checks_failed=0; n=$(heard_count gw02_pc_listener.log)
  assert_rmw_dsoftbus_only_log gw02_talker.log && \
    assert_gateway_dsoftbus_only_log gw02_gw.log && \
    assert_gateway_m2c_healthy gw02_gw.log "$GW_TOPIC_CHATTER" 15 2 && \
    assert_dsoftbus_socket_bytes_trace gw02_talker.log && \
    assert_dsoftbus_socket_bytes_trace gw02_gw.log || checks_failed=1
  [ "$n" -ge 15 ] && [ "$checks_failed" -eq 0 ]
  verdict "GW-02" $? "B->PC heard=$n (>=15), checks_failed=$checks_failed"
}

s_gw03() {
  # Bidirectional 60 s: PC talker -> B subscription; B publisher -> PC
  # listener. One B-side rclpy context owns BOTH roles,
  # avoiding two same-domain DSoftBus session registrations on one device.
  gw_reset || return 1
  start_gateway 180 gw03_gw.log || return 1
  rbg "$BOARD_B" \
    "python3.12 $DEVICE_DIR/mdds_e2e/gw_dsoftbus_probe.py duplex --incoming-topic $GW_TOPIC_CHATTER --outgoing-topic $GW_TOPIC_CHATTER_BACK --publish-count 60 --min-received 40 --rate 1 --depth 100" \
    gw03_b_duplex.log || return 1
  sleep 2
  pc_start gw_pc_talker.bat gw03_pc_talker.log "$GW_TOPIC_CHATTER" || return 1
  pc_start gw_pc_listener.bat gw03_pc_listener.log "$GW_TOPIC_CHATTER_BACK" || return 1
  local i complete=0
  for i in $(seq 1 20); do
    sleep 5
    complete=$(shell "$BOARD_B" "grep -c 'DUPLEX_RESULT ' '$REMOTE_LOGDIR/gw03_b_duplex.log' 2>/dev/null || true" | tr -dc '0-9')
    [ -n "$complete" ] && [ "$complete" -ge 1 ] && break
  done
  gw_reset || return 1
  pull "$BOARD_B" gw03_b_duplex.log || return 1
  pull "$BOARD_A" gw03_gw.log || return 1
  local n1 n2 d2
  n1=$(grep -c 'DUPLEX_RX ' "$LOGDIR/gw03_b_duplex.log" || true)
  n2=$(heard_count gw03_pc_listener.log)
  d2=$(dup_max gw03_pc_listener.log)
  grep -Eq 'DUPLEX_RESULT PASS .*duplicates=0' "$LOGDIR/gw03_b_duplex.log" && \
    [ "$n1" -ge 40 ] && [ "$n2" -ge 40 ] && [ "$d2" -le 1 ] && \
    assert_rmw_dsoftbus_only_log gw03_b_duplex.log && \
    assert_gateway_dsoftbus_only_log gw03_gw.log && \
    assert_gateway_c2m_healthy gw03_gw.log "$GW_TOPIC_CHATTER" 40 && \
    assert_gateway_m2c_healthy gw03_gw.log "$GW_TOPIC_CHATTER_BACK" 40 5 && \
    assert_dsoftbus_socket_bytes_trace gw03_b_duplex.log && \
    assert_dsoftbus_socket_bytes_trace gw03_gw.log
  verdict "GW-03" $? "bidir: PC->B received=$n1, B->PC heard=$n2 dup=$d2"
}

s_gw04() {
  # Large messages PC -> board B across the gateway. RELIABLE delivery is
  # strict: every requested size must have an exact count and no loss,
  # reorder, CRC error, or BAD block.
  gw_reset || return 1
  rbg "$BOARD_B" "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode sub --topic $GW_TOPIC_SWEEP --sizes 1024,4096,65536,262144,1048576,4194304 --idle-timeout 60" gw04_sub.log || return 1
  start_gateway 480 gw04_gw.log || return 1
  sleep 6
  # --depth 200: the reliable Cyclone hop over WiFi repairs fragment losses
  # from the writer's history; KEEP_LAST(10) at 18-64 msg/s leaves only
  # ~150-550 ms of repair slack and thrashes (observed: 64KB block lost 174/183).
  # --rate 5: repair slack = depth x period. At 10 Hz a 200-deep history covers
  # only 20 s, and a measured 18-20 s WiFi repair stall evicted the hole first
  # (writer GAPs it -> permanent loss, observed lost=6 in the 64KB block). 5 Hz
  # doubles the window to 40 s. (The Windows sleep floor ~15.6 ms also pins
  # default 50 msg/s pacing to ~64 msg/s, so the rate must be set explicitly.)
  # --rate-bps 800000: byte-rate cap for the >=256KB blocks; keeps the offered
  # load well under what the jittery WiFi + cyclone repair can sustain.
  # idle-timeout 60 + poll 360s: on a jittery WiFi day (ping avg 39ms/max 137ms)
  # cyclone repair of a 64KB burst can legitimately stall the flow for tens of
  # seconds; the sub idle timeout must outlast repair, not assert liveness.
  # flush-ms 60000: the reliable writer must OUTLAST slow repair tails — once
  # the publisher exits, its reader-side holes can never be repaired and the
  # gateway flow freezes permanently (observed: c2m stuck at 661/847).
  pc_start gw_pc_sweep_pub.bat gw04_pub.log --topic "$GW_TOPIC_SWEEP" --sizes 1024,4096,65536,262144,1048576,4194304 --rate 5 --rate-bps 800000 --depth 200 --wait-match --flush-ms 60000 || return 1
  # poll for completion (hdc shell always exits 0 — poll on captured content,
  # never on exit status)
  local i n
  for i in $(seq 1 36); do
    sleep 10
    n=$(shell "$BOARD_B" "grep -c SWEEP_RESULT '$REMOTE_LOGDIR/gw04_sub.log' 2>/dev/null || true" | tr -dc '0-9')
    [ -n "$n" ] && [ "$n" -ge 1 ] && break
  done
  gw_reset || return 1
  pull "$BOARD_B" gw04_sub.log || return 1
  pull "$BOARD_A" gw04_gw.log || return 1
  local bad=0 line recv want
  for size in 1024 4096 65536 262144 1048576 4194304; do
    line=$(grep "SWEEP-SUB size=$size " "$LOGDIR/gw04_sub.log")
    if [ -z "$line" ]; then echo "   missing block size=$size"; bad=1; continue; fi
    echo "$line" | grep -q "lost=0 reorder=0 crc=0" || { echo "   gap/reorder/crc: $line"; bad=1; }
    recv=$(echo "$line" | sed -n 's/.*received=\([0-9]*\)\/.*/\1/p')
    want=$(echo "$line" | sed -n 's/.*received=[0-9]*\/\([0-9]*\).*/\1/p')
    [ -n "$recv" ] && [ "$recv" = "$want" ] || { echo "   inexact count: $line"; bad=1; }
  done
  grep -q 'SWEEP_RESULT PASS' "$LOGDIR/gw04_sub.log" || { echo "   missing SWEEP_RESULT PASS"; bad=1; }
  grep -q ' BAD' "$LOGDIR/gw04_sub.log" && { echo "   subscriber reported BAD"; bad=1; }
  # The six default sweep blocks offer exactly 847 samples.  This assertion
  # binds the strict board-side CRC/count result to a non-terminal gateway
  # C->M handoff with no finite KEEP_ALL history-pressure rejection.
  assert_gateway_c2m_healthy gw04_gw.log "$GW_TOPIC_SWEEP" 847 \
    || { echo "   gateway cyclone->mdds relay was not healthy for the 847-sample sweep"; bad=1; }
  assert_rmw_dsoftbus_only_log gw04_sub.log || { echo "   B subscriber did not prove DSoftBus-only transport"; bad=1; }
  assert_gateway_dsoftbus_only_log gw04_gw.log || { echo "   gateway did not prove DSoftBus-only transport"; bad=1; }
  assert_dsoftbus_socket_bytes_trace gw04_sub.log || { echo "   B subscriber lacks DSoftBus Socket/Bytes trace"; bad=1; }
  assert_dsoftbus_socket_bytes_trace gw04_gw.log || { echo "   gateway lacks DSoftBus Socket/Bytes trace"; bad=1; }
  verdict "GW-04" $bad "large msgs PC->B 1KB..4MB exact/reliable (gw04_sub.log)"
}

s_gw05() {
  # A single B-side participant publishes and subscribes on its run-unique
  # topic while
  # the gateway bridges both sides. Every locally originated sample must be
  # observed exactly once; an echoed forwarding loop would appear as a second
  # callback for the same unique payload.
  gw_reset || return 1
  rbg "$BOARD_B" \
    "python3.12 $DEVICE_DIR/mdds_e2e/gw_dsoftbus_probe.py loop --topic $GW_TOPIC_CHATTER --count 30 --rate 1 --depth 100 --startup-delay 10 --linger 8" \
    gw05_b_loop.log || return 1
  start_gateway 120 gw05_gw.log || return 1
  local i complete=0
  for i in $(seq 1 16); do
    sleep 5
    complete=$(shell "$BOARD_B" "grep -c 'LOOP_RESULT ' '$REMOTE_LOGDIR/gw05_b_loop.log' 2>/dev/null || true" | tr -dc '0-9')
    [ -n "$complete" ] && [ "$complete" -ge 1 ] && break
  done
  gw_reset || return 1
  pull "$BOARD_B" gw05_b_loop.log || return 1
  pull "$BOARD_A" gw05_gw.log || return 1
  grep -Eq 'LOOP_RESULT PASS .*duplicates=0 .*unexpected=0' "$LOGDIR/gw05_b_loop.log" && \
    grep -Fq "bridging $GW_TOPIC_CHATTER " "$LOGDIR/gw05_gw.log" && \
    grep -Fq "$GW_TOPIC_CHATTER final: cyclone->mdds=0 mdds->cyclone=30" "$LOGDIR/gw05_gw.log" && \
    assert_rmw_dsoftbus_only_log gw05_b_loop.log && \
    assert_gateway_dsoftbus_only_log gw05_gw.log && \
    assert_gateway_m2c_healthy gw05_gw.log "$GW_TOPIC_CHATTER" 30 4 && \
    assert_dsoftbus_socket_bytes_trace gw05_b_loop.log && \
    assert_dsoftbus_socket_bytes_trace gw05_gw.log
  verdict "GW-05" $? "single-participant loop suppression: exact 30/30, real gateway mdds->cyclone=30, no echo duplicate"
}

s_gw06() {
  # Restart recovery: PC talker -> B listener flowing; kill the gateway for
  # 10 s, restart it; forwarding must resume (no storm: dup_max stays 1).
  gw_reset || return 1
  rbg "$BOARD_B" "\$ROS2_LISTENER --ros-args -r chatter:=$GW_TOPIC_CHATTER" gw06_b_listener.log || return 1
  start_gateway 200 gw06_gw1.log || return 1
  sleep 6
  pc_start gw_pc_talker.bat gw06_pc_talker.log "$GW_TOPIC_CHATTER" || return 1
  sleep 15
  local n1
  n1=$(shell "$BOARD_B" "grep -c 'I heard' '$REMOTE_LOGDIR/gw06_b_listener.log' 2>/dev/null || true" | tr -dc '0-9')
  kill_gw || return 1
  sleep 10
  start_gateway 120 gw06_gw2.log || return 1
  sleep 25
  gw_reset || return 1
  pull "$BOARD_B" gw06_b_listener.log || return 1
  pull "$BOARD_A" gw06_gw1.log || return 1
  pull "$BOARD_A" gw06_gw2.log || return 1
  local n2 d
  n2=$(heard_count gw06_b_listener.log)
  d=$(dup_max gw06_b_listener.log)
  [ "${n1:-0}" -ge 5 ] && [ "$n2" -ge $((n1 + 5)) ] && [ "$d" -le 1 ] && \
    assert_rmw_dsoftbus_only_log gw06_b_listener.log && \
    assert_gateway_dsoftbus_only_log gw06_gw1.log && \
    assert_gateway_dsoftbus_only_log gw06_gw2.log && \
    assert_gateway_c2m_healthy gw06_gw1.log "$GW_TOPIC_CHATTER" 5 && \
    assert_gateway_c2m_healthy gw06_gw2.log "$GW_TOPIC_CHATTER" 5 && \
    assert_dsoftbus_socket_bytes_trace gw06_b_listener.log && \
    assert_dsoftbus_socket_bytes_trace gw06_gw1.log && \
    assert_dsoftbus_socket_bytes_trace gw06_gw2.log
  verdict "GW-06" $? "restart recovery: heard before=${n1:-0} after=$n2 dup_max=$d"
}

s_gw07() {
  # Application-level interop: the REAL PC ros2 CLI (ros2.exe topic echo
  # --once) must receive a run-unique sample through the gateway. The bat
  # redirects to %PC_WS%\gw07_echo.log (piping the CLI through grep/head
  # deadlocks on python's block-buffered stdout — observed hang).
  gw_reset || return 1
  local gw07_echo_win
  gw07_echo_win=$(cygpath -aw "$LOGDIR/gw07_pc_echo.log")
  export GW07_ECHO_LOG="$gw07_echo_win"
  rbg "$BOARD_B" "\$ROS2_TALKER --ros-args -r chatter:=$GW_TOPIC_CHATTER" gw07_b_talker.log || return 1
  start_gateway 120 gw07_gw.log || return 1
  sleep 6
  pc_start gw_pc_ros2_echo.bat gw07_pc_echo_outer.log "$GW_TOPIC_CHATTER" || { unset GW07_ECHO_LOG; return 1; }
  unset GW07_ECHO_LOG
  local i
  # the bat retries echo up to 6x (SEDP type-resolution race); wait for a real
  # data line, not just any output (attempt 1 may log only the warning)
  for i in $(seq 1 18); do sleep 10; grep -q "data:" "$LOGDIR/gw07_pc_echo.log" 2>/dev/null && break; done
  sleep 2
  gw_reset || return 1
  pull "$BOARD_B" gw07_b_talker.log || return 1
  pull "$BOARD_A" gw07_gw.log || return 1
  grep -q "data: 'Hello World:" "$LOGDIR/gw07_pc_echo.log" 2>/dev/null && \
    assert_rmw_dsoftbus_only_log gw07_b_talker.log && \
    assert_gateway_dsoftbus_only_log gw07_gw.log && \
    assert_gateway_m2c_healthy gw07_gw.log "$GW_TOPIC_CHATTER" 1 1 && \
    assert_dsoftbus_socket_bytes_trace gw07_b_talker.log && \
    assert_dsoftbus_socket_bytes_trace gw07_gw.log
  verdict "GW-07" $? "PC ros2 CLI topic echo --once /chatter via gateway"
}

s_gw08() {
  # Ten deliberately identical payloads are not a forwarding loop. With
  # dedup_mode=off for this single-gateway topology, PC must receive exactly
  # ten copies of "constant".
  gw_reset || return 1
  pc_start gw_pc_listener.bat gw08_pc_listener.log "$GW_TOPIC_CHATTER" || return 1
  start_gateway 120 gw08_gw.log || return 1
  sleep 6
  rbg "$BOARD_B" \
    "python3.12 $DEVICE_DIR/mdds_e2e/publish_constant.py --topic $GW_TOPIC_CHATTER --payload constant --count 10 --rate 2" \
    gw08_b_constant_pub.log || return 1
  sleep 18
  gw_reset || return 1
  pull "$BOARD_B" gw08_b_constant_pub.log || return 1
  pull "$BOARD_A" gw08_gw.log || return 1
  local exact total
  exact=$(grep -h 'I heard: \[constant\]' "$LOGDIR/gw08_pc_listener.log" "$LOGDIR/gw08_pc_listener.log.err" 2>/dev/null | wc -l | tr -d ' ')
  total=$(grep -h 'I heard:' "$LOGDIR/gw08_pc_listener.log" "$LOGDIR/gw08_pc_listener.log.err" 2>/dev/null | wc -l | tr -d ' ')
  [ "$exact" -eq 10 ] && [ "$total" -eq 10 ] && \
    assert_rmw_dsoftbus_only_log gw08_b_constant_pub.log && \
    assert_gateway_dsoftbus_only_log gw08_gw.log && \
    assert_gateway_m2c_healthy gw08_gw.log "$GW_TOPIC_CHATTER" 10 2 && \
    assert_dsoftbus_socket_bytes_trace gw08_b_constant_pub.log && \
    assert_dsoftbus_socket_bytes_trace gw08_gw.log
  verdict "GW-08" $? "identical String(constant): received=$exact/10 total=$total"
}

s_gw_pc_cleanup_probe() {
  # Test-only local PC lifecycle probe.  pc_start obtains a real durable guard
  # record, the forced mismatch retains it as PENDING, and the recovery path
  # must stop that exact PID:start.  The guard persists a direct cmd.exe
  # PID:start proof after IsProcessInJob succeeds; its disappearance proves
  # that KILL_ON_JOB_CLOSE closed the contained child tree as well.
  gw_reset || return 1
  local bad=0 proof_file proof_tag negative_record negative_tag=pc_negative_no_job_marker
  if [ "$FORCE_PC_POST_RECORD_MISMATCH" != 1 ]; then
    echo "   PC cleanup probe requires MDDS_TEST_FORCE_PC_POST_RECORD_MISMATCH=1" >&2
    bad=1
  elif pc_start gw_pc_talker.bat gw_pc_cleanup_probe.log "$GW_TOPIC_CHATTER"; then
    echo "   PC cleanup probe did not force the expected post-record mismatch" >&2
    bad=1
    gw_reset || bad=1
  fi
  grep -q 'injected=force-post-record-mismatch' "$LOGDIR/pc_launch_faults.txt" 2>/dev/null || bad=1
  proof_file=$(sed -n 's/.* job_proof=\([^ ]*\) tag=.*/\1/p' "$LOGDIR/pc_launch_faults.txt" 2>/dev/null | tail -1)
  proof_tag=$(sed -n 's/.* tag=\([^ ]*\) injected=.*/\1/p' "$LOGDIR/pc_launch_faults.txt" 2>/dev/null | tail -1)
  if ! [[ -n "$proof_file" && -n "$proof_tag" ]]; then
    bad=1
  elif ! pc_assert_job_proof_child_gone "$proof_file" "$proof_tag"; then
    bad=1
  fi
  grep -Eq 'PENDING_PC_RECORD_(STOPPED_JOB_CLOSED|GONE_JOB_CLOSED)' "$LOGDIR/pending_cleanup_records.txt" 2>/dev/null || bad=1
  if grep -Fq 'Cannot overwrite variable PID' "$LOGDIR/pending_cleanup_records.txt" 2>/dev/null; then
    bad=1
  fi
  # Negative admission test: an old six-field record has no containment
  # contract.  Cleanup must reject it before considering the fake PID; it must
  # never silently fall back to taskkill /T.
  negative_record="$LOGDIR/.pc_record_negative_no_job_marker"
  printf 'MDDS_PC_RECORD RUN_ID=%s NONCE=%s TAG=%s PID=1 START=1\n' \
    "$RUN_ID" "$RUN_NONCE" "$negative_tag" > "$negative_record"
  if pc_stop_recorded "$negative_record" "$negative_tag"; then
    echo "   ERROR: missing Job Object marker was accepted for cleanup" >&2
    bad=1
  fi
  grep -Fq "local_record=$negative_record tag=$negative_tag result=PC_RECORD_INVALID" \
    "$LOGDIR/pc_cleanup_records.txt" 2>/dev/null || bad=1
  gw_reset || bad=1
  verdict "GW-PC-CLEANUP" "$bad" "Job Object child cleanup and missing-marker rejection exercised"
}

# --- main --------------------------------------------------------------------

if [ $# -eq 0 ]; then
  set -- gw01 gw02 gw03 gw04 gw05 gw06 gw07 gw08
elif [ "$1" = all ]; then
  set -- gw01 gw02 gw03 gw04 gw05 gw06 gw07 gw08
fi

# Cleanup targets only identity-fenced records.  A failed cleanup is surfaced
# and the record is retained; INT/TERM never continue into a later scenario.
on_gw_exit() {
  local rc=$? cleanup_ok=1
  trap - EXIT
  trap '' INT TERM HUP
  if ! gw_reset; then
    echo "ERROR: gateway gate cleanup was incomplete; retained identity records prevent an unsafe kill" >&2
    cleanup_ok=0
    rc=1
  fi
  if (( cleanup_ok == 1 )); then
    if ! release_activity_locks; then
      echo "ERROR: gateway activity-lock cleanup was incomplete; a fail-closed lock remains" >&2
      rc=1
    fi
  else
    echo "ERROR: retaining gateway activity locks because owned payload stop was not proven" >&2
  fi
  exit "$rc"
}

on_gw_signal() {
  local signal_rc="$1" cleanup_ok=1
  trap - EXIT
  trap '' INT TERM HUP
  if ! gw_reset; then
    echo "ERROR: cleanup after signal was incomplete; records were retained" >&2
    cleanup_ok=0
  fi
  if (( cleanup_ok == 1 )); then
    echo "ERROR: retaining gateway activity locks after signal despite successful cleanup; explicit operator recovery is required" >&2
  else
    echo "ERROR: retaining gateway activity locks after signal because owned payload stop was not proven" >&2
  fi
  exit "$signal_rc"
}

trap on_gw_exit EXIT
trap 'on_gw_signal 130' INT
trap 'on_gw_signal 143' TERM
trap 'on_gw_signal 129' HUP

if ! acquire_activity_locks; then
  echo "ERROR: gateway gate did not start because the shared MDDS activity lock is unavailable" >&2
  exit 1
fi
if ! push_gw_files; then
  echo "ERROR: failed to deploy gateway test helpers" >&2
  exit 1
fi
if ! verify_final_artifacts; then
  echo "ERROR: final-artifact hash verification failed (see $LOGDIR/artifact_hashes.txt)" >&2
  exit 1
fi
for sc in "$@"; do
  echo "== scenario: $sc =="
  if ! declare -F "s_$sc" >/dev/null; then
    verdict "GW-UNKNOWN-$sc" 1 "unknown scenario"
    continue
  fi
  if ! "s_$sc"; then
    verdict "$(echo "$sc" | tr '[:lower:]' '[:upper:]')" 1 "orchestration error"
    if ! gw_reset; then
      echo "ERROR: aborting later gateway scenarios because owned-process cleanup is incomplete" >&2
      break
    fi
  fi
done

if ! gw_reset; then
  verdict "GW-CLEANUP" 1 "owned-process cleanup incomplete (see retained identity errors)"
fi
echo
echo "== mdds gateway summary: $pass passed, $fail failed =="
[ ${#failed_ids[@]} -eq 0 ] || printf '   FAIL %s\n' "${failed_ids[@]}"
[ "$fail" -eq 0 ]
