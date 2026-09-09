#!/usr/bin/env bash
# Run-owned board process helper for the legacy E2E/L4/CLI orchestrators.
# The caller must define DEVICE_DIR and shell(board, command).

declare -a ROS2_OWNED_TRACKED=()
declare -a ROS2_OWNED_LOCK_BOARDS=()
ROS2_OWNED_SEQUENCE=0
ROS2_OWNED_REMOTE_DIR=""
ROS2_OWNED_LABEL=""
ROS2_OWNED_RUN_ID=""

ros2_owned_quote() {
  printf "'%s'" "${1//\'/\'\\\'\'}"
}

ros2_owned_release_one_lock() { # <board> <exact-owner>
  local out
  out="$(shell "$1" "lock='$DEVICE_DIR/.ros2-activity-lock'; if test -d \"\$lock\" && test ! -L \"\$lock\" && test -f \"\$lock/owner\" && test ! -L \"\$lock/owner\" && grep -Fqx '$2' \"\$lock/owner\" && test \"\$(find \"\$lock\" -mindepth 1 -maxdepth 1)\" = \"\$lock/owner\"; then rm -f \"\$lock/owner\" && rmdir \"\$lock\" && printf ROS2_OWNED_LOCK_RELEASED; else printf ROS2_OWNED_LOCK_NOT_OWNED; fi" | tr -d '\r\n')"
  [ "$out" = ROS2_OWNED_LOCK_RELEASED ]
}

ros2_owned_init() { # <label> <board>...
  local label="$1" board out root owner
  local -A seen_boards=()
  shift
  [[ "$label" =~ ^[A-Za-z0-9_.-]+$ ]] || return 2
  ROS2_OWNED_LABEL="$label"
  ROS2_OWNED_RUN_ID="${ROS2_RUN_ID:-${label}_$(date +%Y%m%dT%H%M%S)_${RANDOM}_${RANDOM}_$$}"
  [[ "$ROS2_OWNED_RUN_ID" =~ ^[A-Za-z0-9_.-]+$ ]] || {
    echo "ERROR: ROS2_RUN_ID must contain only A-Za-z0-9_.-" >&2
    return 2
  }
  ROS2_OWNED_REMOTE_DIR="$DEVICE_DIR/.ros2-owned-runs/$ROS2_OWNED_RUN_ID"
  owner="ROS2_ACTIVITY_LOCK MODE=TEST RUN_ID=$ROS2_OWNED_RUN_ID OWNER=$label"
  for board in "$@"; do
    if ! [[ "$board" =~ ^[A-Za-z0-9_.-]+$ ]] || [[ -n "${seen_boards[$board]+present}" ]]; then
      echo "ERROR: board list contains an unsafe or duplicate target: $board" >&2
      return 2
    fi
    seen_boards["$board"]=1
  done
  for board in "$@"; do
    root="$ROS2_OWNED_REMOTE_DIR"
    out="$(shell "$board" "if (umask 077; mkdir '$DEVICE_DIR/.ros2-activity-lock') 2>/dev/null; then if (umask 077; set -C; printf '%s\\n' '$owner' > '$DEVICE_DIR/.ros2-activity-lock/owner') 2>/dev/null && grep -Fqx '$owner' '$DEVICE_DIR/.ros2-activity-lock/owner' && mkdir -p '$DEVICE_DIR/.ros2-owned-runs' && (umask 077; mkdir '$root') 2>/dev/null && (umask 077; set -C; printf '%s\\n' 'ROS2_RUN_OWNER RUN_ID=$ROS2_OWNED_RUN_ID LABEL=$label' > '$root/owner') 2>/dev/null; then printf ROS2_OWNED_READY; else printf ROS2_OWNED_SETUP_FAILED; fi; else printf ROS2_OWNED_LOCK_BUSY; fi" | tr -d '\r\n')"
    if [ "$out" != ROS2_OWNED_READY ]; then
      echo "ERROR: cannot establish run ownership on $board: ${out:-NO_MARKER}" >&2
      # The remote transaction may have written this exact activity owner and
      # then failed while creating its run directory.  Attempt an
      # owner-compared release for the current board before unwinding earlier
      # boards. A foreign/busy lock never matches and is preserved.
      ros2_owned_release_one_lock "$board" "$owner" || true
      ros2_owned_release_locks || true
      return 1
    fi
    ROS2_OWNED_LOCK_BOARDS+=("$board")
  done
}

