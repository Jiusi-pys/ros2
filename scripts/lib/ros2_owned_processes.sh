#!/usr/bin/env bash
# Exact-PID lifecycle helper for generic ROS 2 board acceptance tests.
# The caller defines DEVICE_DIR and remote_shell(board, command).

declare -a ROS2_OWNED_TRACKED=()
declare -a ROS2_OWNED_LOCK_BOARDS=()
ROS2_OWNED_SEQUENCE=0
ROS2_OWNED_REMOTE_DIR=""
ROS2_OWNED_LABEL=""
ROS2_OWNED_RUN_ID=""
ROS2_OWNED_LOCK_DIR="${DEVICE_DIR%/*}/.ros2-generic-deploy.lock"

ros2_owned_quote() {
  printf "'%s'" "${1//\'/\'\\\'\'}"
}

ros2_owned_release_one_lock() {
  local board="$1" owner="$2" result
  result="$(remote_shell "$board" "lock='$ROS2_OWNED_LOCK_DIR'; if test -d \"\$lock\" && test ! -L \"\$lock\" && test -f \"\$lock/owner\" && test ! -L \"\$lock/owner\" && grep -Fqx '$owner' \"\$lock/owner\" && test \"\$(find \"\$lock\" -mindepth 1 -maxdepth 1)\" = \"\$lock/owner\"; then rm -f \"\$lock/owner\" && rmdir \"\$lock\" && printf ROS2_OWNED_LOCK_RELEASED; else printf ROS2_OWNED_LOCK_NOT_OWNED; fi" | tr -d '\r\n')"
  [[ "$result" == ROS2_OWNED_LOCK_RELEASED ]]
}

ros2_owned_init() {
  local label="$1" board root owner result
  local -A seen=()
  shift
  [[ "$label" =~ ^[A-Za-z0-9_.-]+$ ]] || return 2
  ROS2_OWNED_LABEL="$label"
  ROS2_OWNED_RUN_ID="${ROS2_RUN_ID:-${label}_$(date -u +%Y%m%dT%H%M%SZ)_${RANDOM}_${RANDOM}_$$}"
  [[ "$ROS2_OWNED_RUN_ID" =~ ^[A-Za-z0-9_.-]+$ ]] || return 2
  ROS2_OWNED_REMOTE_DIR="$DEVICE_DIR/.ros2-owned-runs/$ROS2_OWNED_RUN_ID"
  owner="ROS2_ACTIVITY_LOCK MODE=TEST RUN_ID=$ROS2_OWNED_RUN_ID OWNER=$label"
  for board in "$@"; do
    [[ "$board" =~ ^[A-Za-z0-9_.-]+$ && -z "${seen[$board]+present}" ]] || return 2
    seen["$board"]=1
  done
  for board in "$@"; do
    root="$ROS2_OWNED_REMOTE_DIR"
    result="$(remote_shell "$board" "if test -f '$DEVICE_DIR/.ros2_deploy_complete' && test ! -L '$DEVICE_DIR/.ros2_deploy_complete' && (umask 077; mkdir '$ROS2_OWNED_LOCK_DIR') 2>/dev/null; then if (umask 077; set -C; printf '%s\\n' '$owner' > '$ROS2_OWNED_LOCK_DIR/owner') 2>/dev/null && grep -Fqx '$owner' '$ROS2_OWNED_LOCK_DIR/owner' && mkdir -p '$DEVICE_DIR/.ros2-owned-runs' && (umask 077; mkdir '$root') 2>/dev/null && (umask 077; set -C; printf '%s\\n' 'ROS2_RUN_OWNER RUN_ID=$ROS2_OWNED_RUN_ID LABEL=$label' > '$root/owner') 2>/dev/null; then printf ROS2_OWNED_READY; else printf ROS2_OWNED_SETUP_FAILED; fi; else printf ROS2_OWNED_LOCK_BUSY_OR_UNVERIFIED; fi" | tr -d '\r\n')"
    if [[ "$result" != ROS2_OWNED_READY ]]; then
      echo "ERROR: cannot establish generic run ownership on $board: ${result:-NO_MARKER}" >&2
      ros2_owned_release_one_lock "$board" "$owner" || true
      ros2_owned_release_locks || true
      return 1
    fi
    ROS2_OWNED_LOCK_BOARDS+=("$board")
  done
}

ros2_owned_monitor_script() {
  cat <<'MONITOR'
guard=$1
shift
sh -c "$guard" sh "$@" &
child=$!
wait "$child"
rc=$?
log_path=$2
run_id=$5
tag=$6
test -f "$log_path" && test ! -L "$log_path" || exit 70
printf "ROS2_OWNED_EXIT RUN_ID=%s TAG=%s PID=%s RC=%s END_UTC=%s\n" "$run_id" "$tag" "$child" "$rc" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$log_path"
exit "$rc"
MONITOR
}

