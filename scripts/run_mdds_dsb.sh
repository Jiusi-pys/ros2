#!/usr/bin/env bash
# mdds DSoftBus-only gate (round-4 Tier-3): proves the MDDS data plane formally
# and auditably runs over DSoftBus Socket/Bytes — not UDP, and with NO silent
# fallback to UDP when DSoftBus fails. (CycloneDDS on board A still uses its
# own UDP toward the Windows PC; what is forbidden here is the MDDS backend
# using UDP.)
#
#   DS-01  board A -> board B, dsoftbus-only, exact N/N
#   DS-02  board B -> board A, dsoftbus-only, exact N/N; board B additionally
#          runs ROS_AUTOMATIC_DISCOVERY_RANGE=SYSTEM_DEFAULT
#   DS-03  board B -> mdds_gateway on A (mdds_transport=dsoftbus) -> Windows PC
#          (CycloneDDS test domain 46), exact N/N
#   DS-03-ACK-BURST  directed P1 regression: 32 high-rate, post-match board-B
#          samples through the same isolated gateway path, completing >=4 ACK
#          fences.  It does not infer the size of each completed fence solely
#          from the fence-count metric.
#   DS-04  negative evidence while dsoftbus nodes run: neither board holds any
#          socket in the mdds UDP port band (47811-47842 = 0xBAC3-0xBAE2), and
#          the process logs name dsoftbus as the active backend
#   DS-05  fault injection: with the domain's dsoftbus session already held,
#          a dsoftbus-only gateway on the same board MUST exit non-zero with
#          the requested/active/failed report + raw DSoftBus error code — even
#          though UDP is available; plus: an unknown MDDS_TRANSPORT value must
#          be rejected fail-closed
#   DS-06  call-chain evidence: MDDS_DEBUG=1 logs from DS-01/02/03 contain the
#          real DSoftBus calls Socket/Listen/BindAsync/OnBind/SendBytes/OnBytes
#   DS-07  board A -> board B DSoftBus-only fragmentation sweep: 1 KiB through
#          8 MiB, exact per-size count with lost/reorder/crc all zero
#   DS-08  launcher pending-cleanup fault injection: a pre-exec record-write
#          failure must never run a payload, and a valid remote PID:start
#          record hidden from every launch read must still be identity-fenced
#          and stopped during pending cleanup
#
# Cleanup kills ONLY the exact PIDs recorded by this run (boards: a persistent
# run-scoped pid:start record written before the best-effort REMOTE_PID token;
# PC: an owned cmd.exe PID and its child tree). No pkill -f, command-line PID
# scanning, or taskkill /IM.
#
#   ./scripts/run_mdds_dsb.sh [ds01 ... ds08 | ds03_ack_burst | all]
#
# `all` includes DS-03-ACK-BURST and DS-08.  The ACK-burst remains a directed
# regression rather than one of the normative DS-01..08 identifiers, but a
# final "all" run may not silently omit either its ACK-fence proof or the
# pending-cleanup fault boundary.
set -uo pipefail
cd "$(dirname "$0")/.."
source "$PWD/scripts/lib/mdds_msys_env.sh" || {
  echo "ERROR: cannot load Git-Bash environment conversion helper" >&2
  exit 2
}

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00
BOARD_B=3e01ff55454d202020104433991c3b00
DEVICE_DIR=/data/local/tmp/ros2
LOGROOT="${MDDS_DSB_LOGROOT:-ohos_test_logs/mdds_dsb}"
# Relative roots are component-safe; an absolute root is permitted only in the
# Git-Bash Windows-drive form (/c/... or /d/...).  Validate before mkdir,
# pending-record bookkeeping, or any PowerShell/HDC invocation.
SAFE_LOGROOT_RE='^([A-Za-z0-9][A-Za-z0-9._-]*)(/[A-Za-z0-9][A-Za-z0-9._-]*)*$|^/[A-Za-z]/[A-Za-z0-9][A-Za-z0-9._-]*(/[A-Za-z0-9][A-Za-z0-9._-]*)*$'
if [[ ! "$LOGROOT" =~ $SAFE_LOGROOT_RE ]]; then
  echo "ERROR: MDDS_DSB_LOGROOT must be a safe relative path or /c/... path (no whitespace, controls, colon, or . / .. components)" >&2
  exit 2
fi
PC_WS=/c/pixi_ws
PC_BAT_DIR="$(cygpath -w "$PWD/scripts/mdds_e2e/pc")"
RUN_ID="${MDDS_RUN_ID:-dsb_$(date +%Y%m%d_%H%M%S)_$RANDOM}"
LOGDIR="$LOGROOT/$RUN_ID"
REMOTE_LOGDIR="$DEVICE_DIR/mdds_dsb_runs/$RUN_ID"
case "$RUN_ID" in
  ''|*[!A-Za-z0-9_-]*) echo "ERROR: MDDS_RUN_ID must use only A-Z a-z 0-9 _ -" >&2; exit 2 ;;
esac
RUN_NONCE="${MDDS_RUN_NONCE:-n${RANDOM}p${RANDOM}x$$}"
case "$RUN_NONCE" in
  ''|*[!A-Za-z0-9_-]*) echo "ERROR: MDDS_RUN_NONCE must use only A-Z a-z 0-9 _ -" >&2; exit 2 ;;
esac
# DS-03 traffic is intentionally bound to this one invocation.  The source
# fields above are constrained to the topic-safe alphabet, so they can be
# rendered into the gateway config and passed through the PowerShell launcher
# without introducing a shell/configuration injection surface.
DS3_TOPIC="/mdds_dsb_sweep_${RUN_ID}_${RUN_NONCE}"
if (( ${#DS3_TOPIC} > 200 )); then
  echo "ERROR: MDDS_RUN_ID + MDDS_RUN_NONCE makes the DS-03 topic too long" >&2
  exit 2
fi
REMOTE_OWNER="$REMOTE_LOGDIR/.mdds_run_owner"
# PowerShell guards inherit only exported state. Keep the shell-local source of
# truth above, then export the already validated copies used in their signed
# intent/record/cancel strings.
export MDDS_RUN_ID="$RUN_ID"
export MDDS_RUN_NONCE="$RUN_NONCE"
mkdir -p "$LOGDIR" || { echo "ERROR: cannot create log directory $LOGDIR" >&2; exit 2; }
DSB_CONFIG_TEMPLATE="scripts/mdds_e2e/mdds_gateway_dsb.conf"
DSB_CONFIG_LOCAL="$LOGDIR/mdds_gateway_dsb.rendered.conf"
printf 'RUN_ID=%s\nRUN_NONCE=%s\nDS3_TOPIC=%s\n' "$RUN_ID" "$RUN_NONCE" "$DS3_TOPIC" \
  > "$LOGDIR/ds03_topic_binding.txt" || { echo "ERROR: cannot record DS-03 topic binding" >&2; exit 2; }
RUN_MARKER="$LOGDIR/.dsb_run_$$_${RANDOM}.marker"
touch "$RUN_MARKER" || { echo "ERROR: cannot create run marker $RUN_MARKER" >&2; exit 2; }

# Test-only launcher fault injection.  It simulates the known HDC failure mode
# in which the remote command successfully launches a process but its stdout
# PID token is lost before reaching this host.  The persistent remote record
# must recover the identity in that case.  Refuse any value other than 0/1 so
# it cannot alter the remote shell command below.
SUPPRESS_LAUNCH_TOKEN="${MDDS_TEST_SUPPRESS_LAUNCH_TOKEN:-0}"
case "$SUPPRESS_LAUNCH_TOKEN" in
  0|1) ;;
  *) echo "ERROR: MDDS_TEST_SUPPRESS_LAUNCH_TOKEN must be 0 or 1" >&2; exit 2 ;;
esac
export MDDS_TEST_SUPPRESS_LAUNCH_TOKEN="$SUPPRESS_LAUNCH_TOKEN"

# These two fault injectors exercise the two launch-proof failure boundaries:
# a lost first record read must retry, while a failed record write must prove
# that the guard never reaches its payload.  They are deliberately accepted
# only as booleans and never interpolate into a board command.
DROP_FIRST_RECORD_READ="${MDDS_TEST_DROP_FIRST_RECORD_READ:-0}"
# This test-only hook proves pending cleanup can recover a board-side valid
# PID:start record even when every host-side record read is lost.  It is never
# interpolated as a shell fragment and defaults off for all normal gates.
DROP_ALL_RECORD_READS="${MDDS_TEST_DROP_ALL_RECORD_READS:-0}"
FAIL_LAUNCH_RECORD_WRITE="${MDDS_TEST_FAIL_LAUNCH_RECORD_WRITE:-0}"
for _launch_test_flag in "$DROP_FIRST_RECORD_READ" "$DROP_ALL_RECORD_READS" "$FAIL_LAUNCH_RECORD_WRITE"; do
  case "$_launch_test_flag" in
    0|1) ;;
    *) echo "ERROR: MDDS_TEST_DROP_FIRST_RECORD_READ, MDDS_TEST_DROP_ALL_RECORD_READS, and MDDS_TEST_FAIL_LAUNCH_RECORD_WRITE must be 0 or 1" >&2; exit 2 ;;
  esac
done
export MDDS_TEST_DROP_FIRST_RECORD_READ="$DROP_FIRST_RECORD_READ"
export MDDS_TEST_DROP_ALL_RECORD_READS="$DROP_ALL_RECORD_READS"
export MDDS_TEST_FAIL_LAUNCH_RECORD_WRITE="$FAIL_LAUNCH_RECORD_WRITE"

export MSYS2_ARG_CONV_EXCL='*'

# mdds UDP port band (mdds_udp_base_port=47811 .. +32): hex 0xBAC3-0xBAE2.
UDP_BAND_RE='BA(C[3-9A-F]|D[0-9A-F]|E[0-2])'

# Source the installed rmw_mdds profile for every ROS-side test node. It
# selects DSoftBus only, rejects a legacy MDDS_TRANSPORT override, requires
# SYSTEM_DEFAULT discovery and makes a DSoftBus start failure fatal. Keeping
# this as the installed profile (rather than reproducing its exports here)
# makes DS-01/02/04/07 prove the deployable production configuration.
# MDDS_DEBUG=1 makes the transport log the real DSoftBus call chain
# (DS-06 evidence).
# ROS_DOMAIN_ID=43 (=> dsoftbus session com.kaihong.mdds.d43) keeps this gate
# clear of board A's pre-existing round-3 `ros2 topic echo` (PID 22646), which
# squat on domain 0's session com.kaihong.mdds.d0 and is NOT ours to kill.
# Domain 42 is additionally avoided: this gate's first runs latched the DSoftBus
# bind DDoS protection on the d42 tuple (10 failed opens/60s => 600s deny);
# libmdds now paces rebinds (kBindRetryMs/kBindDeniedBackoffMs) so a fresh
# tuple stays clear.
RENVS=". $DEVICE_DIR/env.sh; export RMW_IMPLEMENTATION=rmw_mdds;"
DSB_ENVS="$RENVS . $DEVICE_DIR/share/rmw_mdds/config/ohos_dsoftbus.env; export MDDS_DEBUG=1; export ROS_DOMAIN_ID=43;"
# DS-02 pins SYSTEM_DEFAULT on board B explicitly per the gate spec (already
# in DSB_ENVS; kept separate so the requirement is visible at the call site).
DSB_ENVS_B="$DSB_ENVS"
# The DS-03 readiness gate polls the gateway's health line over HDC.  rcutils
# otherwise block-buffers INFO output under nohup redirection, which can make a
# stale `pub_subs=1` appear only after the PC's volatile reader has timed out.
# Force just this test process's ROS logs unbuffered so the gate observes the
# current Cyclone match, not a delayed file flush.
GWENVS=". $DEVICE_DIR/env.sh; unset RMW_IMPLEMENTATION; export CYCLONEDDS_URI=$DEVICE_DIR/mdds_e2e/cyclonedds_board_a.xml; export MDDS_DEBUG=1; export RCUTILS_LOGGING_BUFFERED_STREAM=0;"

# The deployed KaihongOS /bin/sh image has no external `tr`, yet the durable
# launch-control protocol needs only LF-canonical board files.  Provide a
# deliberately narrow shell-function fallback for the two existing calls
# (`tr -d '\\r'` and `tr -d '\\r\\n'`) rather than treating a missing utility
# as an empty launch record.  The function rejects every other form; host-side
# parsing continues to use the real Git-Bash tr.  Board control files are
# written with printf '\n', so passing their stream through cat is equivalent
# for the supported forms and preserves fail-closed parsing if a CR appears.
shell() {
  local board="$1" command="$2"
  "$HDC" -t "$board" shell "tr() { case \"\$1:\$2\" in '-d:\\r'|'-d:\\r\\n') cat ;; *) return 127 ;; esac; }; $command" </dev/null
}

ACTIVITY_LOCK_DIR="$DEVICE_DIR/.mdds-activity-lock"
declare -a ACTIVITY_LOCKED_BOARDS=()