ros2_owned_launch() { # <board> <env-prefix> <payload> <log-name>
  local board="$1" env_prefix="$2" payload="$3" log="$4"
  local tag record log_path guard out line pid start expected owner_line
  [[ "$log" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ && "$payload" != *$'\n'* ]] || {
    echo "ERROR: unsafe owned launch arguments: $log" >&2
    return 2
  }
  ROS2_OWNED_SEQUENCE=$((ROS2_OWNED_SEQUENCE + 1))
  tag="${ROS2_OWNED_SEQUENCE}_${RANDOM}_$$"
  record="$ROS2_OWNED_REMOTE_DIR/${log}.${tag}.pid"
  log_path="$ROS2_OWNED_REMOTE_DIR/$log"
  owner_line="ROS2_RUN_OWNER RUN_ID=$ROS2_OWNED_RUN_ID LABEL=$ROS2_OWNED_LABEL"
  guard='record=$1
log_path=$2
owner_path=$3
owner_line=$4
run_id=$5
tag=$6
env_prefix=$7
payload=$8
test -f "$owner_path" && test ! -L "$owner_path" && grep -Fqx "$owner_line" "$owner_path" || exit 70
test ! -e "$record" && test ! -L "$record" && test ! -e "$log_path" && test ! -L "$log_path" || exit 70
pid=$$
start=$(cut -d " " -f22 /proc/$$/stat 2>/dev/null)
case "$start" in ""|*[!0-9]*) exit 70 ;; esac
line="ROS2_OWNED_PROCESS RUN_ID=$run_id TAG=$tag PID=$pid START=$start"
(set -C; umask 077; printf "%s\n" "$line" > "$record") 2>/dev/null || exit 70
printf "ROS2_OWNED_START RUN_ID=%s TAG=%s PID=%s START=%s\n" "$run_id" "$tag" "$pid" "$start" > "$log_path" || exit 70
exec sh -c "$env_prefix exec $payload" >> "$log_path" 2>&1 < /dev/null
'
  shell "$board" "nohup sh -c $(ros2_owned_quote "$guard") sh $(ros2_owned_quote "$record") $(ros2_owned_quote "$log_path") $(ros2_owned_quote "$ROS2_OWNED_REMOTE_DIR/owner") $(ros2_owned_quote "$owner_line") $(ros2_owned_quote "$ROS2_OWNED_RUN_ID") $(ros2_owned_quote "$tag") $(ros2_owned_quote "$env_prefix") $(ros2_owned_quote "$payload") </dev/null >/dev/null 2>&1 &" >/dev/null 2>&1 || true
  line=""
  for _ in $(seq 1 20); do
    out="$(shell "$board" "if test -f '$record' && test ! -L '$record'; then cat '$record'; fi" || true)"
    line="$(printf '%s' "$out" | tr -d '\r\n')"
    [[ "$line" =~ ^ROS2_OWNED_PROCESS\ RUN_ID=$ROS2_OWNED_RUN_ID\ TAG=$tag\ PID=([0-9]+)\ START=([0-9]+)$ ]] && break
    sleep 0.2
  done
  if ! [[ "$line" =~ ^ROS2_OWNED_PROCESS\ RUN_ID=$ROS2_OWNED_RUN_ID\ TAG=$tag\ PID=([0-9]+)\ START=([0-9]+)$ ]]; then
    echo "ERROR: no exact run-owned PID record for $board/$log" >&2
    return 1
  fi
  pid="${BASH_REMATCH[1]}"
  start="${BASH_REMATCH[2]}"
  expected="ROS2_OWNED_PROCESS RUN_ID=$ROS2_OWNED_RUN_ID TAG=$tag PID=$pid START=$start"
  ROS2_OWNED_TRACKED+=("$board|$pid|$start|$record|$tag|$log")
  printf 'ROS2_OWNED_LAUNCH board=%s pid=%s start=%s tag=%s log=%s\n' \
    "$board" "$pid" "$start" "$tag" "$log"
}