ros2_owned_require_successful_exit() {
  local log_path="$1" start terminal tag pid
  start="$(grep '^ROS2_OWNED_START ' "$log_path" || true)"
  [[ "$start" =~ ^ROS2_OWNED_START\ RUN_ID=$ROS2_OWNED_RUN_ID\ TAG=([A-Za-z0-9_]+)\ PID=([0-9]+)\ START=([0-9]+)$ ]] || return 1
  tag="${BASH_REMATCH[1]}"
  pid="${BASH_REMATCH[2]}"
  terminal="$(grep '^ROS2_OWNED_EXIT ' "$log_path" || true)"
  [[ "$terminal" =~ ^ROS2_OWNED_EXIT\ RUN_ID=$ROS2_OWNED_RUN_ID\ TAG=$tag\ PID=$pid\ RC=0\ END_UTC=[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$ ]]
}

ros2_owned_launch() {
  local board="$1" env_prefix="$2" payload="$3" log="$4"
  local tag record log_path owner_line guard result line pid start
  [[ "$log" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ && "$payload" != *$'\n'* ]] || return 2
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
  # The monitor stays outside the child namespace and waits for the exact
  # launched PID. Disappearance alone is not evidence of a successful exit.
  remote_shell "$board" "nohup sh -c $(ros2_owned_quote "$(ros2_owned_monitor_script)") sh $(ros2_owned_quote "$guard") $(ros2_owned_quote "$record") $(ros2_owned_quote "$log_path") $(ros2_owned_quote "$ROS2_OWNED_REMOTE_DIR/owner") $(ros2_owned_quote "$owner_line") $(ros2_owned_quote "$ROS2_OWNED_RUN_ID") $(ros2_owned_quote "$tag") $(ros2_owned_quote "$env_prefix") $(ros2_owned_quote "$payload") </dev/null >/dev/null 2>&1 &" >/dev/null 2>&1 || true
  line=""
  for _ in $(seq 1 30); do
    result="$(remote_shell "$board" "if test -f '$record' && test ! -L '$record'; then cat '$record'; fi" || true)"
    line="$(printf '%s' "$result" | tr -d '\r\n')"
    [[ "$line" =~ ^ROS2_OWNED_PROCESS\ RUN_ID=$ROS2_OWNED_RUN_ID\ TAG=$tag\ PID=([0-9]+)\ START=([0-9]+)$ ]] && break
    sleep 0.2
  done
  [[ "$line" =~ ^ROS2_OWNED_PROCESS\ RUN_ID=$ROS2_OWNED_RUN_ID\ TAG=$tag\ PID=([0-9]+)\ START=([0-9]+)$ ]] || {
    echo "ERROR: no exact generic run PID record for $board/$log" >&2
    return 1
  }
  pid="${BASH_REMATCH[1]}"
  start="${BASH_REMATCH[2]}"
  ROS2_OWNED_TRACKED+=("$board|$pid|$start|$record|$tag|$log")
  printf 'ROS2_OWNED_LAUNCH board=%s pid=%s start=%s tag=%s log=%s\n' "$board" "$pid" "$start" "$tag" "$log"
}

ros2_owned_stop_entry() {
  local entry="$1" board pid start record tag log expected result
  IFS='|' read -r board pid start record tag log <<< "$entry"
  expected="ROS2_OWNED_PROCESS RUN_ID=$ROS2_OWNED_RUN_ID TAG=$tag PID=$pid START=$start"
  result="$(remote_shell "$board" "if ! test -f '$record' || test -L '$record' || ! grep -Fqx '$expected' '$record'; then printf ROS2_OWNED_RECORD_INVALID; elif ! test -r /proc/$pid/stat; then rm -f '$record'; printf ROS2_OWNED_GONE; elif test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" != '$start'; then printf ROS2_OWNED_PID_REUSED; else kill '$pid' 2>/dev/null || true; i=0; while test \$i -lt 30 && test -r /proc/$pid/stat && test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" = '$start' && test \"\$(cut -d ' ' -f3 /proc/$pid/stat 2>/dev/null)\" != Z; do i=\$((i+1)); sleep 0.1; done; if test -r /proc/$pid/stat && test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" = '$start' && test \"\$(cut -d ' ' -f3 /proc/$pid/stat 2>/dev/null)\" != Z; then kill -9 '$pid' 2>/dev/null || true; sleep 1; fi; if test -r /proc/$pid/stat && test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" = '$start' && test \"\$(cut -d ' ' -f3 /proc/$pid/stat 2>/dev/null)\" != Z; then printf ROS2_OWNED_STILL_LIVE; else rm -f '$record'; printf ROS2_OWNED_STOPPED; fi; fi" | tr -d '\r\n')"
  case "$result" in
    ROS2_OWNED_GONE|ROS2_OWNED_STOPPED) return 0 ;;
    *) echo "ERROR: refused/unable to stop exact generic process $board:$pid:$start ($result)" >&2; return 1 ;;
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