activity_lock_owner() {
  printf 'MDDS_ACTIVITY_LOCK MODE=TEST RUN_ID=%s NONCE=%s OWNER=run_mdds_dsb\n' "$RUN_ID" "$RUN_NONCE"
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

pull() { # pull <board> <device-log>; prove the board log belongs to this run
  local board="$1" name="$2" target="$LOGDIR/$2" temp="$LOGDIR/.${2}.$$.${RANDOM}.tmp"
  if ! shell "$board" \
    "if test -f '$REMOTE_LOGDIR/$name' && grep -Fqx 'DSB_RUN_ID=$RUN_ID' '$REMOTE_LOGDIR/$name'; then echo DSB_LOG_BEGIN; cat '$REMOTE_LOGDIR/$name'; else echo DSB_LOG_MISSING; fi" \
    > "$temp" 2>/dev/null; then
    echo "   ERROR: cannot pull $name from ${board:0:8}" >&2
    rm -f "$temp"
    return 1
  fi
  if ! grep -Fqx 'DSB_LOG_BEGIN' "$temp"; then
    echo "   ERROR: missing fresh log $name from ${board:0:8}" >&2
    rm -f "$temp"
    return 1
  fi
  sed '/^DSB_LOG_BEGIN$/d;/^DSB_RUN_ID=/d' "$temp" > "$temp.clean"
  mv -f "$temp.clean" "$temp"
  if [ ! -s "$temp" ]; then
    echo "   ERROR: fresh log has no process output: $name from ${board:0:8}" >&2
    rm -f "$temp"
    return 1
  fi
  if ! mv -f "$temp" "$target"; then
    echo "   ERROR: cannot replace local log $target" >&2
    rm -f "$temp"
    return 1
  fi
}

require_current_log() { # require_current_log <local-log>
  if [ ! -s "$1" ] || [ ! "$1" -nt "$RUN_MARKER" ]; then
    echo "   missing or stale current-run log: $1" >&2
    return 1
  fi
}

scan_udp_band() { # <board> <serial-suffix> <base|now>; record an explicit scan sentinel
  local board="$1" tag="$2" phase="$3" target="$LOGDIR/ds04_udp_$3_$2.txt"
  if ! shell "$board" \
    "if test -r /proc/net/udp && test -r /proc/net/udp6; then { cat /proc/net/udp /proc/net/udp6 2>/dev/null | grep -E ':$UDP_BAND_RE' | sort || true; }; echo UDP_SCAN_OK; else echo UDP_SCAN_ERROR >&2; exit 2; fi" \
    > "$target" 2>"$target.err"; then
    echo "   ERROR: UDP socket scan failed on board $tag ($phase): $(head -1 "$target.err")" >&2
    return 1
  fi
  if ! grep -Fqx 'UDP_SCAN_OK' "$target"; then
    echo "   ERROR: UDP socket scan has no success sentinel on board $tag ($phase)" >&2
    return 1
  fi
}

# --- owned-process tracking ---------------------------------------------------
# A PID alone is not an ownership proof: it can be recycled between launch and
# cleanup. Every board record also retains /proc/<pid>/stat field 22.
TRACKED=""     # space-separated board:pid:proc-start records launched by this run
# A transaction is pending from the instant its signed intent is persistent
# until its exact PID/start record is added to TRACKED.  The EXIT trap therefore
# never has a gap where a launched process has no recoverable identity.
PENDING_LAUNCH_RECORDS="" # space-separated board:remote-record paths
PENDING_PC_RECORD=""      # host-local Windows guard record path, if pending
REMOTE_OWNERS=""          # board ids whose run owner nonce was verified
LAST_PID=""
PC_PID=""
PC_START_TICKS=""  # Windows FILETIME UTC; fences PID reuse during cleanup
PC_RECORD_FILE=""
REMOTE_RECORD_READ_ATTEMPTS=0
PC_RECORD_READ_ATTEMPTS=0
# The normal transcript paths remain stable for existing gates.  DS-08 replaces
# them temporarily with fresh, run-local transcripts so its injected evidence
# cannot be satisfied by an earlier scenario's appended line.
LAUNCH_FAULT_LOG="$LOGDIR/launch_fault_injection.txt"
PENDING_CLEANUP_LOG="$LOGDIR/pending_cleanup_records.txt"
# Non-empty only after DS-08 has created its assertion transcript.  The EXIT
# handler appends the final activity-lock release outcome to this same evidence.
DS08_ASSERTIONS_LOG=""

parse_pid_token() { # stdin -> one REMOTE_PID=pid:start token, if any
  tr -d '\r' | sed -n 's/.*REMOTE_PID=\([0-9][0-9]*:[0-9][0-9]*\).*/\1/p' | head -1
}

parse_pid_record() { # stdin -> one exact pid:start record, if any
  tr -d '\r' | sed -n "s/^MDDS_LAUNCH_RECORD RUN_ID=$RUN_ID NONCE=$RUN_NONCE PID=\\([0-9][0-9]*\\) START=\\([0-9][0-9]*\\)$/\\1:\\2/p"
}

parse_pc_pid_record() { # stdin -> one exact Windows guard pid:start record, if any
  tr -d '\r' | sed -n "s/^MDDS_PC_LAUNCH_RECORD RUN_ID=$RUN_ID NONCE=$RUN_NONCE PID=\\([0-9][0-9]*\\) START=\\([0-9][0-9]*\\)$/\\1:\\2/p"
}

remote_launch_status() { # <board> <record-path> -> safe pre-exec terminal state, if any
  local raw
  raw=$(shell "$1" "if test -f '$2.status'; then tr -d '\\r\\n' < '$2.status'; fi" 2>/dev/null || true)
  case "$(printf '%s' "$raw" | tr -d '\r\n')" in
    "MDDS_LAUNCH_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE STATE=CANCELLED_PREEXEC")
      printf '%s' CANCELLED_PREEXEC ;;
    "MDDS_LAUNCH_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE STATE=RECORD_WRITE_FAILED")
      printf '%s' RECORD_WRITE_FAILED ;;
    "MDDS_LAUNCH_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE STATE=INTENT_INVALID")
      printf '%s' INTENT_INVALID ;;
  esac
}

read_remote_pid_record() { # <board> <record-path>; print verified pid:start after bounded retries
  local board="$1" record_path="$2" attempt raw pair
  for attempt in 1 2 3; do
    REMOTE_RECORD_READ_ATTEMPTS=$((REMOTE_RECORD_READ_ATTEMPTS + 1))
    if [ "$DROP_ALL_RECORD_READS" = 1 ]; then
      printf 'event=inject-drop-all-remote-record-reads board=%s record=%s attempt=%s\n' \
        "$board" "$record_path" "$attempt" >> "$LAUNCH_FAULT_LOG"
      sleep 1
      continue
    fi
    if [ "$DROP_FIRST_RECORD_READ" = 1 ] && [ "$REMOTE_RECORD_READ_ATTEMPTS" -eq 1 ]; then
      printf 'event=inject-drop-first-remote-record-read board=%s record=%s\n' "$board" "$record_path" \
        >> "$LAUNCH_FAULT_LOG"
      sleep 1
      continue
    fi
    raw=$(shell "$board" "if test -f '$record_path'; then cat '$record_path'; fi" 2>/dev/null || true)
    pair=$(printf '%s' "$raw" | parse_pid_record | head -1)
    if [[ "$pair" =~ ^[0-9]+:[0-9]+$ ]]; then
      printf '%s' "$pair"
      return 0
    fi
    sleep 1
  done
  return 1
}

ensure_remote_owner() { # <board>: atomically claim or verify RUN_ID+nonce
  local board="$1" known out
  for known in $REMOTE_OWNERS; do
    [ "$known" = "$board" ] && return 0
  done
  out=$(shell "$board" "mkdir -p '$REMOTE_LOGDIR/launch'; if test -f '$REMOTE_OWNER'; then if grep -Fqx 'RUN_ID=$RUN_ID' '$REMOTE_OWNER' && grep -Fqx 'NONCE=$RUN_NONCE' '$REMOTE_OWNER'; then echo OWNER_OK; else echo OWNER_CONFLICT; fi; else ( set -C; printf 'RUN_ID=%s\\nNONCE=%s\\n' '$RUN_ID' '$RUN_NONCE' > '$REMOTE_OWNER' ) 2>/dev/null && { if grep -Fqx 'RUN_ID=$RUN_ID' '$REMOTE_OWNER' && grep -Fqx 'NONCE=$RUN_NONCE' '$REMOTE_OWNER'; then echo OWNER_CREATED; else echo OWNER_CONFLICT; fi; } || echo OWNER_CONFLICT; fi" || true)
  case "$out" in
    *OWNER_OK*|*OWNER_CREATED*)
      REMOTE_OWNERS="$REMOTE_OWNERS $board"
      return 0 ;;
    *)
      echo "   ERROR: remote run owner conflict/unreadable for $board (RUN_ID=$RUN_ID)" >&2
      return 1 ;;
  esac
}

prepare_remote_launch_intent() { # <board> <record-path>; persistent intent exists before HDC starts a guard
  local board="$1" record_path="$2" intent_path="$2.intent" cancel_path="$2.cancel" status_path="$2.status" out
  ensure_remote_owner "$board" || return 1
  out=$(shell "$board" "if test -e '$record_path' || test -e '$intent_path' || test -e '$cancel_path' || test -e '$status_path'; then echo LAUNCH_INTENT_CONFLICT; else ( set -C; printf 'MDDS_LAUNCH_INTENT RUN_ID=%s NONCE=%s\\n' '$RUN_ID' '$RUN_NONCE' > '$intent_path' ) 2>/dev/null && { if grep -Fqx 'MDDS_LAUNCH_INTENT RUN_ID=$RUN_ID NONCE=$RUN_NONCE' '$intent_path'; then echo LAUNCH_INTENT_READY; else echo LAUNCH_INTENT_INVALID; fi; } || echo LAUNCH_INTENT_CONFLICT; fi" || true)
  if [[ "$out" != *LAUNCH_INTENT_READY* ]]; then
    echo "   ERROR: unable to establish exclusive remote launch intent for $board:$record_path" >&2
    return 1
  fi
}

add_pending_launch_record() { # <board> <remote-record-path>
  PENDING_LAUNCH_RECORDS="$PENDING_LAUNCH_RECORDS $1:$2"
}

remove_pending_launch_record() { # <exact board:remote-record-path>
  local wanted="$1" pair kept=""
  for pair in $PENDING_LAUNCH_RECORDS; do
    [ "$pair" = "$wanted" ] || kept="$kept $pair"
  done
  PENDING_LAUNCH_RECORDS="$kept"
}

launch() { # launch <board> <env-prefix> <cmd> <log>; remote pid -> $LAST_PID
  LAST_PID=""
  local out token record pid start record_path cancel_path status_path intent_path record_source pending terminal
  if [[ ! "$4" =~ ^[A-Za-z0-9._-]+$ || "$3" == *"'"* || "$3" == *'"'* ]]; then
    echo "   ERROR: refusing unsafe launch arguments for [$3]" >&2
    return 1
  fi
  # The nonce is part of the identity *and* path, so an old process with a
  # reused MDDS_RUN_ID cannot be adopted or signalled by this invocation.
  record_path="$REMOTE_LOGDIR/launch/$4.$RUN_NONCE.pid"
  cancel_path="$record_path.cancel"
  status_path="$record_path.status"
  intent_path="$record_path.intent"
  prepare_remote_launch_intent "$1" "$record_path" || return 1
  pending="$1:$record_path"
  add_pending_launch_record "$1" "$record_path"
  # The guard never removes intent/cancel files. It validates the signed
  # intent, writes its full run+nonce+PID+start record before exec, and records
  # a signed terminal state whenever it exits before a payload is possible.
  out=$(shell "$1" "printf 'DSB_RUN_ID=%s\\n' '$RUN_ID' > '$REMOTE_LOGDIR/$4'; $2 nohup sh -c 'record_path=\$1; cancel_path=\$2; status_path=\$3; intent_path=\$4; log_path=\$5; payload=\$6; run_id=\$7; nonce=\$8; fail_write=\$9; intent=\"MDDS_LAUNCH_INTENT RUN_ID=\$run_id NONCE=\$nonce\"; cancel=\"MDDS_LAUNCH_CANCEL RUN_ID=\$run_id NONCE=\$nonce\"; status() { printf \"MDDS_LAUNCH_STATUS RUN_ID=%s NONCE=%s STATE=%s\\n\" \"\$run_id\" \"\$nonce\" \"\$1\" > \"\$status_path\"; }; if ! grep -Fqx \"\$intent\" \"\$intent_path\" 2>/dev/null; then status INTENT_INVALID; exit 0; fi; if test -f \"\$cancel_path\" && grep -Fqx \"\$cancel\" \"\$cancel_path\"; then status CANCELLED_PREEXEC; exit 0; fi; pid=\$\$; start=\$(cut -d \" \" -f22 /proc/\$\$/stat 2>/dev/null); case \"\$start\" in \"\"|*[!0-9]*) status RECORD_WRITE_FAILED; exit 0 ;; esac; record=\"MDDS_LAUNCH_RECORD RUN_ID=\$run_id NONCE=\$nonce PID=\$pid START=\$start\"; if test \"\$fail_write\" = 1 || ! ( set -C; printf \"%s\\n\" \"\$record\" > \"\$record_path\" ) 2>/dev/null || ! grep -Fqx \"\$record\" \"\$record_path\"; then status RECORD_WRITE_FAILED; exit 0; fi; if test -f \"\$cancel_path\" && grep -Fqx \"\$cancel\" \"\$cancel_path\"; then status CANCELLED_PREEXEC; exit 0; fi; exec sh -c \"exec \$payload\" >> \"\$log_path\" 2>&1' mdds-launch '$record_path' '$cancel_path' '$status_path' '$intent_path' '$REMOTE_LOGDIR/$4' \"$3\" '$RUN_ID' '$RUN_NONCE' '$FAIL_LAUNCH_RECORD_WRITE' </dev/null >/dev/null 2>&1 & i=0; while [ \$i -lt 8 ]; do if test -s '$record_path'; then record=\$(tr -d '\\r\\n' < '$record_path'); prefix='MDDS_LAUNCH_RECORD RUN_ID=$RUN_ID NONCE=$RUN_NONCE PID='; case \"\$record\" in \"\$prefix\"*) rest=\${record#\"\$prefix\"}; pid=\${rest%% START=*}; start=\${rest#* START=}; case \"\$pid\" in ''|*[!0-9]*) ;; *) case \"\$start\" in ''|*[!0-9]*) ;; *) if [ '$SUPPRESS_LAUNCH_TOKEN' != 1 ]; then printf 'REMOTE_PID=%s:%s\\n' \"\$pid\" \"\$start\"; fi; break ;; esac ;; esac ;; esac; fi; if test -s '$status_path'; then printf 'REMOTE_LAUNCH_STATUS=%s\\n' \"\$(tr -d '\\r\\n' < '$status_path')\"; break; fi; i=\$((i+1)); sleep 1; done")
  token=$(printf '%s' "$out" | parse_pid_token)
  record=$(read_remote_pid_record "$1" "$record_path" || true)
  record_source=remote-record
  if [[ "$token" =~ ^[0-9]+:[0-9]+$ ]]; then
    if [ -z "$record" ] || [ "$token" != "$record" ]; then
      echo "   ERROR: stdout launch token did not match the signed persistent record for [$3]" >&2
      return 1
    fi
    record_source=stdout+remote-record
  fi
  pid=$(printf '%s' "$record" | cut -d: -f1)
  start=$(printf '%s' "$record" | cut -d: -f2)
  if ! [[ "$pid" =~ ^[0-9]+$ && "$start" =~ ^[0-9]+$ ]]; then
    terminal=$(remote_launch_status "$1" "$record_path")
    echo "   ERROR: no signed persistent launch record for [$3] (terminal=${terminal:-unresolved}); hdc output: $(echo "$out" | tr -d '\r' | head -3)" >&2
    return 1
  fi
  printf 'board=%s run_id=%s nonce=%s pid=%s start=%s source=%s remote_record=%s intent=%s log=%s\n' \
    "$1" "$RUN_ID" "$RUN_NONCE" "$pid" "$start" "$record_source" "$record_path" "$intent_path" "$4" \
    >> "$LOGDIR/launch_records.txt"
  if [ "$record_source" = remote-record ]; then
    echo "   recovered launch identity from persistent remote record: $1:$pid:$start" >&2
  fi
  LAST_PID=$pid
  # Publish TRACKED before deleting PENDING. A signal in this handoff sees one
  # or both exact identities; cleanup is deliberately idempotent.
  TRACKED="$TRACKED $1:$pid:$start"
  remove_pending_launch_record "$pending"
}