ros2_owned_stop_entry() { # board|pid|start|record|tag|log
  local entry="$1" board pid start record tag log expected out
  IFS='|' read -r board pid start record tag log <<< "$entry"
  expected="ROS2_OWNED_PROCESS RUN_ID=$ROS2_OWNED_RUN_ID TAG=$tag PID=$pid START=$start"
  out="$(shell "$board" "if ! test -f '$record' || test -L '$record' || ! grep -Fqx '$expected' '$record'; then printf ROS2_OWNED_RECORD_INVALID; elif ! test -r /proc/$pid/stat; then rm -f '$record'; printf ROS2_OWNED_GONE; elif test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" != '$start'; then printf ROS2_OWNED_PID_REUSED; else kill '$pid' 2>/dev/null || true; i=0; while test \$i -lt 20 && test -r /proc/$pid/stat && test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" = '$start' && test \"\$(cut -d ' ' -f3 /proc/$pid/stat 2>/dev/null)\" != Z; do i=\$((i+1)); sleep 0.1; done; if test -r /proc/$pid/stat && test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" = '$start' && test \"\$(cut -d ' ' -f3 /proc/$pid/stat 2>/dev/null)\" != Z; then kill -9 '$pid' 2>/dev/null || true; sleep 1; fi; if test -r /proc/$pid/stat && test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" = '$start' && test \"\$(cut -d ' ' -f3 /proc/$pid/stat 2>/dev/null)\" != Z; then printf ROS2_OWNED_STILL_LIVE; else rm -f '$record'; printf ROS2_OWNED_STOPPED; fi; fi" | tr -d '\r\n')"
  case "$out" in
    ROS2_OWNED_GONE|ROS2_OWNED_STOPPED) return 0 ;;
    *) echo "ERROR: refused/unable to stop exact owned process $board:$pid:$start ($out)" >&2; return 1 ;;
  esac
}

ros2_owned_stop_all() {
  local entry rc=0
  local -a retained=()
  for entry in "${ROS2_OWNED_TRACKED[@]}"; do
    ros2_owned_stop_entry "$entry" || { retained+=("$entry"); rc=1; }
  done
  ROS2_OWNED_TRACKED=("${retained[@]}")
  return "$rc"
}

ros2_owned_stop_log() { # <board> <log-name>
  local wanted_board="$1" wanted_log="$2" entry board pid start record tag log rc=1
  local -a kept=()
  for entry in "${ROS2_OWNED_TRACKED[@]}"; do
    IFS='|' read -r board pid start record tag log <<< "$entry"
    if [ "$board" = "$wanted_board" ] && [ "$log" = "$wanted_log" ]; then
      ros2_owned_stop_entry "$entry" && rc=0 || kept+=("$entry")
    else
      kept+=("$entry")
    fi
  done
  ROS2_OWNED_TRACKED=("${kept[@]}")
  return "$rc"
}

ros2_owned_release_locks() {
  local board owner rc=0
  local -a retained=()
  owner="ROS2_ACTIVITY_LOCK MODE=TEST RUN_ID=$ROS2_OWNED_RUN_ID OWNER=$ROS2_OWNED_LABEL"
  for board in "${ROS2_OWNED_LOCK_BOARDS[@]}"; do
    ros2_owned_release_one_lock "$board" "$owner" || { retained+=("$board"); rc=1; }
  done
  ROS2_OWNED_LOCK_BOARDS=("${retained[@]}")
  return "$rc"
}

ros2_owned_finish() {
  local rc=0
  ros2_owned_stop_all || rc=1
  if [ ${#ROS2_OWNED_TRACKED[@]} -eq 0 ]; then
    ros2_owned_release_locks || rc=1
  else
    echo "ERROR: retaining activity locks because owned processes remain unresolved" >&2
    rc=1
  fi
  return "$rc"
}