# Adopt children of the isolated tracing namespace before shutting it down.
# A private mount namespace identifies consumer daemons even if they reparent;
# exact PID/start-time records retain the same cleanup guard as direct launches.
ros2_owned_adopt_namespace() {
  local board="$1" anchor="$2" output output_record pid start ns record tag entry
  local known_board known_pid known_start anchor_start='' already_tracked
  [[ "$anchor" =~ ^[0-9]+$ ]] || return 2
  for entry in "${ROS2_OWNED_TRACKED[@]}"; do
    IFS='|' read -r known_board known_pid known_start _ <<< "$entry"
    if [[ "$known_board" == "$board" && "$known_pid" == "$anchor" ]]; then anchor_start="$known_start"; fi
  done
  [[ "$anchor_start" =~ ^[0-9]+$ ]] || return 1
  output="$(remote_shell "$board" "test \"\$(cut -d ' ' -f22 /proc/$anchor/stat 2>/dev/null)\" = '$anchor_start' || exit 1; ns=\$(readlink /proc/$anchor/ns/mnt) || exit 1; test \"\$ns\" != \"\$(readlink /proc/1/ns/mnt)\" || exit 1; for p in /proc/[0-9]*; do test \"\$(readlink \"\$p/ns/mnt\" 2>/dev/null)\" = \"\$ns\" || continue; pid=\${p##*/}; start=\$(cut -d ' ' -f22 \"\$p/stat\"); printf '%s %s\\n' \"\$pid\" \"\$start\"; done" | tr -d '\r')"
  [[ -n "$output" ]] || return 1
  while read -r pid start; do
    [[ "$pid" =~ ^[0-9]+$ && "$start" =~ ^[0-9]+$ ]] || return 1
    [[ "$pid" != "$anchor" ]] || continue
    already_tracked=0
    for entry in "${ROS2_OWNED_TRACKED[@]}"; do
      IFS='|' read -r known_board known_pid known_start _ <<< "$entry"
      if [[ "$known_board" == "$board" && "$known_pid" == "$pid" && "$known_start" == "$start" ]]; then already_tracked=1; fi
    done
    [[ "$already_tracked" == 0 ]] || continue
    tag="namespace_${pid}_${start}"
    record="$ROS2_OWNED_REMOTE_DIR/$tag.pid"
    entry="ROS2_OWNED_PROCESS RUN_ID=$ROS2_OWNED_RUN_ID TAG=$tag PID=$pid START=$start"
    output_record="$(remote_shell "$board" "test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" = '$start' && (umask 077; set -C; printf '%s\\n' '$entry' > '$record') && printf ROS2_NAMESPACE_ADOPTED" | tr -d '\r\n')"
    [[ "$output_record" == ROS2_NAMESPACE_ADOPTED ]] || return 1
    ROS2_OWNED_TRACKED+=("$board|$pid|$start|$record|$tag|$tag")
  done <<< "$output"
}

ros2_owned_stop_log() {
  # Normal acceptance requires graceful termination. SIGKILL is reserved for
  # emergency cleanup after a failed gate, never for minting a PASS verdict.
  ros2_owned_signal_log "$1" "$2" TERM
}

ros2_owned_signal_log() {
  local wanted_board="$1" wanted_log="$2" signal="$3"
  local entry board pid start record tag log expected result rc=1
  local -a kept=()
  case "$signal" in INT|TERM|HUP) ;; *) return 2 ;; esac
  for entry in "${ROS2_OWNED_TRACKED[@]}"; do
    IFS='|' read -r board pid start record tag log <<< "$entry"
    if [[ "$board" == "$wanted_board" && "$log" == "$wanted_log" ]]; then
      expected="ROS2_OWNED_PROCESS RUN_ID=$ROS2_OWNED_RUN_ID TAG=$tag PID=$pid START=$start"
      result="$(remote_shell "$board" "if ! test -f '$record' || test -L '$record' || ! grep -Fqx '$expected' '$record'; then printf ROS2_OWNED_RECORD_INVALID; elif ! test -r /proc/$pid/stat; then rm -f '$record'; printf ROS2_OWNED_GONE; elif test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" != '$start'; then printf ROS2_OWNED_PID_REUSED; else kill -s '$signal' '$pid' 2>/dev/null || true; i=0; while test \$i -lt 100 && test -r /proc/$pid/stat && test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" = '$start' && test \"\$(cut -d ' ' -f3 /proc/$pid/stat 2>/dev/null)\" != Z; do i=\$((i+1)); sleep 0.1; done; if test -r /proc/$pid/stat && test \"\$(cut -d ' ' -f22 /proc/$pid/stat 2>/dev/null)\" = '$start' && test \"\$(cut -d ' ' -f3 /proc/$pid/stat 2>/dev/null)\" != Z; then printf ROS2_OWNED_SIGNAL_TIMEOUT; else rm -f '$record'; printf ROS2_OWNED_SIGNALLED; fi; fi" | tr -d '\r\n')"
      case "$result" in
        ROS2_OWNED_GONE|ROS2_OWNED_SIGNALLED) rc=0 ;;
        *) kept+=("$entry"); echo "ERROR: graceful signal failed for $board:$pid:$log ($result)" >&2 ;;
      esac
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
  if [[ ${#ROS2_OWNED_TRACKED[@]} -eq 0 ]]; then
    ros2_owned_release_locks || rc=1
  else
    echo "ERROR: retaining acceptance locks because owned processes remain unresolved" >&2
    rc=1
  fi
  return "$rc"
}