cleanup_pending_launch_record() { # <board> <remote-record-path>
  local board="$1" record_path="$2" cancel_path="$2.cancel" status_path="$2.status" intent_path="$2.intent" out
  # Cancellation is persistent and signed before we inspect the PID record.
  # A missing record is never success by itself: only a guard-written,
  # signed pre-exec terminal status proves no payload can still appear.
  out=$(shell "$board" "intent='MDDS_LAUNCH_INTENT RUN_ID=$RUN_ID NONCE=$RUN_NONCE'; cancel='MDDS_LAUNCH_CANCEL RUN_ID=$RUN_ID NONCE=$RUN_NONCE'; if ! grep -Fqx \"\$intent\" '$intent_path' 2>/dev/null; then echo PENDING_INTENT_INVALID; exit 2; fi; if test -e '$cancel_path' && ! grep -Fqx \"\$cancel\" '$cancel_path'; then echo PENDING_CANCEL_CONFLICT; exit 3; fi; if ! printf '%s\\n' \"\$cancel\" > '$cancel_path' || ! grep -Fqx \"\$cancel\" '$cancel_path'; then echo PENDING_CANCEL_WRITE_FAILED; exit 4; fi; i=0; while [ \$i -lt 10 ]; do if test -f '$record_path'; then record=\$(tr -d '\\r\\n' < '$record_path' 2>/dev/null); prefix='MDDS_LAUNCH_RECORD RUN_ID=$RUN_ID NONCE=$RUN_NONCE PID='; case \"\$record\" in \"\$prefix\"*) rest=\${record#\"\$prefix\"}; pid=\${rest%% START=*}; start=\${rest#* START=} ;; *) echo PENDING_RECORD_INVALID; exit 5 ;; esac; case \"\$pid\" in ''|*[!0-9]*) echo PENDING_RECORD_INVALID; exit 5 ;; esac; case \"\$start\" in ''|*[!0-9]*) echo PENDING_RECORD_INVALID; exit 5 ;; esac; if test ! -r /proc/\$pid/stat; then echo PENDING_RECORD_GONE; exit 0; fi; current=\$(cut -d ' ' -f22 /proc/\$pid/stat); state=\$(cut -d ' ' -f3 /proc/\$pid/stat); if [ \"\$current\" != \"\$start\" ]; then echo PENDING_RECORD_REUSED; exit 6; fi; if [ \"\$state\" = Z ]; then echo PENDING_RECORD_GONE; exit 0; fi; kill \"\$pid\" 2>/dev/null || { echo PENDING_RECORD_SIGNAL_FAILED; exit 7; }; sleep 2; if test ! -r /proc/\$pid/stat || [ \"\$(cut -d ' ' -f3 /proc/\$pid/stat)\" = Z ]; then echo PENDING_RECORD_STOPPED; exit 0; fi; kill -9 \"\$pid\" 2>/dev/null || { echo PENDING_RECORD_SIGNAL_FAILED; exit 7; }; sleep 1; if test ! -r /proc/\$pid/stat || [ \"\$(cut -d ' ' -f3 /proc/\$pid/stat)\" = Z ]; then echo PENDING_RECORD_STOPPED; exit 0; fi; echo PENDING_RECORD_LIVE; exit 8; fi; if test -f '$status_path'; then status=\$(tr -d '\\r\\n' < '$status_path'); case \"\$status\" in 'MDDS_LAUNCH_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE STATE=CANCELLED_PREEXEC') echo PENDING_RECORD_CANCELLED_PREEXEC; exit 0 ;; 'MDDS_LAUNCH_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE STATE=RECORD_WRITE_FAILED') echo PENDING_RECORD_WRITE_FAILED_PREEXEC; exit 0 ;; 'MDDS_LAUNCH_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE STATE=INTENT_INVALID') echo PENDING_RECORD_INTENT_INVALID_PREEXEC; exit 0 ;; *) echo PENDING_STATUS_INVALID; exit 5 ;; esac; fi; i=\$((i+1)); sleep 1; done; echo PENDING_RECORD_UNRESOLVED; exit 9" || true)
  printf 'board=%s remote_record=%s result=%s\n' "$board" "$record_path" \
    "$(printf '%s' "$out" | tr -d '\r\n')" >> "$PENDING_CLEANUP_LOG"
  case "$out" in
    *PENDING_RECORD_GONE*|*PENDING_RECORD_STOPPED*|*PENDING_RECORD_CANCELLED_PREEXEC*|*PENDING_RECORD_WRITE_FAILED_PREEXEC*|*PENDING_RECORD_INTENT_INVALID_PREEXEC*)
      return 0 ;;
    *)
      echo "   ERROR: pending launch record could not be safely recovered: $board:$record_path" >&2
      return 1 ;;
  esac
}

cleanup_pending_launch_records() {
  local snapshot="$PENDING_LAUNCH_RECORDS" pair board record_path kept="" rc=0
  for pair in $snapshot; do
    board=$(printf '%s' "$pair" | cut -d: -f1)
    record_path=${pair#*:}
    if ! cleanup_pending_launch_record "$board" "$record_path"; then
      kept="$kept $pair"
      rc=1
    fi
  done
  PENDING_LAUNCH_RECORDS="$kept"
  return "$rc"
}

remote_record_state() { # <board> <pid> <proc-start> -> GONE|LIVE|REUSED
  local result
  result=$(shell "$1" "if test ! -r /proc/$2/stat; then echo GONE; else current=\$(cut -d ' ' -f22 /proc/$2/stat); state=\$(cut -d ' ' -f3 /proc/$2/stat); if [ \"\$current\" != '$3' ]; then echo REUSED; elif [ \"\$state\" = Z ]; then echo GONE; else echo LIVE; fi; fi" | tr -d '\r')
  case "$result" in
    *GONE*) echo GONE ;;
    *LIVE*) echo LIVE ;;
    *) echo REUSED ;;
  esac
}

remove_tracked_record() { # <exact board:pid:start>
  local wanted="$1" pair kept=""
  for pair in $TRACKED; do
    [ "$pair" = "$wanted" ] || kept="$kept $pair"
  done
  TRACKED="$kept"
}

# Stop one process that was launched by this script, without disturbing the
# other members of TRACKED.  DS-03 uses this to retire its passive type/bridge
# seed before starting the real publisher while keeping the gateway alive.
kill_recorded() { # kill_recorded <board> <pid>
  local board="$1" pid="$2" pair="" b candidate_pid start state grace=2
  [[ "$pid" =~ ^[0-9]+$ ]] || {
    echo "   ERROR: refusing non-numeric recorded pid: $pid" >&2
    return 1
  }
  for pair in $TRACKED; do
    b=$(printf '%s' "$pair" | cut -d: -f1)
    candidate_pid=$(printf '%s' "$pair" | cut -d: -f2)
    if [ "$b" = "$board" ] && [ "$candidate_pid" = "$pid" ]; then break; fi
    pair=""
  done
  if [ -z "$pair" ]; then
    echo "   ERROR: refusing to kill untracked process $board:$pid" >&2
    return 1
  fi
  start=$(printf '%s' "$pair" | cut -d: -f3)
  state=$(remote_record_state "$board" "$pid" "$start")
  case "$state" in
    GONE) remove_tracked_record "$pair"; return 0 ;;
    REUSED)
      echo "   ERROR: retained cleanup record $pair has a reused PID; refusing to kill" >&2
      return 1 ;;
  esac
  shell "$board" "kill $pid 2>/dev/null" >/dev/null || {
    echo "   ERROR: failed to signal owned process $pair" >&2
    return 1
  }
  sleep "$grace"
  state=$(remote_record_state "$board" "$pid" "$start")
  if [ "$state" = LIVE ]; then
    echo "   WARN: $pair survived SIGTERM, sending SIGKILL" >&2
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

kill_tracked() {
  local snapshot="$TRACKED" pair b pid rc=0
  for pair in $snapshot; do
    b=$(printf '%s' "$pair" | cut -d: -f1)
    pid=$(printf '%s' "$pair" | cut -d: -f2)
    kill_recorded "$b" "$pid" || rc=1
  done
  return "$rc"
}

wait_recorded_exit() { # <board> <pid> <seconds>; accepts only an already-tracked direct payload
  local board="$1" pid="$2" max_seconds="$3" pair="" b candidate_pid start state i
  for pair in $TRACKED; do
    b=$(printf '%s' "$pair" | cut -d: -f1)
    candidate_pid=$(printf '%s' "$pair" | cut -d: -f2)
    if [ "$b" = "$board" ] && [ "$candidate_pid" = "$pid" ]; then break; fi
    pair=""
  done
  if [ -z "$pair" ]; then
    echo "   ERROR: refusing to wait on untracked process $board:$pid" >&2
    return 1
  fi
  start=$(printf '%s' "$pair" | cut -d: -f3)
  for i in $(seq 1 "$max_seconds"); do
    state=$(remote_record_state "$board" "$pid" "$start")
    case "$state" in
      GONE) remove_tracked_record "$pair"; return 0 ;;
      REUSED)
        echo "   ERROR: launch record $pair was reused while waiting for its direct exit" >&2
        return 1 ;;
    esac
    sleep 1
  done
  echo "   ERROR: owned one-shot process did not exit within ${max_seconds}s: $pair" >&2
  return 1
}

pc_launch_status() { # <record-file> -> safe pre-exec terminal state, if any
  local raw
  raw=$(tr -d '\r\n' < "$1.status" 2>/dev/null || true)
  case "$raw" in
    "MDDS_PC_LAUNCH_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE STATE=CANCELLED_PREEXEC")
      printf '%s' CANCELLED_PREEXEC ;;
    "MDDS_PC_LAUNCH_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE STATE=RECORD_WRITE_FAILED")
      printf '%s' RECORD_WRITE_FAILED ;;
    "MDDS_PC_LAUNCH_STATUS RUN_ID=$RUN_ID NONCE=$RUN_NONCE STATE=INTENT_INVALID")
      printf '%s' INTENT_INVALID ;;
  esac
}

read_pc_pid_record() { # <record-file>; print verified guard pid:start after bounded retries
  local record_file="$1" attempt raw pair
  for attempt in 1 2 3; do
    PC_RECORD_READ_ATTEMPTS=$((PC_RECORD_READ_ATTEMPTS + 1))
    if [ "$DROP_FIRST_RECORD_READ" = 1 ] && [ "$PC_RECORD_READ_ATTEMPTS" -eq 1 ]; then
      printf 'event=inject-drop-first-pc-record-read record=%s\n' "$record_file" \
        >> "$LAUNCH_FAULT_LOG"
      sleep 1
      continue
    fi
    # The first probe can run before the PowerShell guard has created its
    # record.  Treat that expected state as an empty read, without letting the
    # shell redirection emit a misleading host-side error into gate evidence.
    raw=$({ if [ -r "$record_file" ]; then tr -d '\r' < "$record_file"; fi; } 2>/dev/null || true)
    pair=$(printf '%s' "$raw" | parse_pc_pid_record | head -1)
    if [[ "$pair" =~ ^[0-9]+:[0-9]+$ ]]; then
      printf '%s' "$pair"
      return 0
    fi
    sleep 1
  done
  return 1
}

pc_start_sweep_sub() {
  local count="${1:-}" depth="${2:-}" tag="${3:-ds03}" topic="${4:-$DS3_TOPIC}"
  # DS-03 publishes a short burst through the gateway.  The PC reader must be
  # able to retain every expected sample while its executor drains the queue;
  # otherwise KEEP_LAST(10) fabricates a transport-loss failure (11/30 was the
  # observed signature).  Keep this guard next to the PC launch so a future
  # scenario-count change cannot silently reintroduce that false negative.
  if ! [[ "$count" =~ ^[1-9][0-9]*$ && "$depth" =~ ^[1-9][0-9]*$ && "$tag" =~ ^[a-z0-9_]+$ && "$topic" =~ ^/[A-Za-z0-9_-]+$ ]] || (( depth < count )); then
    echo "   ERROR: invalid DS-03 PC queue/topic contract: count=$count depth=$depth topic=$topic (depth must be >= count)" >&2
    return 1
  fi
  # The recorded root is a PowerShell guard, not cmd.exe.  The guard writes a
  # signed PID/start record before it starts cmd, then waits for cmd; therefore
  # taskkill /T can prove cleanup of every inherited child rather than merely
  # stopping a launcher that already escaped to a new process tree.
  local out_win err_win token_file ps_err_file record_file intent_file cancel_file status_file token pair source terminal i msys_env_conv_excl
  out_win=$(cygpath -w "$LOGDIR/${tag}_pc_sub.log")
  err_win=$(cygpath -w "$LOGDIR/${tag}_pc_sub.err.log")
  record_file="$LOGDIR/.pc_guard_${RUN_NONCE}_$$_${RANDOM}.record"
  intent_file="$record_file.intent"
  cancel_file="$record_file.cancel"
  status_file="$record_file.status"
  token_file="$record_file.stdout"
  ps_err_file="$record_file.stderr"
  if ! ( set -C; printf 'MDDS_PC_LAUNCH_INTENT RUN_ID=%s NONCE=%s\n' "$RUN_ID" "$RUN_NONCE" > "$intent_file" ) 2>/dev/null \
    || ! grep -Fqx "MDDS_PC_LAUNCH_INTENT RUN_ID=$RUN_ID NONCE=$RUN_NONCE" "$intent_file"; then
    echo "   ERROR: cannot establish exclusive PC launch intent: $intent_file" >&2
    return 1
  fi
  export MDDS_PC_BATCH="$PC_BAT_DIR\\gw_pc_sweep_sub.bat"
  export MDDS_PC_STDOUT="$out_win"
  export MDDS_PC_STDERR="$err_win"
  export MDDS_PC_WORKDIR="$(cygpath -w "$PC_WS")"
  export MDDS_PC_RECORD="$(cygpath -w "$record_file")"
  export MDDS_PC_INTENT="$(cygpath -w "$intent_file")"
  export MDDS_PC_CANCEL="$(cygpath -w "$cancel_file")"
  export MDDS_PC_STATUS="$(cygpath -w "$status_file")"
  export MDDS_PC_SWEEP_COUNT="$count"
  export MDDS_PC_SWEEP_DEPTH="$depth"
  export MDDS_PC_SWEEP_TOPIC="$topic"
  # MSYS2_ARG_CONV_EXCL (set for HDC below) does not control inherited
  # environment conversion.  Without this narrow exclusion Git Bash rewrites
  # the absolute ROS topic, e.g. /mdds_dsb_sweep_X, into a Windows path before
  # PowerShell can hand it to cmd.exe.  Keep the already-Windows-formatted
  # file-path variables convertible; only this protocol value must remain
  # byte-for-byte unchanged.  This matches the guarded PC launcher in the GW
  # runner and is covered by test_msys_pc_topic_env.sh without a board.
  msys_env_conv_excl="$(mdds_append_msys2_env_conv_excl MDDS_PC_SWEEP_TOPIC)" || {
    echo "   ERROR: cannot construct MSYS2 environment-conversion exclusion" >&2
    return 1
  }
  # Pending is set before the guard is even forked.  On an interrupt before it
  # reaches its record write, cleanup leaves a signed cancellation request and
  # waits for its signed pre-exec acknowledgement; it never treats absence as
  # proof of safety.
  PENDING_PC_RECORD="$record_file"
  MSYS2_ENV_CONV_EXCL="$msys_env_conv_excl" \
  powershell -NoProfile -NonInteractive -Command '
    function Set-LaunchStatus([string]$state) {
      try { [System.IO.File]::WriteAllText($env:MDDS_PC_STATUS, "MDDS_PC_LAUNCH_STATUS RUN_ID=$env:MDDS_RUN_ID NONCE=$env:MDDS_RUN_NONCE STATE=$state$([Environment]::NewLine)") } catch { }
    }
    function Has-ExpectedCancel {
      if (-not (Test-Path -LiteralPath $env:MDDS_PC_CANCEL)) { return $false }
      try { return (([System.IO.File]::ReadAllText($env:MDDS_PC_CANCEL)).Trim() -eq "MDDS_PC_LAUNCH_CANCEL RUN_ID=$env:MDDS_RUN_ID NONCE=$env:MDDS_RUN_NONCE") } catch { return $false }
    }
    try {
      $intent = "MDDS_PC_LAUNCH_INTENT RUN_ID=$env:MDDS_RUN_ID NONCE=$env:MDDS_RUN_NONCE"
      if (-not (Test-Path -LiteralPath $env:MDDS_PC_INTENT) -or ([System.IO.File]::ReadAllText($env:MDDS_PC_INTENT)).Trim() -ne $intent) { Set-LaunchStatus "INTENT_INVALID"; exit 0 }
      if (Has-ExpectedCancel) { Set-LaunchStatus "CANCELLED_PREEXEC"; exit 0 }
      $start = (Get-Process -Id $PID).StartTime.ToUniversalTime().ToFileTimeUtc()
      $record = "MDDS_PC_LAUNCH_RECORD RUN_ID=$env:MDDS_RUN_ID NONCE=$env:MDDS_RUN_NONCE PID=$PID START=$start"
      if ($env:MDDS_TEST_FAIL_LAUNCH_RECORD_WRITE -eq "1") { Set-LaunchStatus "RECORD_WRITE_FAILED"; exit 0 }
      try {
        # CreateNew prevents a duplicated/stale launcher from replacing an
        # already-owned record and thereby making its payload untrackable.
        $stream = [System.IO.File]::Open($env:MDDS_PC_RECORD, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
        try {
          $bytes = [System.Text.Encoding]::UTF8.GetBytes("$record$([Environment]::NewLine)")
          $stream.Write($bytes, 0, $bytes.Length)
        } finally { $stream.Dispose() }
        if (([System.IO.File]::ReadAllText($env:MDDS_PC_RECORD)).Trim() -ne $record) { throw "record verification failed" }
      } catch { Set-LaunchStatus "RECORD_WRITE_FAILED"; exit 0 }
      if (Has-ExpectedCancel) { Set-LaunchStatus "CANCELLED_PREEXEC"; exit 0 }
      $cmd = "call `"$env:MDDS_PC_BATCH`" --topic $env:MDDS_PC_SWEEP_TOPIC --sizes 1024 --count $env:MDDS_PC_SWEEP_COUNT --depth $env:MDDS_PC_SWEEP_DEPTH --idle-timeout 60"
      $child = Start-Process -FilePath "cmd.exe" -ArgumentList @("/c", $cmd) -WorkingDirectory $env:MDDS_PC_WORKDIR -RedirectStandardOutput $env:MDDS_PC_STDOUT -RedirectStandardError $env:MDDS_PC_STDERR -WindowStyle Hidden -PassThru
      if ($env:MDDS_TEST_SUPPRESS_LAUNCH_TOKEN -ne "1") { Write-Output "MDDS_PC_ROOT=${PID}:$start" }
      $child.WaitForExit()
      exit $child.ExitCode
    } catch { exit 1 }
  ' > "$token_file" 2> "$ps_err_file" &
  for i in $(seq 1 10); do
    pair=$(read_pc_pid_record "$record_file" || true)
    [[ "$pair" =~ ^[0-9]+:[0-9]+$ ]] && break
    terminal=$(pc_launch_status "$record_file")
    [ -n "$terminal" ] && break
    sleep 1
  done
  token=$(tr -d '\r\n' < "$token_file" 2>/dev/null || true)
  unset MDDS_PC_BATCH MDDS_PC_STDOUT MDDS_PC_STDERR MDDS_PC_WORKDIR MDDS_PC_RECORD MDDS_PC_INTENT MDDS_PC_CANCEL MDDS_PC_STATUS MDDS_PC_SWEEP_COUNT MDDS_PC_SWEEP_DEPTH MDDS_PC_SWEEP_TOPIC
  if ! [[ "$pair" =~ ^([0-9]+):([0-9]+)$ ]]; then
    terminal=$(pc_launch_status "$record_file")
    echo "   ERROR: failed to recover signed PC guard identity (terminal=${terminal:-unresolved}); stderr=$(head -1 "$ps_err_file" 2>/dev/null)" >&2
    return 1
  fi
  source=local-record
  if printf '%s' "$token" | grep -Fqx "MDDS_PC_ROOT=$pair"; then
    source=stdout+local-record
  fi
  # Publish the tracked root before dropping pending.  Either side of an
  # asynchronous signal can now recover the exact same PowerShell tree.
  PC_PID="${BASH_REMATCH[1]}"
  PC_START_TICKS="${BASH_REMATCH[2]}"
  PC_RECORD_FILE="$record_file"
  printf 'run_id=%s nonce=%s pid=%s start=%s source=%s local_record=%s intent=%s stdout=%s stderr=%s log=%s_pc_sub.log\n' \
    "$RUN_ID" "$RUN_NONCE" "$PC_PID" "$PC_START_TICKS" "$source" "$record_file" "$intent_file" "$token_file" "$ps_err_file" \
    "$tag" \
    >> "$LOGDIR/pc_launch_records.txt"
  PENDING_PC_RECORD=""
  if [ "$source" = local-record ]; then
    echo "   recovered PC launch identity from persistent local record: $PC_PID:$PC_START_TICKS" >&2
  fi
}

pc_cleanup_pending_record() {
  [ -n "$PENDING_PC_RECORD" ] || return 0
  local record_file="$PENDING_PC_RECORD" cancel_file="$record_file.cancel" status_file="$record_file.status" intent_file="$record_file.intent" out
  export MDDS_PC_RECORD="$(cygpath -w "$record_file")"
  export MDDS_PC_CANCEL="$(cygpath -w "$cancel_file")"
  export MDDS_PC_STATUS="$(cygpath -w "$status_file")"
  export MDDS_PC_INTENT="$(cygpath -w "$intent_file")"
  out=$(powershell -NoProfile -NonInteractive -Command '
    try {
      $intent = "MDDS_PC_LAUNCH_INTENT RUN_ID=$env:MDDS_RUN_ID NONCE=$env:MDDS_RUN_NONCE"
      $cancel = "MDDS_PC_LAUNCH_CANCEL RUN_ID=$env:MDDS_RUN_ID NONCE=$env:MDDS_RUN_NONCE"
      if (-not (Test-Path -LiteralPath $env:MDDS_PC_INTENT) -or ([System.IO.File]::ReadAllText($env:MDDS_PC_INTENT)).Trim() -ne $intent) { Write-Output "PENDING_PC_INTENT_INVALID"; exit 2 }
      if ((Test-Path -LiteralPath $env:MDDS_PC_CANCEL) -and ([System.IO.File]::ReadAllText($env:MDDS_PC_CANCEL)).Trim() -ne $cancel) { Write-Output "PENDING_PC_CANCEL_CONFLICT"; exit 3 }
      [System.IO.File]::WriteAllText($env:MDDS_PC_CANCEL, "$cancel$([Environment]::NewLine)")
      if (([System.IO.File]::ReadAllText($env:MDDS_PC_CANCEL)).Trim() -ne $cancel) { Write-Output "PENDING_PC_CANCEL_WRITE_FAILED"; exit 4 }
      for ($i = 0; $i -lt 20; ++$i) {
        if (Test-Path -LiteralPath $env:MDDS_PC_RECORD) {
          $record = ([System.IO.File]::ReadAllText($env:MDDS_PC_RECORD)).Trim()
          if ($record -notmatch "^MDDS_PC_LAUNCH_RECORD RUN_ID=$([regex]::Escape($env:MDDS_RUN_ID)) NONCE=$([regex]::Escape($env:MDDS_RUN_NONCE)) PID=([0-9]+) START=([0-9]+)$") { Write-Output "PENDING_PC_RECORD_INVALID"; exit 5 }
          $pid = [int]$matches[1]; $start = [int64]$matches[2]
          $p = Get-Process -Id $pid -ErrorAction SilentlyContinue
          if ($null -eq $p) { Write-Output "PENDING_PC_RECORD_GONE"; exit 0 }
          if ($p.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $start) { Write-Output "PENDING_PC_RECORD_REUSED"; exit 6 }
          & taskkill.exe /PID $pid /T /F | Out-Null
          if ($LASTEXITCODE -ne 0) { Write-Output "PENDING_PC_RECORD_SIGNAL_FAILED"; exit 7 }
          for ($j = 0; $j -lt 20; ++$j) { Start-Sleep -Milliseconds 100; if ($null -eq (Get-Process -Id $pid -ErrorAction SilentlyContinue)) { Write-Output "PENDING_PC_RECORD_STOPPED"; exit 0 } }
          Write-Output "PENDING_PC_RECORD_LIVE"; exit 8
        }
        if (Test-Path -LiteralPath $env:MDDS_PC_STATUS) {
          $status = ([System.IO.File]::ReadAllText($env:MDDS_PC_STATUS)).Trim()
          if ($status -eq "MDDS_PC_LAUNCH_STATUS RUN_ID=$env:MDDS_RUN_ID NONCE=$env:MDDS_RUN_NONCE STATE=CANCELLED_PREEXEC") { Write-Output "PENDING_PC_RECORD_CANCELLED_PREEXEC"; exit 0 }
          if ($status -eq "MDDS_PC_LAUNCH_STATUS RUN_ID=$env:MDDS_RUN_ID NONCE=$env:MDDS_RUN_NONCE STATE=RECORD_WRITE_FAILED") { Write-Output "PENDING_PC_RECORD_WRITE_FAILED_PREEXEC"; exit 0 }
          if ($status -eq "MDDS_PC_LAUNCH_STATUS RUN_ID=$env:MDDS_RUN_ID NONCE=$env:MDDS_RUN_NONCE STATE=INTENT_INVALID") { Write-Output "PENDING_PC_RECORD_INTENT_INVALID_PREEXEC"; exit 0 }
          Write-Output "PENDING_PC_STATUS_INVALID"; exit 5
        }
        Start-Sleep -Milliseconds 250
      }
      Write-Output "PENDING_PC_RECORD_UNRESOLVED"; exit 9
    } catch { Write-Output "PENDING_PC_RECORD_ERROR"; exit 10 }
  ' 2>&1 || true)
  unset MDDS_PC_RECORD MDDS_PC_CANCEL MDDS_PC_STATUS MDDS_PC_INTENT
  printf 'local_record=%s result=%s\n' "$record_file" \
    "$(printf '%s' "$out" | tr -d '\r\n')" >> "$PENDING_CLEANUP_LOG"
  case "$out" in
    *PENDING_PC_RECORD_GONE*|*PENDING_PC_RECORD_STOPPED*|*PENDING_PC_RECORD_CANCELLED_PREEXEC*|*PENDING_PC_RECORD_WRITE_FAILED_PREEXEC*|*PENDING_PC_RECORD_INTENT_INVALID_PREEXEC*)
      PENDING_PC_RECORD=""
      return 0 ;;
    *)
      echo "   ERROR: pending PC launch record could not be safely recovered: $record_file" >&2
      return 1 ;;
  esac
}

pc_cleanup() {
  if [[ "$PC_PID" =~ ^[0-9]+$ && "$PC_START_TICKS" =~ ^[0-9]+$ ]]; then
    export MDDS_PC_PID="$PC_PID"
    export MDDS_PC_START_TICKS="$PC_START_TICKS"
    if ! powershell -NoProfile -NonInteractive -Command '
      $p = Get-Process -Id ([int]$env:MDDS_PC_PID) -ErrorAction SilentlyContinue
      if ($null -eq $p) { exit 0 }
      if ($p.StartTime.ToUniversalTime().ToFileTimeUtc() -ne [int64]$env:MDDS_PC_START_TICKS) {
        Write-Error "refusing reused PC PID $env:MDDS_PC_PID"
        exit 3
      }
      & taskkill.exe /PID $env:MDDS_PC_PID /T /F | Out-Null
      if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
      for ($i = 0; $i -lt 20; ++$i) {
        Start-Sleep -Milliseconds 100
        if ($null -eq (Get-Process -Id ([int]$env:MDDS_PC_PID) -ErrorAction SilentlyContinue)) { exit 0 }
      }
      Write-Error "owned PC process tree did not exit: $env:MDDS_PC_PID"
      exit 4
    ' >/dev/null 2>&1; then
      echo "   ERROR: unable to clean owned PC process tree pid=$PC_PID" >&2
      unset MDDS_PC_PID MDDS_PC_START_TICKS
      return 1
    fi
    unset MDDS_PC_PID MDDS_PC_START_TICKS
    PC_PID=""
    PC_START_TICKS=""
  elif [ -n "$PC_PID$PC_START_TICKS" ]; then
    echo "   ERROR: incomplete owned PC process identity" >&2
    return 1
  fi
}

# A reliable MDDS writer can match the gateway before CycloneDDS has matched
# the PC subscriber.  The gateway's publisher is VOLATILE, so publishing in
# that gap loses valid first samples by DDS contract.  Its health log reports
# the actual Cyclone publisher subscription count; wait for it instead of a
# fixed startup sleep.  The probe is topic-specific: another bridge's healthy
# subscription is not evidence that this VOLATILE writer can reach the PC.
wait_gateway_cyclone_sub() { # <gateway-log-name>
  # Do the poll inside one HDC shell instead of opening 30 independent HDC
  # sessions.  On these boards an HDC session can take seconds to establish;
  # the old outer loop consumed the PC reader's entire volatile lifetime even
  # though the gateway had already matched it.
  local log="$1" marker
  [[ "$log" =~ ^[a-z0-9_]+\.log$ ]] || return 1
  marker=$(shell "$BOARD_A" \
    "i=0; while [ \$i -lt 30 ]; do if grep -F \"$DS3_TOPIC alive: \" '$REMOTE_LOGDIR/$log' 2>/dev/null | grep -Eq 'cyclone\\(pub_subs=[1-9][0-9]*\\)'; then echo GW_CYCLONE_SUB_READY; exit 0; fi; i=\$((i+1)); sleep 1; done; echo GW_CYCLONE_SUB_TIMEOUT" \
    | tr -d '\r')
  [[ "$marker" == *GW_CYCLONE_SUB_READY* ]]
}

wait_gateway_mdds_writer() { # <gateway-log-name>; the true B writer must be admitted on A
  # `remote(w,r)` is derived from committed, QoS-compatible remote endpoint
  # snapshots.  The DS-03 seed is only a reader, so its second field is zero;
  # a positive second field on this run-unique topic proves the new B writer
  # crossed ANNOUNCE admission before its release barrier opens.
  local log="$1" marker
  [[ "$log" =~ ^[a-z0-9_]+\.log$ ]] || return 1
  marker=$(shell "$BOARD_A" \
    "i=0; while [ \$i -lt 40 ]; do if grep -F \"$DS3_TOPIC alive: \" '$REMOTE_LOGDIR/$log' 2>/dev/null | grep -Eq 'remote\\(w,r\\)=\\([0-9]+,[1-9][0-9]*\\)'; then echo GW_MDDS_WRITER_READY; exit 0; fi; i=\$((i+1)); sleep 1; done; echo GW_MDDS_WRITER_TIMEOUT" \
    | tr -d '\r')
  [[ "$marker" == *GW_MDDS_WRITER_READY* ]]
}

prepare_publisher_barrier() { # <safe-tag> <release-path> <token>
  local tag="$1" release_path="$2" token="$3" out
  [[ "$tag" =~ ^[a-z0-9_]+$ && "$release_path" == "$REMOTE_LOGDIR/"* && "$token" =~ ^[A-Za-z0-9_-]{1,200}$ ]] || return 1
  ensure_remote_owner "$BOARD_B" || return 1
  out=$(shell "$BOARD_B" \
    "if test -d '$REMOTE_LOGDIR' && test ! -L '$REMOTE_LOGDIR' && test -f '$REMOTE_OWNER' && test ! -L '$REMOTE_OWNER' && grep -Fqx 'RUN_ID=$RUN_ID' '$REMOTE_OWNER' && grep -Fqx 'NONCE=$RUN_NONCE' '$REMOTE_OWNER' && ! test -e '$release_path' && ! test -L '$release_path' && ! test -e '$release_path.tmp' && ! test -L '$release_path.tmp'; then printf BARRIER_RELEASE_PATH_CLEAR; else printf BARRIER_RELEASE_PATH_CONFLICT; fi" \
    | tr -d '\r\n')
  [[ "$out" == "BARRIER_RELEASE_PATH_CLEAR" ]]
}

commit_publisher_barrier_release() { # <release-path> <token>; atomically reveal a fully written file
  local release_path="$1" token="$2" expected out
  [[ "$release_path" == "$REMOTE_LOGDIR/"* && "$token" =~ ^[A-Za-z0-9_-]{1,200}$ ]] || return 1
  expected="MDDS_SWEEP_RELEASE token=$token"
  # A same-directory hard link makes the release path visible only after its
  # complete exact-token body has been written and verified.  Both paths are
  # generated from validated run/tag/nonce fields; the only cleanup removes
  # this invocation's exact temporary path.
  out=$(shell "$BOARD_B" \
    "release='$release_path'; tmp='$release_path.tmp'; expected='$expected'; if ! test -d '$REMOTE_LOGDIR' || test -L '$REMOTE_LOGDIR' || test -e \"\$release\" || test -L \"\$release\" || test -e \"\$tmp\" || test -L \"\$tmp\"; then printf BARRIER_RELEASE_CONFLICT; exit 2; fi; umask 077; if ! printf '%s\\n' \"\$expected\" > \"\$tmp\" || ! test -f \"\$tmp\" || test -L \"\$tmp\" || ! grep -Fqx \"\$expected\" \"\$tmp\"; then rm -f \"\$tmp\"; printf BARRIER_RELEASE_STAGE_FAILED; exit 3; fi; if ln \"\$tmp\" \"\$release\" 2>/dev/null && test -f \"\$release\" && test ! -L \"\$release\" && grep -Fqx \"\$expected\" \"\$release\"; then rm -f \"\$tmp\"; printf BARRIER_RELEASE_COMMITTED; else rm -f \"\$tmp\"; printf BARRIER_RELEASE_COMMIT_FAILED; exit 4; fi" \
    | tr -d '\r\n')
  [[ "$out" == "BARRIER_RELEASE_COMMITTED" ]]
}

wait_publisher_barrier_ready() { # <publisher-log-name> <token>
  local log="$1" token="$2" marker
  [[ "$log" =~ ^[a-z0-9_]+\.log$ && "$token" =~ ^[A-Za-z0-9_-]{1,200}$ ]] || return 1
  marker=$(shell "$BOARD_B" \
    "i=0; while [ \$i -lt 30 ]; do if grep -Eq '^SWEEP-PUB-BARRIER-READY token=$token local_subs=[1-9][0-9]*$' '$REMOTE_LOGDIR/$log' 2>/dev/null; then echo PUB_BARRIER_READY; exit 0; fi; if grep -Fq 'SWEEP-PUB-NO-MATCH' '$REMOTE_LOGDIR/$log' 2>/dev/null || grep -Fq 'SWEEP-PUB-BARRIER-FAIL' '$REMOTE_LOGDIR/$log' 2>/dev/null; then echo PUB_BARRIER_FAILED; exit 0; fi; i=\$((i+1)); sleep 1; done; echo PUB_BARRIER_TIMEOUT" \
    | tr -d '\r')
  [[ "$marker" == *PUB_BARRIER_READY* ]]
}

wait_gateway_bridge() { # <gateway-log-name>
  local log="$1" marker
  [[ "$log" =~ ^[a-z0-9_]+\.log$ ]] || return 1
  marker=$(shell "$BOARD_A" \
    "i=0; while [ \$i -lt 30 ]; do if grep -Fq 'bridging $DS3_TOPIC [std_msgs/msg/ByteMultiArray]' '$REMOTE_LOGDIR/$log' 2>/dev/null; then echo GW_BRIDGE_READY; exit 0; fi; i=\$((i+1)); sleep 1; done; echo GW_BRIDGE_TIMEOUT" \
    | tr -d '\r')
  [[ "$marker" == *GW_BRIDGE_READY* ]]
}

trace_dsb_gateway_case() { # <safe tag> <event...>
  local tag="$1"; shift
  [[ "$tag" =~ ^[a-z0-9_]+$ ]] || return 1
  printf '%s %s\n' "$(date -Ins)" "$*" >> "$LOGDIR/${tag}_timeline.log"
}

gateway_final_m2c_is_healthy() { # <local gateway log>
  local log="$1"
  awk -v topic="$DS3_TOPIC final: " '
    index($0, topic) &&
    $0 ~ /m2c_ack_batches=[1-9][0-9]*/ &&
    $0 ~ /m2c_ack_timeouts=0 / &&
    $0 ~ /m2c_messages_lost=0 / &&
    $0 ~ /m2c_resource_drops=0 / &&
    $0 ~ /m2c_terminal=0 / &&
    $0 ~ /m2c_forward_exceptions=0 / { found = 1 }
    END { exit !found }
  ' "$log"
}

gateway_alive_c2m_is_healthy() { # <local gateway log>
  local log="$1"
  awk -v topic="$DS3_TOPIC alive: " '
    index($0, topic) &&
    $0 ~ /c2m_dropped=0 / &&
    $0 ~ /cyclone\(pub_subs=[1-9][0-9]*\)/ { found = 1 }
    END { exit !found }
  ' "$log"
}

pass=0; fail=0; failed_ids=()
verdict() { # verdict <ID> <0|1> [detail]
  if [ "$2" -eq 0 ]; then echo "$1 PASS $3"; pass=$((pass+1));
  else echo "$1 FAIL $3"; fail=$((fail+1)); failed_ids+=("$1"); fi
}

# Prove that the binaries which this gate executes are the current local
# final artifacts. Deploying a Python scenario alone is not sufficient: an
# otherwise-green result from stale board libraries or a stale production
# profile would say nothing about a newly reviewed transport/lifecycle fix.
# mdds, rmw_mdds and the DSoftBus profile execute on both boards; the gateway
# is only used on board A.
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

render_dsb_gateway_config() {
  local tmp rendered_topic_count template_topic_count
  [ -f "$DSB_CONFIG_TEMPLATE" ] && [ ! -L "$DSB_CONFIG_TEMPLATE" ] || {
    echo "ERROR: DS-03 gateway config template is not a regular file: $DSB_CONFIG_TEMPLATE" >&2
    return 1
  }
  [ ! -e "$DSB_CONFIG_LOCAL" ] || {
    echo "ERROR: refusing to overwrite DS-03 rendered config: $DSB_CONFIG_LOCAL" >&2
    return 1
  }
  template_topic_count=$(grep -Fxc 'topic = @DS3_TOPIC@' "$DSB_CONFIG_TEMPLATE" 2>/dev/null || true)
  if [ "$template_topic_count" -ne 1 ]; then
    echo "ERROR: DS-03 config template must contain exactly one topic placeholder" >&2
    return 1
  fi
  tmp=$(mktemp "$LOGDIR/.mdds_gateway_dsb.XXXXXX") || return 1
  if ! sed "s|@DS3_TOPIC@|$DS3_TOPIC|g" "$DSB_CONFIG_TEMPLATE" > "$tmp"; then
    rm -f -- "$tmp"
    return 1
  fi
  rendered_topic_count=$(grep -Fxc "topic = $DS3_TOPIC" "$tmp" 2>/dev/null || true)
  if [ "$rendered_topic_count" -ne 1 ] \
    || grep -Fq '@DS3_TOPIC@' "$tmp" \
    || [ "$(grep -Fxc 'cyclone_domain_id = 46' "$tmp" 2>/dev/null || true)" -ne 1 ] \
    || [ "$(grep -Fxc 'mdds_domain_id = 43' "$tmp" 2>/dev/null || true)" -ne 1 ]; then
    echo "ERROR: DS-03 rendered gateway config failed its topic/domain contract" >&2
    rm -f -- "$tmp"
    return 1
  fi
  mv -- "$tmp" "$DSB_CONFIG_LOCAL" || return 1
}

push_dsb_files() {
  : > "$LOGDIR/helper_transfer_transcript.txt" || return 1
  local board file
  render_dsb_gateway_config || return 1
  for board in "$BOARD_A" "$BOARD_B"; do
    send_verified_helper "$board" board_sweep.py scripts/mdds_e2e/board_sweep.py \
      "$DEVICE_DIR/mdds_e2e/board_sweep.py" || return 1
  done
  send_verified_helper "$BOARD_A" cyclonedds_board_a.xml scripts/mdds_e2e/cyclonedds_board_a.xml \
    "$DEVICE_DIR/mdds_e2e/cyclonedds_board_a.xml" || return 1
  send_verified_helper "$BOARD_A" mdds_gateway_dsb.rendered.conf "$DSB_CONFIG_LOCAL" \
    "$DEVICE_DIR/mdds_e2e/mdds_gateway_dsb.conf" || return 1
}

verify_final_artifacts() {
  local local_path name board remote want got
  : > "$LOGDIR/artifact_hashes.txt"
  for name in libmdds.so librmw_mdds.so librmw_cyclonedds_cpp.so ohos_dsoftbus.env mdds_gateway; do
    case "$name" in
      libmdds.so) local_path="install_ohos/lib/libmdds.so"; remote="$DEVICE_DIR/lib/libmdds.so" ;;
      librmw_mdds.so) local_path="install_ohos/lib/librmw_mdds.so"; remote="$DEVICE_DIR/lib/librmw_mdds.so" ;;
      librmw_cyclonedds_cpp.so)
        local_path="install_ohos/lib/librmw_cyclonedds_cpp.so"
        remote="$DEVICE_DIR/lib/librmw_cyclonedds_cpp.so"
        ;;
      ohos_dsoftbus.env)
        local_path="install_ohos/share/rmw_mdds/config/ohos_dsoftbus.env"
        remote="$DEVICE_DIR/share/rmw_mdds/config/ohos_dsoftbus.env"
        ;;
      mdds_gateway) local_path="install_ohos/lib/mdds_gateway/mdds_gateway"; remote="$DEVICE_DIR/lib/mdds_gateway/mdds_gateway" ;;
    esac
    if [ ! -f "$local_path" ]; then
      echo "missing local final artifact: $local_path" | tee -a "$LOGDIR/artifact_hashes.txt"
      return 1
    fi
    want=$(sha256_local "$local_path")
    # Gateway and its Cyclone-specific RMW are not board-B test dependencies.
    for board in "$BOARD_A" $([ "$name" = mdds_gateway ] || [ "$name" = librmw_cyclonedds_cpp.so ] || echo "$BOARD_B"); do
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

# poll_sweep_result <board> <device-log> <max-10s-polls> — hdc shell always
# exits 0, so poll on captured content, never on exit status.
poll_sweep_result() {
  local i n=""
  for i in $(seq 1 "$3"); do
    sleep 10
    n=$(shell "$1" "grep -c SWEEP_RESULT '$REMOTE_LOGDIR/$2' 2>/dev/null || true" | tr -dc '0-9')
    [ -n "$n" ] && [ "$n" -ge 1 ] && return 0
  done
  echo "   no SWEEP_RESULT in $2 within the poll window"
  return 1
}

# assert_sweep_pass <local-log> <expected-count>
assert_sweep_pass() {
  local line recv
  require_current_log "$1" || return 1
  grep -q "SWEEP_RESULT PASS" "$1" || { echo "   no SWEEP_RESULT PASS in $1"; return 1; }
  grep -q " BAD" "$1" && { echo "   BAD block in $1"; return 1; }
  line=$(grep "SWEEP-SUB size=1024 " "$1")
  [ -n "$line" ] || { echo "   no 1024-block line in $1"; return 1; }
  echo "$line" | grep -q "lost=0 reorder=0 crc=0" || { echo "   loss/reorder/crc: $line"; return 1; }
  recv=$(echo "$line" | sed -n 's/.*received=\([0-9]*\)\/.*/\1/p')
  [ "${recv:-0}" -eq "$2" ] || { echo "   incomplete block (want $2): $line"; return 1; }
  return 0
}

# assert_sweep_all_pass <local-log> <expected-count> <size...>
# Unlike the smoke gate, large samples exercise DATA_FRAG/reassembly and must
# prove every requested size completed exactly, byte-for-byte, in order.
assert_sweep_all_pass() {
  local log="$1" expected="$2"; shift 2
  local size line recv
  require_current_log "$log" || return 1
  grep -q "SWEEP_RESULT PASS" "$log" || { echo "   no SWEEP_RESULT PASS in $log"; return 1; }
  grep -q " BAD" "$log" && { echo "   BAD block in $log"; return 1; }
  for size in "$@"; do
    line=$(grep "SWEEP-SUB size=$size " "$log")
    [ -n "$line" ] || { echo "   no size=$size line in $log"; return 1; }
    echo "$line" | grep -q "lost=0 reorder=0 crc=0" \
      || { echo "   size=$size loss/reorder/crc: $line"; return 1; }
    recv=$(echo "$line" | sed -n 's/.*received=\([0-9]*\)\/.*/\1/p')
    [ "${recv:-0}" -eq "$expected" ] \
      || { echo "   size=$size incomplete (want $expected): $line"; return 1; }
  done
}

# --- scenarios ---------------------------------------------------------------

s_ds01() {
  # A -> B, dsoftbus-only, exact 30/30. MDDS_DEBUG=1 on both sides feeds DS-06.
  cleanup_dsb || return 1
  launch "$BOARD_B" "$DSB_ENVS" \
    "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode sub --topic /mdds_dsb --sizes 1024 --count 30 --idle-timeout 25" \
    ds01_sub.log || return
  sleep 4
  launch "$BOARD_A" "$DSB_ENVS" \
    "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode pub --topic /mdds_dsb --sizes 1024 --count 30 --rate 20 --wait-match --flush-ms 8000" \
    ds01_pub.log || return
  poll_sweep_result "$BOARD_B" ds01_sub.log 12
  local polled=$?
  kill_tracked || return 1
  pull "$BOARD_B" ds01_sub.log || return 1
  pull "$BOARD_A" ds01_pub.log || return 1
  local bad=$polled
  assert_sweep_pass "$LOGDIR/ds01_sub.log" 30 || bad=1
  verdict "DS-01" $bad "A->B dsoftbus-only exact 30/30 (ds01_sub.log)"
}

s_ds02() {
  # B -> A, dsoftbus-only, exact 30/30; B pins SYSTEM_DEFAULT discovery range.
  cleanup_dsb || return 1
  launch "$BOARD_A" "$DSB_ENVS" \
    "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode sub --topic /mdds_dsb --sizes 1024 --count 30 --idle-timeout 25" \
    ds02_sub.log || return
  sleep 4
  launch "$BOARD_B" "$DSB_ENVS_B" \
    "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode pub --topic /mdds_dsb --sizes 1024 --count 30 --rate 20 --wait-match --flush-ms 8000" \
    ds02_pub.log || return
  poll_sweep_result "$BOARD_A" ds02_sub.log 12
  local polled=$?
  kill_tracked || return 1
  pull "$BOARD_A" ds02_sub.log || return 1
  pull "$BOARD_B" ds02_pub.log || return 1
  local bad=$polled
  assert_sweep_pass "$LOGDIR/ds02_sub.log" 30 || bad=1
  verdict "DS-02" $bad "B->A dsoftbus-only exact 30/30, B SYSTEM_DEFAULT (ds02_sub.log)"
}

s_ds03_case() { # <verdict-id> <safe-log-tag> <count> <rate-hz> <settle-ms> <minimum-ack-batches> <detail>
  local verdict_id="$1" tag="$2" ds03_count="$3" rate="$4" settle_ms="$5" minimum_batches="$6" detail="$7"
  local ds03_pc_depth=64
  if ! [[ "$tag" =~ ^[a-z0-9_]+$ && "$ds03_count" =~ ^[1-9][0-9]*$ && "$rate" =~ ^[1-9][0-9]*$ && "$settle_ms" =~ ^[0-9]+$ && "$minimum_batches" =~ ^[1-9][0-9]*$ ]]; then
    echo "   ERROR: invalid DS-03 gateway-case parameters" >&2
    return 1
  fi
  (( ds03_pc_depth >= ds03_count )) || ds03_pc_depth=$ds03_count
  local seed_log="${tag}_seed.log" gw_log="${tag}_gw.log" pub_log="${tag}_pub.log" pc_log="${tag}_pc_sub.log"
  cleanup_dsb || return 1
  : > "$LOGDIR/${tag}_timeline.log"
  trace_dsb_gateway_case "$tag" "begin topic=$DS3_TOPIC count=$ds03_count rate=$rate settle_ms=$settle_ms qos=reliable/keep_last/10"
  # A passive board-B reader establishes discovery/type exchange before the
  # gateway and PC volatile reader start.  It is deliberately a reader: after
  # it stops, the runner must prove that the *new writer* was admitted on A
  # before any first DATA can leave B.
  launch "$BOARD_B" "$DSB_ENVS_B" \
    "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode sub --topic $DS3_TOPIC --sizes 1024 --count $ds03_count --idle-timeout 90" \
    "$seed_log" || return 1
  local seed_pid="$LAST_PID"
  trace_dsb_gateway_case "$tag" "seed_started board=B pid=$seed_pid"
  sleep 3
  # No `timeout` wrapper: kill_tracked must own the direct gateway PID so a
  # failed run cannot orphan a process holding the DSoftBus domain-43 session.
  launch "$BOARD_A" "$GWENVS" \
    "$DEVICE_DIR/lib/mdds_gateway/mdds_gateway -c $DEVICE_DIR/mdds_e2e/mdds_gateway_dsb.conf" \
    "$gw_log" || return 1
  trace_dsb_gateway_case "$tag" "gateway_started board=A pid=$LAST_PID"
  if ! wait_gateway_bridge "$gw_log"; then
    pull "$BOARD_A" "$gw_log" || echo "   ERROR: unable to recover gateway failure log" >&2
    echo "   gateway never created the DSoftBus-side bridge"
    cleanup_dsb || echo "   ERROR: cleanup after bridge readiness failure was incomplete" >&2
    return 1
  fi
  trace_dsb_gateway_case "$tag" "gateway_bridge_ready"
  pc_start_sweep_sub "$ds03_count" "$ds03_pc_depth" "$tag" "$DS3_TOPIC" || {
    cleanup_dsb || echo "   ERROR: cleanup after PC subscriber launch failure was incomplete" >&2
    return 1
  }
  trace_dsb_gateway_case "$tag" "pc_subscriber_started pid=$PC_PID"
  if ! wait_gateway_cyclone_sub "$gw_log"; then
    pull "$BOARD_A" "$gw_log" || echo "   ERROR: unable to recover gateway failure log" >&2
    echo "   gateway never observed a Cyclone PC subscription"
    cleanup_dsb || echo "   ERROR: cleanup after Cyclone readiness failure was incomplete" >&2
    return 1
  fi
  trace_dsb_gateway_case "$tag" "gateway_cyclone_sub_ready"
  kill_recorded "$BOARD_B" "$seed_pid" || {
    cleanup_dsb || echo "   ERROR: cleanup after seed shutdown failure was incomplete" >&2
    return 1
  }
  trace_dsb_gateway_case "$tag" "seed_stopped board=B pid=$seed_pid"
  local barrier_token="b_${RUN_NONCE}_${tag}"
  local barrier_release="$REMOTE_LOGDIR/${tag}.${RUN_NONCE}.release"
  if ! prepare_publisher_barrier "$tag" "$barrier_release" "$barrier_token"; then
    echo "   unable to reserve the true-publisher release barrier"
    cleanup_dsb || echo "   ERROR: cleanup after barrier reservation failure was incomplete" >&2
    return 1
  fi
  trace_dsb_gateway_case "$tag" "publisher_barrier_reserved board=B path=$barrier_release token=$barrier_token"
  launch "$BOARD_B" "$DSB_ENVS_B" \
    "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode pub --topic $DS3_TOPIC --sizes 1024 --count $ds03_count --rate $rate --reliability reliable --history keep_last --depth 10 --wait-match --match-timeout-ms 10000 --barrier-release-file $barrier_release --barrier-token $barrier_token --barrier-timeout-s 45 --settle-ms $settle_ms --flush-ms 10000" \
    "$pub_log" || {
      cleanup_dsb || echo "   ERROR: cleanup after true-publisher launch failure was incomplete" >&2
      return 1
    }
  trace_dsb_gateway_case "$tag" "true_publisher_started board=B pid=$LAST_PID"
  if ! wait_publisher_barrier_ready "$pub_log" "$barrier_token"; then
    pull "$BOARD_B" "$pub_log" || echo "   ERROR: unable to recover publisher barrier log" >&2
    pull "$BOARD_A" "$gw_log" || echo "   ERROR: unable to recover gateway barrier log" >&2
    echo "   true publisher did not establish its local release barrier"
    cleanup_dsb || echo "   ERROR: cleanup after publisher barrier readiness failure was incomplete" >&2
    return 1
  fi
  trace_dsb_gateway_case "$tag" "true_publisher_barrier_ready"
  if ! wait_gateway_mdds_writer "$gw_log"; then
    pull "$BOARD_B" "$pub_log" || echo "   ERROR: unable to recover publisher writer-readiness log" >&2
    pull "$BOARD_A" "$gw_log" || echo "   ERROR: unable to recover gateway writer-readiness log" >&2
    echo "   gateway never admitted the true board-B writer on the exact topic"
    cleanup_dsb || echo "   ERROR: cleanup after gateway writer-readiness failure was incomplete" >&2
    return 1
  fi
  trace_dsb_gateway_case "$tag" "gateway_mdds_writer_ready"
  if ! commit_publisher_barrier_release "$barrier_release" "$barrier_token"; then
    pull "$BOARD_B" "$pub_log" || echo "   ERROR: unable to recover publisher release log" >&2
    pull "$BOARD_A" "$gw_log" || echo "   ERROR: unable to recover gateway release log" >&2
    echo "   unable to commit the true-publisher release barrier"
    cleanup_dsb || echo "   ERROR: cleanup after barrier release failure was incomplete" >&2
    return 1
  fi
  trace_dsb_gateway_case "$tag" "true_publisher_barrier_released"
  local i n=0
  for i in $(seq 1 15); do
    sleep 10
    n=$(grep -c "SWEEP_RESULT" "$LOGDIR/$pc_log" 2>/dev/null || true)
    [ "$n" -ge 1 ] && break
  done
  cleanup_dsb || return 1
  pull "$BOARD_A" "$gw_log" || return 1
  pull "$BOARD_B" "$pub_log" || return 1
  local bad=0
  [ "$n" -ge 1 ] || { echo "   PC sub produced no SWEEP_RESULT"; bad=1; }
  assert_sweep_pass "$LOGDIR/$pc_log" "$ds03_count" || bad=1
  require_current_log "$LOGDIR/$gw_log" || bad=1
  require_current_log "$LOGDIR/$pub_log" || bad=1
  grep -Fq "SWEEP-PUB-DONE size=1024 count=$ds03_count" "$LOGDIR/$pub_log" \
    || { echo "   intended board-B publisher did not complete its exact $ds03_count-sample offer"; bad=1; }
  grep -Fq 'SWEEP-PUB-ALL-DONE' "$LOGDIR/$pub_log" \
    || { echo "   intended board-B publisher did not report completion"; bad=1; }
  grep -Fq 'SWEEP-PUB-ERROR' "$LOGDIR/$pub_log" \
    && { echo "   intended board-B publisher logged an error"; bad=1; }
  grep -Eq "^SWEEP-PUB-MATCHED local_subs=[1-9][0-9]* elapsed_ms=[0-9]+$" "$LOGDIR/$pub_log" \
    || { echo "   intended board-B publisher never proved its local subscription match"; bad=1; }
  grep -Fqx "SWEEP-PUB-BARRIER-READY token=$barrier_token local_subs=1" "$LOGDIR/$pub_log" \
    || grep -Eq "^SWEEP-PUB-BARRIER-READY token=$barrier_token local_subs=[1-9][0-9]*$" "$LOGDIR/$pub_log" \
    || { echo "   intended board-B publisher never reached the release barrier"; bad=1; }
  grep -Fqx "SWEEP-PUB-BARRIER-RELEASED token=$barrier_token" "$LOGDIR/$pub_log" \
    || { echo "   intended board-B publisher was never released"; bad=1; }
  awk -v release="SWEEP-PUB-BARRIER-RELEASED token=$barrier_token" '
    $0 == release { released = 1 }
    /^SWEEP-PUB size=/ && !released { bad = 1 }
    END { exit !(released && !bad) }
  ' "$LOGDIR/$pub_log" \
    || { echo "   board-B publisher emitted DATA before the signed release barrier"; bad=1; }
  grep -q "requested=\[dsoftbus\]" "$LOGDIR/$gw_log" \
    || { echo "   gateway startup lacks requested=[dsoftbus]"; bad=1; }
  grep -q "active=\[dsoftbus(session=" "$LOGDIR/$gw_log" \
    || { echo "   gateway startup lacks active=[dsoftbus(session=...)]"; bad=1; }
  grep -q "active=\[[^]]*udp(" "$LOGDIR/$gw_log" \
    && { echo "   gateway startup shows udp active"; bad=1; }
  grep -Fq "$DS3_TOPIC final: cyclone->mdds=0 mdds->cyclone=$ds03_count" \
    "$LOGDIR/$gw_log" \
    || { echo "   gateway final counter is not cyclone->mdds=0 mdds->cyclone=$ds03_count"; bad=1; }
  # A timeout, ingress loss, or terminal forwarding failure is a hard negative
  # even if a PC subscriber happened to report N/N before the bridge stopped.
  gateway_final_m2c_is_healthy "$LOGDIR/$gw_log" \
    || { echo "   gateway final ACK/ingress counters are missing or non-zero"; bad=1; }
  local batches
  batches=$(sed -n 's/.*m2c_ack_batches=\([0-9][0-9]*\).*/\1/p' "$LOGDIR/$gw_log" | tail -1)
  if ! [[ "$batches" =~ ^[0-9]+$ ]] || (( batches < minimum_batches )); then
    echo "   gateway ACK-fence batch count is below $minimum_batches: ${batches:-missing}"; bad=1
  fi
  grep -Eq 'ACK fence failed|ACK fence timed out|terminal bridge failure|ingress loss|mdds->cyclone publish failed|mdds->cyclone forwarding thread failed|mdds_gateway: terminal executor failure' \
    "$LOGDIR/$gw_log" \
    && { echo "   gateway logged terminal forwarding failure"; bad=1; }
  gateway_alive_c2m_is_healthy "$LOGDIR/$gw_log" \
    || { echo "   gateway did not log an active Cyclone subscriber with c2m_dropped=0"; bad=1; }
  grep -Fq "cyclone domain 46, mdds domain 43" "$LOGDIR/$gw_log" \
    || { echo "   gateway did not run on the isolated Cyclone domain 46"; bad=1; }
  verdict "$verdict_id" "$bad" "$detail (topic=$DS3_TOPIC; PC depth=$ds03_pc_depth; $pc_log; $pub_log)"
}

s_ds03() {
  # Steady byte-exact DSoftBus->gateway->PC proof. The deliberate 2 Hz offer
  # and post-match settle avoid conflating ordinary DS-03 with reconnect-burst
  # throughput; that adversarial case is covered separately below.
  s_ds03_case "DS-03" "ds03" 30 2 5000 1 \
    "B->gateway(dsoftbus)->PC exact 30/30 steady path"
}

s_ds03_ack_burst() {
  # P1 regression: after the seed reader releases the DSoftBus session, the
  # true writer must first pass the bilateral control-plane barrier above. It
  # then offers 32 KEEP_LAST(10) samples at 1000 Hz without a data-plane settle.
  # This verifies exact delivery and at least four completed downstream ACK
  # fences without mislabeling the aggregate count as four full 8-sample fences.
  # It is intentionally separate from the normative DS-01..07 suite.
  s_ds03_case "DS-03-ACK-BURST" "ds03_ack_burst" 32 1000 0 4 \
    "B->gateway(dsoftbus)->PC post-match 32-sample ACK-fence burst exact 32/32"
}

s_ds04() {
  # Negative evidence: while dsoftbus-only nodes run, the scenarios must not
  # create any NEW socket in the mdds UDP port band (47811-47842), and each
  # process log must name dsoftbus. The check is snapshot-diffed: board A's
  # pre-existing round-3 `ros2 topic echo` (PID 22646) already holds a band
  # socket and is not ours to kill, so we diff against a pre-launch baseline
  # instead of demanding an empty band.
  cleanup_dsb || return 1
  local b hits tag bad=0
  # Both serials share the "3e01ff55" prefix — key snapshots by serial SUFFIX.
  for b in "$BOARD_A" "$BOARD_B"; do
    tag=${b: -6}
    scan_udp_band "$b" "$tag" base || bad=1
  done
  launch "$BOARD_B" "$DSB_ENVS" "\$ROS2_LISTENER" ds04_b_listener.log || return
  launch "$BOARD_A" "$DSB_ENVS" "\$ROS2_TALKER" ds04_a_talker.log || return
  sleep 12
  for b in "$BOARD_A" "$BOARD_B"; do
    tag=${b: -6}
    scan_udp_band "$b" "$tag" now || bad=1
    if ! grep -Fqx 'UDP_SCAN_OK' "$LOGDIR/ds04_udp_base_$tag.txt" \
      || ! grep -Fqx 'UDP_SCAN_OK' "$LOGDIR/ds04_udp_now_$tag.txt"; then
      bad=1
      continue
    fi
    hits=$(comm -13 \
      <(grep -Fvx 'UDP_SCAN_OK' "$LOGDIR/ds04_udp_base_$tag.txt") \
      <(grep -Fvx 'UDP_SCAN_OK' "$LOGDIR/ds04_udp_now_$tag.txt") | wc -l)
    if [ "${hits:-0}" -ne 0 ]; then
      echo "   board $tag: $hits NEW socket(s) in mdds UDP band 0xBAC3-0xBAE2:"
      comm -13 \
        <(grep -Fvx 'UDP_SCAN_OK' "$LOGDIR/ds04_udp_base_$tag.txt") \
        <(grep -Fvx 'UDP_SCAN_OK' "$LOGDIR/ds04_udp_now_$tag.txt") | sed 's/^/     /'
      bad=1
    fi
  done
  pull "$BOARD_A" ds04_a_talker.log || return 1
  pull "$BOARD_B" ds04_b_listener.log || return 1
  # rmw init logs `mdds transports active: dsoftbus(session=... peers=N)`:
  # dsoftbus must be present, udp must not appear among the active backends.
  local f
  for f in ds04_a_talker.log ds04_b_listener.log; do
    require_current_log "$LOGDIR/$f" || bad=1
    grep -q "mdds transports active:.*dsoftbus(" "$LOGDIR/$f" \
      || { echo "   $f: no active dsoftbus backend line"; bad=1; }
    grep -q "mdds transports active:.*udp(" "$LOGDIR/$f" \
      && { echo "   $f: udp listed among active backends"; bad=1; }
  done
  cleanup_dsb || return 1
  verdict "DS-04" $bad "no mdds UDP sockets; dsoftbus named active on both boards"
}

s_ds05() {
  # Fault injection. The gate's domain-43 DSoftBus session
  # (com.kaihong.mdds.d43) is
  # held by a talker; a dsoftbus-only gateway on the same board/domain must
  # then FAIL CLOSED: non-zero exit + requested/active/failed report + raw
  # DSoftBus error code — even though UDP is up and would otherwise work.
  cleanup_dsb || return 1
  launch "$BOARD_A" "$DSB_ENVS" "\$ROS2_TALKER" ds05_holder.log || return
  sleep 5
  # The conflict injection is only meaningful if the holder actually owns the
  # domain's dsoftbus session; otherwise a gateway failure proves nothing.
  local bad=0
  pull "$BOARD_A" ds05_holder.log || return 1
  require_current_log "$LOGDIR/ds05_holder.log" || bad=1
  grep -q "mdds transports active:.*dsoftbus(" "$LOGDIR/ds05_holder.log" \
    || { echo "   holder is NOT up on dsoftbus; conflict precondition missing"; bad=1; }
  # Run both expected startup failures through the ordinary signed launcher.
  # They are direct payloads (not timeout/supervisor children), so their record
  # is the actual gateway/listener PID and an HDC interruption remains exactly
  # recoverable by cleanup_dsb.  We prove the immediate fatal branch by its
  # exact fatal log and a verified direct-process exit; main.cpp maps that
  # branch to return 1, rather than trusting an HDC shell exit status.
  launch "$BOARD_A" "$GWENVS" \
    "$DEVICE_DIR/lib/mdds_gateway/mdds_gateway -c $DEVICE_DIR/mdds_e2e/mdds_gateway_dsb.conf" \
    ds05_gw.log || return 1
  local gw_pid="$LAST_PID"
  if ! wait_recorded_exit "$BOARD_A" "$gw_pid" 20; then
    echo "   FAIL-CLOSED VIOLATION: conflicting gateway remained alive" >&2
    bad=1
  fi
  pull "$BOARD_A" ds05_gw.log || return 1
  require_current_log "$LOGDIR/ds05_gw.log" || bad=1
  echo "   gateway direct-exit=verified (pid=$gw_pid)"
  grep -Fq "mdds_gateway: mdds participant failed to start:" "$LOGDIR/ds05_gw.log" \
    || { echo "   gateway log lacks participant-start failure anchor"; bad=1; }
  grep -Fq "requested=[dsoftbus]" "$LOGDIR/ds05_gw.log" \
    || { echo "   gateway log lacks requested=[dsoftbus]"; bad=1; }
  grep -Fq "active=[]" "$LOGDIR/ds05_gw.log" \
    || { echo "   gateway log lacks active=[]"; bad=1; }
  grep -Fq "failed=[dsoftbus:" "$LOGDIR/ds05_gw.log" \
    || { echo "   gateway log lacks failed=[dsoftbus:...]"; bad=1; }
  # ...and an unknown transport value must be rejected the same way. `env`
  # keeps this a single direct exec-able payload for the launcher.
  launch "$BOARD_A" "$RENVS" "env MDDS_TRANSPORT=bogus \$ROS2_LISTENER" ds05_bogus.log || return 1
  local bogus_pid="$LAST_PID"
  if ! wait_recorded_exit "$BOARD_A" "$bogus_pid" 20; then
    echo "   FAIL-CLOSED VIOLATION: unknown-transport listener remained alive" >&2
    bad=1
  fi
  pull "$BOARD_A" ds05_bogus.log || return 1
  require_current_log "$LOGDIR/ds05_bogus.log" || bad=1
  echo "   unknown-transport direct-exit=verified (pid=$bogus_pid)"
  grep -Fq "MDDS_TRANSPORT: unknown transport 'bogus'" "$LOGDIR/ds05_bogus.log" \
    || { echo "   bogus-transport log lacks exact parser error"; bad=1; }
  cleanup_dsb || return 1
  verdict "DS-05" $bad "fail-closed: session-conflict gateway + unknown transport both abort"
}

s_ds06() {
  # Call-chain evidence: the DS-01/02/03 logs (all launched with MDDS_DEBUG=1)
  # must contain the real DSoftBus Socket/Bytes call chain. Requires ds01+ds02
  # (and ideally ds03) to have run in this invocation.
  local bad=0 log
  local current_logs=()
  local required_logs=(ds01_sub ds01_pub ds02_sub ds02_pub)
  if [[ " $REQUESTED_SCENARIOS " == *" ds03 "* ]]; then
    required_logs+=(ds03_gw)
  fi
  for log in "${required_logs[@]}"; do
    if ! require_current_log "$LOGDIR/$log.log"; then
      bad=1
      continue
    fi
    current_logs+=("$LOGDIR/$log.log")
    grep -q "Socket(name=" "$LOGDIR/$log.log" || { echo "   $log: no Socket(name=)"; bad=1; }
    grep -q "Listen(fd=" "$LOGDIR/$log.log"  || { echo "   $log: no Listen(fd=)"; bad=1; }
  done
  # data-path markers appear on whichever sides actually sent/received
  if [ ${#current_logs[@]} -eq 0 ]; then
    echo "   no current-invocation DS logs; run ds01 ds02 first"; bad=1
  elif ! grep -lq "SendBytes(fd=" "${current_logs[@]}"; then
    echo "   no SendBytes(fd=) in any DS log"; bad=1
  fi
  if [ ${#current_logs[@]} -gt 0 ] && ! grep -lq "OnBytes(fd=" "${current_logs[@]}"; then
    echo "   no OnBytes(fd=) in any DS log"; bad=1
  fi
  if [ ${#current_logs[@]} -gt 0 ] && ! grep -lq "BindAsync(fd=\|OnBind(fd=" "${current_logs[@]}"; then
    echo "   no BindAsync/OnBind in any DS log"; bad=1
  fi
  [ ${#current_logs[@]} -eq ${#required_logs[@]} ] \
    || { echo "   missing required current DS logs; run ds01 and ds02 first"; bad=1; }
  verdict "DS-06" $bad "DSoftBus Socket/Listen/BindAsync/OnBind/SendBytes/OnBytes in logs"
}

s_ds07() {
  # DSoftBus-only fragmentation/reassembly sweep. Keep it on a unique topic
  # and run a deliberately low rate: the acceptance is integrity/order, not
  # a throughput benchmark, and an 8 MiB sample spans thousands of Bytes
  # frames on conservative DSoftBus links.
  cleanup_dsb || return 1
  local sizes="1024,4096,65536,262144,1048576,4194304,8388608"
  local count=8
  launch "$BOARD_B" "$DSB_ENVS" \
    "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode sub --topic /mdds_dsb_large --sizes $sizes --count $count --idle-timeout 120" \
    ds07_sub.log || return
  sleep 5
  launch "$BOARD_A" "$DSB_ENVS" \
    "python3.12 $DEVICE_DIR/mdds_e2e/board_sweep.py --mode pub --topic /mdds_dsb_large --sizes $sizes --count $count --rate 1 --wait-match --flush-ms 90000" \
    ds07_pub.log || return
  poll_sweep_result "$BOARD_B" ds07_sub.log 45
  local polled=$?
  kill_tracked || return 1
  pull "$BOARD_B" ds07_sub.log || return 1
  pull "$BOARD_A" ds07_pub.log || return 1
  local bad=$polled
  assert_sweep_all_pass "$LOGDIR/ds07_sub.log" "$count" \
    1024 4096 65536 262144 1048576 4194304 8388608 || bad=1
  verdict "DS-07" $bad "A->B DSoftBus DATA_FRAG exact 1KiB-8MiB (ds07_sub.log)"
}

s_ds08() {
  # The launcher is the only entity that can authoritatively associate a
  # remote payload with this run.  Exercise both sides of its pending-record
  # contract without invoking ROS or a broad process search:
  #
  #  A) a guard that cannot persist its PID:start record must prove it never
  #     execs the marker payload, then be retired through its signed pre-exec
  #     status;
  #  B) a guard that DID persist a valid record but whose host-side identity
  #     recovery reads and stdout token are deliberately unavailable must
  #     remain pending and be stopped only after cleanup verifies that exact
  #     PID and proc-start tuple.
  cleanup_dsb || return 1
  local bad=0 saved_fail="$FAIL_LAUNCH_RECORD_WRITE"
  local saved_drop_all="$DROP_ALL_RECORD_READS"
  local saved_drop_first="$DROP_FIRST_RECORD_READ"
  local saved_suppress="$SUPPRESS_LAUNCH_TOKEN"
  local saved_fault_log="$LAUNCH_FAULT_LOG"
  local saved_pending_cleanup_log="$PENDING_CLEANUP_LOG"
  local saved_ds08_assertions_log="$DS08_ASSERTIONS_LOG"
  local write_fail_marker="$REMOTE_LOGDIR/ds08a_payload_ran"
  local write_fail_record="$REMOTE_LOGDIR/launch/ds08a_payload.log.$RUN_NONCE.pid"
  local expected_record="$REMOTE_LOGDIR/launch/ds08b_payload.log.$RUN_NONCE.pid"
  local attempt expected_fault_line
  DS08_ASSERTIONS_LOG="$LOGDIR/ds08_assertions.txt"
  LAUNCH_FAULT_LOG="$LOGDIR/ds08_fault_injection.txt"
  PENDING_CLEANUP_LOG="$LOGDIR/ds08_pending_cleanup_records.txt"
  if ! : > "$DS08_ASSERTIONS_LOG" || ! : > "$LAUNCH_FAULT_LOG" || ! : > "$PENDING_CLEANUP_LOG"; then
    echo "ERROR: cannot create fresh DS-08 evidence transcripts" >&2
    LAUNCH_FAULT_LOG="$saved_fault_log"
    PENDING_CLEANUP_LOG="$saved_pending_cleanup_log"
    DS08_ASSERTIONS_LOG="$saved_ds08_assertions_log"
    return 1
  fi

  # DS-08A: the status path is the only safe proof of a pre-exec record-write
  # fault.  The marker would exist if the guard ever reached the payload.
  FAIL_LAUNCH_RECORD_WRITE=1
  DROP_ALL_RECORD_READS=0
  DROP_FIRST_RECORD_READ=0
  SUPPRESS_LAUNCH_TOKEN=0
  export MDDS_TEST_FAIL_LAUNCH_RECORD_WRITE="$FAIL_LAUNCH_RECORD_WRITE"
  export MDDS_TEST_DROP_ALL_RECORD_READS="$DROP_ALL_RECORD_READS"
  export MDDS_TEST_DROP_FIRST_RECORD_READ="$DROP_FIRST_RECORD_READ"
  export MDDS_TEST_SUPPRESS_LAUNCH_TOKEN="$SUPPRESS_LAUNCH_TOKEN"
  if launch "$BOARD_A" "" "touch $write_fail_marker" ds08a_payload.log; then
    echo "DS08A unexpected-launch-success" | tee -a "$DS08_ASSERTIONS_LOG"
    bad=1
  else
    echo "DS08A launch-rejected-as-expected" | tee -a "$DS08_ASSERTIONS_LOG"
  fi
  if ! cleanup_pending_launch_records; then
    echo "DS08A pending-cleanup-failed" | tee -a "$DS08_ASSERTIONS_LOG"
    bad=1
  fi
  if [ "$(grep -Fxc "board=$BOARD_A remote_record=$write_fail_record result=PENDING_RECORD_WRITE_FAILED_PREEXEC" "$PENDING_CLEANUP_LOG" 2>/dev/null || true)" -ne 1 ]; then
    echo "DS08A missing-exact-preexec-status" | tee -a "$DS08_ASSERTIONS_LOG"
    bad=1
  fi
  if [[ "$(shell "$BOARD_A" "if test ! -e '$write_fail_marker'; then echo DS08A_PAYLOAD_ABSENT; else echo DS08A_PAYLOAD_RAN; fi" | tr -d '\r\n')" != "DS08A_PAYLOAD_ABSENT" ]]; then
    echo "DS08A payload-ran-after-record-write-failure" | tee -a "$DS08_ASSERTIONS_LOG"
    bad=1
  else
    echo "DS08A payload-absent" | tee -a "$DS08_ASSERTIONS_LOG"
  fi

  # DS-08B: hide stdout and every host-side persistent-record read.  The
  # sleeper remains live until cleanup discovers the record itself, validates
  # PID:start, and stops that exact process.
  FAIL_LAUNCH_RECORD_WRITE=0
  DROP_ALL_RECORD_READS=1
  DROP_FIRST_RECORD_READ=0
  SUPPRESS_LAUNCH_TOKEN=1
  export MDDS_TEST_FAIL_LAUNCH_RECORD_WRITE="$FAIL_LAUNCH_RECORD_WRITE"
  export MDDS_TEST_DROP_ALL_RECORD_READS="$DROP_ALL_RECORD_READS"
  export MDDS_TEST_DROP_FIRST_RECORD_READ="$DROP_FIRST_RECORD_READ"
  export MDDS_TEST_SUPPRESS_LAUNCH_TOKEN="$SUPPRESS_LAUNCH_TOKEN"
  if launch "$BOARD_B" "" "sleep 120" ds08b_payload.log; then
    echo "DS08B unexpected-launch-success" | tee -a "$DS08_ASSERTIONS_LOG"
    bad=1
  else
    echo "DS08B launch-read-loss-as-expected" | tee -a "$DS08_ASSERTIONS_LOG"
  fi
  for attempt in 1 2 3; do
    expected_fault_line="event=inject-drop-all-remote-record-reads board=$BOARD_B record=$expected_record attempt=$attempt"
    if [ "$(grep -Fxc "$expected_fault_line" "$LAUNCH_FAULT_LOG" 2>/dev/null || true)" -ne 1 ]; then
      echo "DS08B missing-exact-dropped-record-read attempt=$attempt" | tee -a "$DS08_ASSERTIONS_LOG"
      bad=1
    fi
  done
  if [ "$(grep -Fc "event=inject-drop-all-remote-record-reads board=$BOARD_B record=$expected_record" "$LAUNCH_FAULT_LOG" 2>/dev/null || true)" -ne 3 ]; then
    echo "DS08B unexpected-dropped-record-read-count" | tee -a "$DS08_ASSERTIONS_LOG"
    bad=1
  fi
  if ! cleanup_pending_launch_records; then
    echo "DS08B pending-cleanup-failed" | tee -a "$DS08_ASSERTIONS_LOG"
    bad=1
  fi
  if [ "$(grep -Fxc "board=$BOARD_B remote_record=$expected_record result=PENDING_RECORD_STOPPED" "$PENDING_CLEANUP_LOG" 2>/dev/null || true)" -ne 1 ]; then
    echo "DS08B missing-exact-identity-fenced-stop" | tee -a "$DS08_ASSERTIONS_LOG"
    bad=1
  else
    echo "DS08B identity-fenced-stop" | tee -a "$DS08_ASSERTIONS_LOG"
  fi
  # This must be empty after each directed subcase.  Leaving a record here
  # would make the EXIT trap retain the activity locks rather than claiming a
  # clean gate completion.
  if [ -n "$PENDING_LAUNCH_RECORDS" ] || [ -n "$TRACKED" ]; then
    echo "DS08 residual-owned-records" | tee -a "$DS08_ASSERTIONS_LOG"
    bad=1
  fi
  if [[ " ${ACTIVITY_LOCKED_BOARDS[*]} " != *" $BOARD_A "* || " ${ACTIVITY_LOCKED_BOARDS[*]} " != *" $BOARD_B "* ]]; then
    echo "DS08 missing-owned-activity-lock" | tee -a "$DS08_ASSERTIONS_LOG"
    bad=1
  else
    # Do not release and reacquire here: another scenario may follow DS-08.
    # The EXIT handler owns the one final release transaction and appends its
    # PASS/FAIL result below, preserving the lock across the whole invocation.
    echo "DS08 activity-lock-release=DEFERRED_TO_EXIT_TRAP" | tee -a "$DS08_ASSERTIONS_LOG"
  fi
  grep -Eq 'PENDING_(INTENT_INVALID|CANCEL_CONFLICT|CANCEL_WRITE_FAILED|STATUS_INVALID|RECORD_INVALID|RECORD_REUSED|RECORD_SIGNAL_FAILED|RECORD_LIVE|RECORD_UNRESOLVED)' \
    "$PENDING_CLEANUP_LOG" \
    && { echo "DS08 unsafe-pending-cleanup-result" | tee -a "$DS08_ASSERTIONS_LOG"; bad=1; }
  FAIL_LAUNCH_RECORD_WRITE="$saved_fail"
  DROP_ALL_RECORD_READS="$saved_drop_all"
  DROP_FIRST_RECORD_READ="$saved_drop_first"
  SUPPRESS_LAUNCH_TOKEN="$saved_suppress"
  export MDDS_TEST_FAIL_LAUNCH_RECORD_WRITE="$FAIL_LAUNCH_RECORD_WRITE"
  export MDDS_TEST_DROP_ALL_RECORD_READS="$DROP_ALL_RECORD_READS"
  export MDDS_TEST_DROP_FIRST_RECORD_READ="$DROP_FIRST_RECORD_READ"
  export MDDS_TEST_SUPPRESS_LAUNCH_TOKEN="$SUPPRESS_LAUNCH_TOKEN"
  LAUNCH_FAULT_LOG="$saved_fault_log"
  PENDING_CLEANUP_LOG="$saved_pending_cleanup_log"
  verdict "DS-08" $bad "pending launch cleanup: preexec write failure + hidden valid PID:start record"
}

# --- main --------------------------------------------------------------------

cleanup_dsb() {
  local rc=0
  cleanup_pending_launch_records || rc=1
  kill_tracked || rc=1
  pc_cleanup_pending_record || rc=1
  pc_cleanup || rc=1
  return "$rc"
}

record_ds08_lock_release() { # <PASS|FAIL|NOT_ATTEMPTED_*>
  [ -n "$DS08_ASSERTIONS_LOG" ] || return 0
  printf 'DS08 activity-lock-release=%s\n' "$1" >> "$DS08_ASSERTIONS_LOG"
}

on_dsb_exit() {
  local rc=$? cleanup_ok=1
  trap - EXIT
  trap '' INT TERM HUP
  if ! cleanup_dsb; then
    echo "ERROR: DSoftBus gate cleanup was incomplete; retained identity records prevent an unsafe kill" >&2
    cleanup_ok=0
    rc=1
    if ! record_ds08_lock_release NOT_ATTEMPTED_CLEANUP_FAILED; then
      echo "ERROR: cannot record DS-08 activity-lock cleanup failure" >&2
      rc=1
    fi
  fi
  if (( cleanup_ok == 1 )); then
    if ! release_activity_locks; then
      echo "ERROR: DSoftBus activity-lock cleanup was incomplete; a fail-closed lock remains" >&2
      rc=1
      if ! record_ds08_lock_release FAIL; then
        echo "ERROR: cannot record DS-08 activity-lock release failure" >&2
        rc=1
      fi
    elif ! record_ds08_lock_release PASS; then
      echo "ERROR: cannot record DS-08 activity-lock release success" >&2
      rc=1
    fi
  else
    echo "ERROR: retaining DSoftBus activity locks because owned payload stop was not proven" >&2
  fi
  exit "$rc"
}

on_dsb_signal() {
  local signal_rc="$1" cleanup_ok=1
  trap - EXIT
  trap '' INT TERM HUP
  if ! cleanup_dsb; then
    echo "ERROR: cleanup after signal was incomplete; records were retained" >&2
    cleanup_ok=0
  fi
  if ! record_ds08_lock_release NOT_ATTEMPTED_SIGNAL; then
    echo "ERROR: cannot record DS-08 signal-path activity-lock outcome" >&2
  fi
  if (( cleanup_ok == 1 )); then
    echo "ERROR: retaining DSoftBus activity locks after signal despite successful cleanup; explicit operator recovery is required" >&2
  else
    echo "ERROR: retaining DSoftBus activity locks after signal because owned payload stop was not proven" >&2
  fi
  exit "$signal_rc"
}

trap on_dsb_exit EXIT
trap 'on_dsb_signal 130' INT
trap 'on_dsb_signal 143' TERM
trap 'on_dsb_signal 129' HUP

if [ $# -eq 0 ] || [ "$1" = all ]; then
  # Keep the normative suite and the directed ACK-fence regression together in
  # the default final invocation.  Reports still identify the latter
  # separately as DS-03-ACK-BURST rather than relabeling DS-01..07.
  set -- ds01 ds02 ds03 ds03_ack_burst ds04 ds05 ds06 ds07 ds08
fi
REQUESTED_SCENARIOS=" $* "
# DS-03 hands a leading-slash ROS topic from Git Bash through native
# PowerShell/cmd.exe.  Validate the exact shared conversion helper before we
# acquire a board lock or deploy anything, so host argument mutation fails
# locally instead of looking like a gateway discovery failure later.
if [[ "$REQUESTED_SCENARIOS" == *" ds03 "* || "$REQUESTED_SCENARIOS" == *" ds03_ack_burst "* ]]; then
  if ! bash "$PWD/scripts/test_msys_pc_topic_env.sh"; then
    echo "ERROR: DS-03 PC topic launch preflight failed" >&2
    exit 1
  fi
fi
if ! acquire_activity_locks; then
  echo "ERROR: DSoftBus gate did not start because the shared MDDS activity lock is unavailable" >&2
  exit 1
fi
if ! push_dsb_files; then
  echo "== mdds dsoftbus gate summary: 0 passed, 1 failed =="
  echo "   FAIL deployment"
  exit 1
fi
if ! verify_final_artifacts; then
  echo "== mdds dsoftbus gate summary: 0 passed, 1 failed =="
  echo "   FAIL final-artifact hash verification (see $LOGDIR/artifact_hashes.txt)"
  exit 1
fi
for sc in "$@"; do
  echo "== scenario: $sc =="
  if ! declare -F "s_$sc" >/dev/null; then
    echo "   unknown scenario: $sc"
    fail=$((fail+1)); failed_ids+=("$sc")
    continue
  fi
  if ! "s_$sc"; then
    local_failure_id="DS-${sc#ds}"
    # Directed scenarios own a canonical external verdict ID that is not a
    # mechanical dsXX spelling.  Preserve it even on an early setup/barrier
    # error, otherwise manifests would split one failed gate into a new ID.
    [ "$sc" = ds03_ack_burst ] && local_failure_id="DS-03-ACK-BURST"
    echo "   (scenario $sc errored)"
    verdict "$local_failure_id" 1 "scenario execution error"
    if ! cleanup_dsb; then
      echo "ERROR: aborting later DSoftBus scenarios because owned-process cleanup is incomplete" >&2
      break
    fi
  fi
done

if ! cleanup_dsb; then
  verdict "DS-CLEANUP" 1 "owned-process cleanup incomplete (see retained identity errors)"
fi
echo
echo "== mdds dsoftbus gate summary: $pass passed, $fail failed =="
[ ${#failed_ids[@]} -eq 0 ] || printf '   FAIL %s\n' "${failed_ids[@]}"
[ "$fail" -eq 0 ]
