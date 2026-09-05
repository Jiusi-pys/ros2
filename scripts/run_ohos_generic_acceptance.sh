#!/usr/bin/env bash
# Provenance-bound generic ROS 2 runtime acceptance on two KaihongOS boards.
set -euo pipefail
cd "$(dirname "$0")/.."

BOARD_A="${ROS2_BOARD_A:-3e01ff55454d202020104033bf453b00}"
BOARD_B="${ROS2_BOARD_B:-3e01ff55454d202020104433991c3b00}"
DEVICE_DIR="${ROS2_DEVICE_DIR:-/data/local/tmp/ros2-generic}"
PROVENANCE="${ROS2_RELEASE_PROVENANCE:-$PWD/ros2_ohos_generic_release_provenance.json}"
CASE_TIMEOUT="${ROS2_ACCEPTANCE_CASE_TIMEOUT:-90}"
DOMAIN="${ROS2_ACCEPTANCE_DOMAIN_ID:-$((180 + RANDOM % 40))}"
[[ "$DEVICE_DIR" =~ ^/data/local/tmp/ros2-[A-Za-z0-9][A-Za-z0-9_.-]*$ ]] || {
  echo "ERROR: ROS2_DEVICE_DIR must name an isolated /data/local/tmp/ros2-* directory" >&2
  exit 2
}

if [[ -z "${HDC:-}" ]]; then
  if [[ -n "${LOCALAPPDATA:-}" ]]; then
    HDC="$(cygpath -u "$LOCALAPPDATA")/OpenHarmony/Sdk/23/toolchains/hdc.exe"
  else
    HDC=/c/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe
  fi
fi
for value in "$BOARD_A" "$BOARD_B"; do
  [[ "$value" =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "ERROR: unsafe board identifier: $value" >&2; exit 2; }
done
[[ "$BOARD_A" != "$BOARD_B" ]] || { echo "ERROR: acceptance requires two distinct boards" >&2; exit 2; }
[[ "$DOMAIN" =~ ^[0-9]+$ && "$DOMAIN" -ge 0 && "$DOMAIN" -le 232 ]] || {
  echo "ERROR: ROS2_ACCEPTANCE_DOMAIN_ID must be in 0..232" >&2
  exit 2
}
[[ "$CASE_TIMEOUT" =~ ^[1-9][0-9]*$ && "$CASE_TIMEOUT" -le 600 ]] || {
  echo "ERROR: case timeout must be in 1..600 seconds" >&2
  exit 2
}
[[ -x "$HDC" ]] || { echo "ERROR: HDC not found: $HDC" >&2; exit 2; }
[[ -f "$PROVENANCE" && ! -L "$PROVENANCE" ]] || {
  echo "ERROR: release provenance record is missing: $PROVENANCE" >&2
  exit 2
}

remote_shell() { MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$1" shell "$2" </dev/null; }
. scripts/lib/ros2_owned_processes.sh

PROVENANCE_SHA="$(sha256sum "$PROVENANCE" | cut -d ' ' -f1)"
SOURCE_SHA="$(pixi run python -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["source_snapshot_sha256"])' "$PROVENANCE" | tr -d '\r\n')"
SDK_SHA="$(pixi run python -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["sdk"]["fingerprint_sha256"])' "$PROVENANCE" | tr -d '\r\n')"
ARCHIVE_SHA="$(pixi run python -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["artifact"]["archive_sha256"])' "$PROVENANCE" | tr -d '\r\n')"
EXPECTED_RMW="$(pixi run python -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["selected_rmw"])' "$PROVENANCE" | tr -d '\r\n')"
readarray -t PYTHON_BINDING < <(pixi run python - "$PROVENANCE" <<'PY' | tr -d '\r'
import json, sys
p = json.load(open(sys.argv[1], encoding="utf-8"))
print(p["inputs"]["clean_build_receipt"]["sha256"])
for key in ("runtime_archive_sha256", "runtime_tree_sha256", "stage_tree_sha256"):
    print(p["inputs"]["python"][key])
PY
)
[[ "${#PYTHON_BINDING[@]}" -eq 4 ]] || { echo 'ERROR: missing Python provenance' >&2; exit 2; }
RECEIPT_SHA="${PYTHON_BINDING[0]}"
PYTHON_ARCHIVE_SHA="${PYTHON_BINDING[1]}"
PYTHON_RUNTIME_TREE_SHA="${PYTHON_BINDING[2]}"
PYTHON_STAGE_TREE_SHA="${PYTHON_BINDING[3]}"
for digest in "${PYTHON_BINDING[@]}"; do
  [[ "$digest" =~ ^[0-9a-f]{64}$ ]] || { echo 'ERROR: malformed Python provenance' >&2; exit 2; }
done
for digest in "$PROVENANCE_SHA" "$SOURCE_SHA" "$SDK_SHA" "$ARCHIVE_SHA"; do
  [[ "$digest" =~ ^[0-9a-f]{64}$ ]] || { echo "ERROR: malformed provenance digest" >&2; exit 2; }
done
case "$EXPECTED_RMW" in rmw_fastrtps_cpp|rmw_cyclonedds_cpp) ;; *) echo "ERROR: unsupported provenance RMW" >&2; exit 2 ;; esac

# Capture one exact OS identity per serial before creating any test process.
# Every later terminal/process record repeats this tuple, so an individual log
# remains attributable even when copied away from the surrounding bundle.
declare -A BOARD_PRODUCT=()
declare -A BOARD_VERSION=()
declare -A BOARD_ARCH=()
for board in "$BOARD_A" "$BOARD_B"; do
  identity="$(remote_shell "$board" "product=\$(param get const.product.name 2>/dev/null | tr -d ' \\r\\n'); version=\$(param get const.product.software.version 2>/dev/null | tr -d ' \\r\\n'); arch=\$(uname -m 2>/dev/null | tr -d ' \\r\\n'); printf '%s|%s|%s' \"\$product\" \"\$version\" \"\$arch\"" | tr -d '\r\n')"
  if [[ ! "$identity" =~ ^([A-Za-z0-9_.-]+)\|([A-Za-z0-9_.-]+)\|([A-Za-z0-9_.-]+)$ ]]; then
    echo "ERROR: malformed board identity for $board: ${identity:-MISSING}" >&2
    exit 2
  fi
  BOARD_PRODUCT["$board"]="${BASH_REMATCH[1]}"
  BOARD_VERSION["$board"]="${BASH_REMATCH[2]}"
  BOARD_ARCH["$board"]="${BASH_REMATCH[3]}"
  if [[ "${BOARD_PRODUCT[$board]}" != KaihongOS || "${BOARD_ARCH[$board]}" != aarch64 ]]; then
    echo "ERROR: unexpected board identity for $board: $identity" >&2
    exit 2
  fi
done

ROS2_RUN_ID="${ROS2_RUN_ID:-generic_acceptance_$(date -u +%Y%m%dT%H%M%SZ)_${RANDOM}_${RANDOM}_$$}"
export ROS2_RUN_ID
[[ "$ROS2_RUN_ID" =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "ERROR: unsafe ROS2_RUN_ID" >&2; exit 2; }
LOGDIR="${ROS2_ACCEPTANCE_LOGROOT:-ohos_test_logs/generic_acceptance}/$ROS2_RUN_ID"
if [[ -e "$LOGDIR" || -L "$LOGDIR" ]]; then
  echo "ERROR: refusing to reuse acceptance evidence directory: $LOGDIR" >&2
  exit 2
fi
(umask 077; mkdir -p "$LOGDIR")
cp "$PROVENANCE" "$LOGDIR/release_provenance.json"
cp scripts/run_ohos_generic_acceptance.sh "$LOGDIR/runner.sh"
cp scripts/lib/ros2_owned_processes.sh "$LOGDIR/owned_processes.sh"
RUN_RECORD="$LOGDIR/run.record"
RESULT_RECORD="$LOGDIR/result.record"
printf 'ROS2_ACCEPTANCE V=1\nRUN_ID=%s\nBOARD_A=%s\nBOARD_A_OS=%s/%s/%s\nBOARD_B=%s\nBOARD_B_OS=%s/%s/%s\nDOMAIN_ID=%s\nEXPECTED_RMW=%s\nPROVENANCE_SHA256=%s\nSOURCE_SHA256=%s\nSDK_SHA256=%s\nARCHIVE_SHA256=%s\n' \
  "$ROS2_RUN_ID" "$BOARD_A" "${BOARD_PRODUCT[$BOARD_A]}" "${BOARD_VERSION[$BOARD_A]}" "${BOARD_ARCH[$BOARD_A]}" \
  "$BOARD_B" "${BOARD_PRODUCT[$BOARD_B]}" "${BOARD_VERSION[$BOARD_B]}" "${BOARD_ARCH[$BOARD_B]}" \
  "$DOMAIN" "$EXPECTED_RMW" \
  "$PROVENANCE_SHA" "$SOURCE_SHA" "$SDK_SHA" "$ARCHIVE_SHA" > "$RUN_RECORD"
printf 'BUILD_RECEIPT_SHA256=%s\nPYTHON_RUNTIME_ARCHIVE_SHA256=%s\nPYTHON_RUNTIME_TREE_SHA256=%s\nPYTHON_STAGE_TREE_SHA256=%s\n' \
  "$RECEIPT_SHA" "$PYTHON_ARCHIVE_SHA" "$PYTHON_RUNTIME_TREE_SHA" "$PYTHON_STAGE_TREE_SHA" >> "$RUN_RECORD"

finish() {
  local rc="$1"
  trap - EXIT INT TERM HUP
  if [[ -n "${ROS2_TRACE_ANCHOR:-}" ]]; then
    # A failed trace command can leave consumer children behind. Adopt them
    # while the exact namespace anchor is still alive, before stopping it.
    if ! ros2_owned_adopt_namespace "$BOARD_A" "$ROS2_TRACE_ANCHOR"; then
      ros2_owned_stop_all || true
      echo 'ERROR: trace namespace cleanup could not be established; retaining activity locks' >&2
      exit 1
    fi
  fi
  if [[ ${#ROS2_OWNED_LOCK_BOARDS[@]} -gt 0 ]]; then
    ros2_owned_finish || rc=1
  fi
  exit "$rc"
}
trap 'finish "$?"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

record_pass() {
  printf 'CASE=%s RESULT=PASS EVIDENCE=%s\n' "$1" "$2" | tee -a "$RUN_RECORD"
}

run_case() {
  local board="$1" name="$2" command="$3" output rc=0 terminal_count terminal
  output="$LOGDIR/${board}.${name}.log"
  local product="${BOARD_PRODUCT[$board]}" version="${BOARD_VERSION[$board]}" arch="${BOARD_ARCH[$board]}"
  local started_utc ended_utc command_sha
  [[ "$name" =~ ^[A-Za-z0-9_.-]+$ && "$command" != *$'\n'* ]] || return 2
  started_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  command_sha="$(printf '%s' "$command" | sha256sum | cut -d ' ' -f1)"
  printf 'ROS2_CASE_START RUN_ID=%s CASE=%s BOARD=%s START_UTC=%s COMMAND_SHA256=%s\n' \
    "$ROS2_RUN_ID" "$name" "$board" "$started_utc" "$command_sha" > "$output"
  MSYS2_ARG_CONV_EXCL='*' timeout "$CASE_TIMEOUT" "$HDC" -t "$board" shell \
    ". '$DEVICE_DIR/env.sh' || exit 70; export ROS_DOMAIN_ID='$DOMAIN'; export ROS_LOCALHOST_ONLY=0; product=\$(param get const.product.name 2>/dev/null | tr -d ' \\r\\n'); version=\$(param get const.product.software.version 2>/dev/null | tr -d ' \\r\\n'); arch=\$(uname -m 2>/dev/null | tr -d ' \\r\\n'); if test \"\$product\" = '$product' && test \"\$version\" = '$version' && test \"\$arch\" = '$arch'; then ( $command ); rc=\$?; else rc=71; fi; printf '\\nROS2_CASE_TERMINAL RUN_ID=$ROS2_RUN_ID CASE=$name BOARD=$board PRODUCT=%s VERSION=%s ARCH=%s RC=%s RMW=%s SOURCE_SHA256=%s SDK_SHA256=%s ARCHIVE_SHA256=%s PROVENANCE_SHA256=%s\\n' \"\$product\" \"\$version\" \"\$arch\" \"\$rc\" \"\$RMW_IMPLEMENTATION\" \"\$ROS2_SOURCE_SNAPSHOT_SHA256\" \"\$ROS2_SDK_FINGERPRINT_SHA256\" \"\$ROS2_ARCHIVE_SHA256\" \"\$ROS2_RELEASE_PROVENANCE_SHA256\"" \
    </dev/null >> "$output" 2>&1 || rc=$?
  ended_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf '\nROS2_CASE_END RUN_ID=%s CASE=%s BOARD=%s END_UTC=%s HDC_RC=%s\n' \
    "$ROS2_RUN_ID" "$name" "$board" "$ended_utc" "$rc" >> "$output"
  tr -d '\r' < "$output" > "$output.normalized"
  mv "$output.normalized" "$output"
  terminal_count="$(grep -Fxc "ROS2_CASE_TERMINAL RUN_ID=$ROS2_RUN_ID CASE=$name BOARD=$board PRODUCT=$product VERSION=$version ARCH=$arch RC=0 RMW=$EXPECTED_RMW SOURCE_SHA256=$SOURCE_SHA SDK_SHA256=$SDK_SHA ARCHIVE_SHA256=$ARCHIVE_SHA PROVENANCE_SHA256=$PROVENANCE_SHA" "$output" || true)"
  terminal="$(grep -F "ROS2_CASE_TERMINAL RUN_ID=$ROS2_RUN_ID CASE=$name BOARD=$board " "$output" || true)"
  expected_terminal="ROS2_CASE_TERMINAL RUN_ID=$ROS2_RUN_ID CASE=$name BOARD=$board PRODUCT=$product VERSION=$version ARCH=$arch RC=0 RMW=$EXPECTED_RMW SOURCE_SHA256=$SOURCE_SHA SDK_SHA256=$SDK_SHA ARCHIVE_SHA256=$ARCHIVE_SHA PROVENANCE_SHA256=$PROVENANCE_SHA"
  if [[ "$rc" != 0 || "$terminal_count" != 1 || "$terminal" != "$expected_terminal" ]]; then
    echo "ERROR: runtime case failed board=$board case=$name transport_rc=$rc terminal=${terminal:-MISSING}" >&2
    return 1
  fi
  record_pass "$name@$board" "$(basename "$output")"
}

fetch_owned_log() {
  local board="$1" remote_name="$2" local_name="$3" binding explicit_binding default_binding
  # The exact-PID monitor may still be reaping a child just observed as gone.
  # Await its terminal record, but never accept a missing/nonzero exit status.
  for _ in $(seq 1 10); do
    remote_shell "$board" "cat '$ROS2_OWNED_REMOTE_DIR/$remote_name'" | tr -d '\r' > "$LOGDIR/$local_name"
    grep -q '^ROS2_OWNED_EXIT ' "$LOGDIR/$local_name" && break
    sleep 0.1
  done
  ros2_owned_require_successful_exit "$LOGDIR/$local_name" || {
    echo "ERROR: missing, mismatched or nonzero owned process exit in $local_name" >&2
    return 1
  }
  binding="$(grep '^ROS2_PROCESS_BINDING ' "$LOGDIR/$local_name" || true)"
  explicit_binding="ROS2_PROCESS_BINDING RUN_ID=$ROS2_RUN_ID BOARD=$board PRODUCT=${BOARD_PRODUCT[$board]} VERSION=${BOARD_VERSION[$board]} ARCH=${BOARD_ARCH[$board]} RMW_SELECTED=$EXPECTED_RMW REQUEST_MODE=explicit SOURCE_SHA256=$SOURCE_SHA SDK_SHA256=$SDK_SHA ARCHIVE_SHA256=$ARCHIVE_SHA PROVENANCE_SHA256=$PROVENANCE_SHA"
  default_binding="ROS2_PROCESS_BINDING RUN_ID=$ROS2_RUN_ID BOARD=$board PRODUCT=${BOARD_PRODUCT[$board]} VERSION=${BOARD_VERSION[$board]} ARCH=${BOARD_ARCH[$board]} RMW_SELECTED=$EXPECTED_RMW REQUEST_MODE=compiled-default SOURCE_SHA256=$SOURCE_SHA SDK_SHA256=$SDK_SHA ARCHIVE_SHA256=$ARCHIVE_SHA PROVENANCE_SHA256=$PROVENANCE_SHA"
  if [[ "$binding" != "$explicit_binding" && "$binding" != "$default_binding" ]]; then
    echo "ERROR: missing or mismatched process provenance in $local_name" >&2
    return 1
  fi
  if grep -Eqi 'segmentation fault|core dumped|terminate called after throwing|double free or corruption|Traceback \(most recent call last\)' "$LOGDIR/$local_name"; then
    echo "ERROR: fatal runtime or teardown signature in $local_name" >&2
    return 1
  fi
}

assert_payload_pair() {
  local case_name="$1" publisher_log="$2" subscriber_log="$3"
  grep -Fq 'Publishing:' "$publisher_log" || { echo "ERROR: no publisher payload for $case_name" >&2; return 1; }
  grep -Fq 'I heard:' "$subscriber_log" || { echo "ERROR: no subscriber payload for $case_name" >&2; return 1; }
  pixi run python - "$publisher_log" "$subscriber_log" <<'PY'
import re, sys
from pathlib import Path
published = re.findall(r"Publishing:.*?(Hello World: [0-9]+)", Path(sys.argv[1]).read_text())
received = re.findall(r"I heard:.*?(Hello World: [0-9]+)", Path(sys.argv[2]).read_text())
if len(received) < 3 or any(value not in published for value in received):
    raise SystemExit("payload mismatch or fewer than 3 received samples")
print(f"PAYLOAD_MATCH published={len(published)} received={len(received)} result=PASS")
PY
  record_pass "$case_name" "$(basename "$publisher_log"),$(basename "$subscriber_log")"
}

assert_action_result() {
  pixi run python - "$1" <<'PY'
import re, sys
from pathlib import Path
text = Path(sys.argv[1]).read_text()
match = re.search(r"Result:\s*\n(.*?)Goal finished with status: SUCCEEDED", text, re.S)
if match is None:
    raise SystemExit("action has no successful terminal result")
numbers = [int(value) for value in re.findall(r"^\s*-\s*([0-9]+)\s*$", match[1], re.M)]
if numbers != [0, 1, 1, 2, 3, 5]:
    raise SystemExit(f"wrong Fibonacci result: {numbers}")
print("ACTION_RESULT_MATCH result=PASS")
PY
}

# Fail closed on stale generic ROS graph processes.  Existing unrelated
# services (including a session daemon from an older test) are recorded but
# never killed by this run.
for board in "$BOARD_A" "$BOARD_B"; do
  process_log="$LOGDIR/${board}.preexisting_processes.log"
  remote_shell "$board" "ps -ef | grep -E '[d]emo_nodes|[a]ction_tutorials|[r]osbag2_transport/(player|recorder)|[t]urtlesim_node|[r]viz2|[i]ox-roudi|[r]os2 daemon' || true" | tr -d '\r' > "$process_log"
  if [[ -s "$process_log" ]]; then
    echo "ERROR: board $board has pre-existing ROS acceptance processes; refusing to kill them" >&2
    exit 2
  fi
done

ros2_owned_init generic_acceptance "$BOARD_A" "$BOARD_B"
owned_env() { # <board> <explicit|compiled-default>
  local board="$1" mode="$2" suffix="" code
  [[ "$mode" == explicit || "$mode" == compiled-default ]] || return 2
  if [[ "$mode" == compiled-default ]]; then
    # Exercise Fast DDS' compiled transport defaults as well as RMW selection;
    # an environment-only UDP workaround must not stand in for the core fix.
    suffix='unset RMW_IMPLEMENTATION FASTDDS_BUILTIN_TRANSPORTS;'
  fi
  code=". '$DEVICE_DIR/env.sh' || exit 70; export ROS_DOMAIN_ID='$DOMAIN'; export ROS_LOCALHOST_ONLY=0; product=\$(param get const.product.name 2>/dev/null | tr -d ' \r\n'); version=\$(param get const.product.software.version 2>/dev/null | tr -d ' \r\n'); arch=\$(uname -m 2>/dev/null | tr -d ' \r\n'); test \"\$product\" = '${BOARD_PRODUCT[$board]}' && test \"\$version\" = '${BOARD_VERSION[$board]}' && test \"\$arch\" = '${BOARD_ARCH[$board]}' || exit 71; printf 'ROS2_PROCESS_BINDING RUN_ID=$ROS2_RUN_ID BOARD=$board PRODUCT=%s VERSION=%s ARCH=%s RMW_SELECTED=%s REQUEST_MODE=$mode SOURCE_SHA256=%s SDK_SHA256=%s ARCHIVE_SHA256=%s PROVENANCE_SHA256=%s\\n' \"\$product\" \"\$version\" \"\$arch\" \"\$RMW_IMPLEMENTATION\" \"\$ROS2_SOURCE_SNAPSHOT_SHA256\" \"\$ROS2_SDK_FINGERPRINT_SHA256\" \"\$ROS2_ARCHIVE_SHA256\" \"\$ROS2_RELEASE_PROVENANCE_SHA256\"; $suffix"
  printf '%s' "$code"
}
ENV_A_EXPLICIT="$(owned_env "$BOARD_A" explicit)"
ENV_B_EXPLICIT="$(owned_env "$BOARD_B" explicit)"
ENV_A_DEFAULT="$(owned_env "$BOARD_A" compiled-default)"
ENV_B_DEFAULT="$(owned_env "$BOARD_B" compiled-default)"

for board in "$BOARD_A" "$BOARD_B"; do
  metadata_command="product=\$(param get const.product.name 2>/dev/null | tr -d ' \\r\\n'); version=\$(param get const.product.software.version 2>/dev/null | tr -d ' \\r\\n'); arch=\$(uname -m | tr -d ' \\r\\n'); provenance=\$(sha256sum '$DEVICE_DIR/release_provenance.json' | cut -d ' ' -f1); test \"\$product\" = KaihongOS && test \"\$arch\" = aarch64 && test \"\$provenance\" = '$PROVENANCE_SHA' && test \"\$RMW_IMPLEMENTATION\" = '$EXPECTED_RMW' && test \"\$ROS2_SOURCE_SNAPSHOT_SHA256\" = '$SOURCE_SHA' && test \"\$ROS2_SDK_FINGERPRINT_SHA256\" = '$SDK_SHA' && test \"\$ROS2_ARCHIVE_SHA256\" = '$ARCHIVE_SHA' && printf 'RUNTIME_BINDING BOARD=%s PRODUCT=%s VERSION=%s ARCH=%s RMW=%s SOURCE_SHA256=%s SDK_SHA256=%s ARCHIVE_SHA256=%s PROVENANCE_SHA256=%s\\n' '$board' \"\$product\" \"\$version\" \"\$arch\" \"\$RMW_IMPLEMENTATION\" \"\$ROS2_SOURCE_SNAPSHOT_SHA256\" \"\$ROS2_SDK_FINGERPRINT_SHA256\" \"\$ROS2_ARCHIVE_SHA256\" \"\$provenance\""
  run_case "$board" metadata "$metadata_command"
  run_case "$board" artifact_tree "cd '$DEVICE_DIR' && sha256sum -c deploy_manifest.sha256 >/dev/null"
  run_case "$board" python_tree "python3.12 -I -B \$ROS2_HOME/share/ros2_ohos/verify_board_python.py --receipt \$ROS2_HOME/build_receipt.json --receipt-sha256 '$RECEIPT_SHA' --prefix \$ROS2_PYTHON_REMOTE_PREFIX --board '$board' --full-tree"
done

python_command="python3.12 -c 'import platform,sys,sysconfig; import numpy,psutil,rclpy,ros2cli,yaml; assert sys.version_info[:3] == (3,12,7); assert sysconfig.get_config_var(\"SOABI\") == \"cpython-312-aarch64-linux-ohos\"; assert platform.machine() == \"aarch64\"; print(\"PYTHON_ACCEPTANCE\", sys.version.split()[0], sysconfig.get_config_var(\"SOABI\"), numpy.__version__)'"
run_case "$BOARD_A" python_imports "$python_command"
run_case "$BOARD_B" python_imports "$python_command"
grep -Fq 'PYTHON_ACCEPTANCE 3.12.7 cpython-312-aarch64-linux-ohos' "$LOGDIR/${BOARD_A}.python_imports.log"
grep -Fq 'PYTHON_ACCEPTANCE 3.12.7 cpython-312-aarch64-linux-ohos' "$LOGDIR/${BOARD_B}.python_imports.log"

# Exercise the compiled/default selection path itself.  The deployed profile
# normally pins its accepted RMW, so this command deliberately removes that
# override in a subshell and asks rclpy which implementation the loader chose.
default_rmw_command="unset RMW_IMPLEMENTATION; python3.12 -c 'import rclpy; actual=rclpy.get_rmw_implementation_identifier(); assert actual == \"$EXPECTED_RMW\", actual; print(\"DEFAULT_RMW\", actual)'"
run_case "$BOARD_A" default_rmw "$default_rmw_command"
run_case "$BOARD_B" default_rmw "$default_rmw_command"
grep -Fq "DEFAULT_RMW $EXPECTED_RMW" "$LOGDIR/${BOARD_A}.default_rmw.log"
grep -Fq "DEFAULT_RMW $EXPECTED_RMW" "$LOGDIR/${BOARD_B}.default_rmw.log"

# These exact executables were included before closing the build receipt and
# are covered by the deployment manifest. No MDDS/token wrapper participates.
suite_timeout="$CASE_TIMEOUT"
CASE_TIMEOUT=210
for suite_rmw in "$EXPECTED_RMW"; do
  while IFS= read -r test_name; do
    [[ "$test_name" =~ ^test_[a-z_]+$ ]] || exit 2
    run_case "$BOARD_A" "${test_name}_${suite_rmw}" "RMW_IMPLEMENTATION='$suite_rmw' GTEST_BRIEF=1 timeout 180 \$ROS2_HOME/Lib/ros2_ohos_tests/$test_name"
  done < scripts/ohos_rmw_tests.txt
done
CASE_TIMEOUT="$suite_timeout"

run_case "$BOARD_A" cli "ros2 --help >/dev/null && ros2 pkg prefix rclcpp && ros2 interface show std_msgs/msg/String"

topic_base="/ohos_accept_${ROS2_RUN_ID//[^A-Za-z0-9_]/_}"
life_node="ohos_lifecycle_${ROS2_RUN_ID//[^A-Za-z0-9_]/_}"
ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "\$ROS2_HOME/Lib/lifecycle/lifecycle_talker --ros-args -r __node:=$life_node" lifecycle.log
sleep 3
run_case "$BOARD_A" parameter_cli "ros2 param set --no-daemon /$life_node use_sim_time true && ros2 param get --no-daemon /$life_node use_sim_time"
grep -Fq 'Boolean value is: True' "$LOGDIR/${BOARD_A}.parameter_cli.log"
run_case "$BOARD_A" parameter_restore "ros2 param set --no-daemon /$life_node use_sim_time false && ros2 param get --no-daemon /$life_node use_sim_time"
grep -Fq 'Boolean value is: False' "$LOGDIR/${BOARD_A}.parameter_restore.log"
run_case "$BOARD_A" lifecycle_initial "ros2 lifecycle get --no-daemon /$life_node"
grep -Fxq 'unconfigured [1]' "$LOGDIR/${BOARD_A}.lifecycle_initial.log"
for transition_state in 'configure|inactive [2]' 'activate|active [3]' 'deactivate|inactive [2]' 'cleanup|unconfigured [1]' 'shutdown|finalized [4]'; do
  IFS='|' read -r transition expected_state <<< "$transition_state"
  run_case "$BOARD_A" "lifecycle_$transition" "ros2 lifecycle set --no-daemon /$life_node $transition && ros2 lifecycle get --no-daemon /$life_node"
  grep -Fxq "$expected_state" "$LOGDIR/${BOARD_A}.lifecycle_$transition.log"
done
ros2_owned_stop_log "$BOARD_A" lifecycle.log
fetch_owned_log "$BOARD_A" lifecycle.log lifecycle.log

ros2_owned_launch "$BOARD_A" "$ENV_A_DEFAULT" "\$ROS2_LISTENER_RAW --ros-args -r chatter:=${topic_base}_cpp_loop" cpp_loop_listener.log
sleep 3
ros2_owned_launch "$BOARD_A" "$ENV_A_DEFAULT" "\$ROS2_TALKER_RAW --ros-args -r chatter:=${topic_base}_cpp_loop" cpp_loop_talker.log
sleep 10
ros2_owned_stop_log "$BOARD_A" cpp_loop_talker.log
ros2_owned_stop_log "$BOARD_A" cpp_loop_listener.log
fetch_owned_log "$BOARD_A" cpp_loop_talker.log cpp_loop_talker.log
fetch_owned_log "$BOARD_A" cpp_loop_listener.log cpp_loop_listener.log
assert_payload_pair cpp_loopback "$LOGDIR/cpp_loop_talker.log" "$LOGDIR/cpp_loop_listener.log"

ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "python3.12 \$ROS2_PY_LISTENER_RAW --ros-args -r chatter:=${topic_base}_py_loop" py_loop_listener.log
sleep 3
ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "python3.12 \$ROS2_PY_TALKER_RAW --ros-args -r chatter:=${topic_base}_py_loop" py_loop_talker.log
sleep 10
ros2_owned_stop_log "$BOARD_A" py_loop_talker.log
ros2_owned_stop_log "$BOARD_A" py_loop_listener.log
fetch_owned_log "$BOARD_A" py_loop_talker.log py_loop_talker.log
fetch_owned_log "$BOARD_A" py_loop_listener.log py_loop_listener.log
assert_payload_pair py_loopback "$LOGDIR/py_loop_talker.log" "$LOGDIR/py_loop_listener.log"

ros2_owned_launch "$BOARD_B" "$ENV_B_DEFAULT" "\$ROS2_LISTENER_RAW --ros-args -r chatter:=${topic_base}_cross" cross_listener.log
sleep 4
ros2_owned_launch "$BOARD_A" "$ENV_A_DEFAULT" "\$ROS2_TALKER_RAW --ros-args -r chatter:=${topic_base}_cross" cross_talker.log
sleep 15
ros2_owned_stop_log "$BOARD_A" cross_talker.log
ros2_owned_stop_log "$BOARD_B" cross_listener.log
fetch_owned_log "$BOARD_A" cross_talker.log cross_talker_A.log
fetch_owned_log "$BOARD_B" cross_listener.log cross_listener_B.log
assert_payload_pair cpp_cross_board "$LOGDIR/cross_talker_A.log" "$LOGDIR/cross_listener_B.log"

ros2_owned_launch "$BOARD_B" "$ENV_B_EXPLICIT" "python3.12 \$ROS2_PY_LISTENER_RAW --ros-args -r chatter:=${topic_base}_mixed" mixed_listener.log
sleep 3
ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "\$ROS2_TALKER_RAW --ros-args -r chatter:=${topic_base}_mixed" mixed_talker.log
sleep 10
ros2_owned_stop_log "$BOARD_A" mixed_talker.log
ros2_owned_stop_log "$BOARD_B" mixed_listener.log
fetch_owned_log "$BOARD_A" mixed_talker.log mixed_talker_A.log
fetch_owned_log "$BOARD_B" mixed_listener.log mixed_listener_B.log
assert_payload_pair cpp_python_cross_board "$LOGDIR/mixed_talker_A.log" "$LOGDIR/mixed_listener_B.log"

ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "\$ROS2_HOME/Lib/demo_nodes_cpp/add_two_ints_server" service_server.log
sleep 3
run_case "$BOARD_A" service "timeout 30 \$ROS2_HOME/Lib/demo_nodes_cpp/add_two_ints_client"
grep -Fq 'Result of add_two_ints: 5' "$LOGDIR/${BOARD_A}.service.log"
ros2_owned_stop_log "$BOARD_A" service_server.log
fetch_owned_log "$BOARD_A" service_server.log service_server.log

ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "python3.12 \$ROS2_HOME/Lib/demo_nodes_py/add_two_ints_server-script.py" py_service_server.log
sleep 3
run_case "$BOARD_A" python_service_cli_run "timeout 30 python3.12 -c 'import sys; from ros2cli.cli import main; sys.exit(main())' run demo_nodes_py add_two_ints_client"
grep -Fq 'Result of add_two_ints: 5' "$LOGDIR/${BOARD_A}.python_service_cli_run.log"
ros2_owned_stop_log "$BOARD_A" py_service_server.log
fetch_owned_log "$BOARD_A" py_service_server.log py_service_server.log

ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "\$ROS2_HOME/Lib/action_tutorials_cpp/fibonacci_action_server" action_server.log
sleep 3
run_case "$BOARD_A" action_cli "timeout 45 python3.12 -c 'import sys; from ros2cli.cli import main; sys.exit(main())' action send_goal /fibonacci action_tutorials_interfaces/action/Fibonacci '{order: 5}' --feedback"
grep -Fq 'Goal accepted' "$LOGDIR/${BOARD_A}.action_cli.log"
grep -Fq 'Result:' "$LOGDIR/${BOARD_A}.action_cli.log"
assert_action_result "$LOGDIR/${BOARD_A}.action_cli.log"
ros2_owned_stop_log "$BOARD_A" action_server.log
fetch_owned_log "$BOARD_A" action_server.log action_server.log

ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "python3.12 \$ROS2_HOME/Lib/action_tutorials_py/fibonacci_action_server-script.py" py_action_server.log
sleep 3
run_case "$BOARD_A" python_action_cli "timeout 45 python3.12 -c 'import sys; from ros2cli.cli import main; sys.exit(main())' action send_goal /fibonacci action_tutorials_interfaces/action/Fibonacci '{order: 5}' --feedback"
grep -Fq 'Goal accepted' "$LOGDIR/${BOARD_A}.python_action_cli.log"
grep -Fq 'Result:' "$LOGDIR/${BOARD_A}.python_action_cli.log"
assert_action_result "$LOGDIR/${BOARD_A}.python_action_cli.log"
# This upstream tutorial handles KeyboardInterrupt (the documented Ctrl-C
# path), unlike demo_nodes_py which also handles ExternalShutdownException.
ros2_owned_signal_log "$BOARD_A" py_action_server.log INT
fetch_owned_log "$BOARD_A" py_action_server.log py_action_server.log

for bag_language in cpp python; do
bag_name="bag_$bag_language"
bag_dir="$ROS2_OWNED_REMOTE_DIR/$bag_name"
bag_topic="${topic_base}_$bag_name"
bag_publisher='$ROS2_TALKER_RAW'
bag_subscriber='$ROS2_LISTENER_RAW'
if [[ "$bag_language" == python ]]; then
  bag_publisher='python3.12 $ROS2_PY_TALKER_RAW'
  bag_subscriber='python3.12 $ROS2_PY_LISTENER_RAW'
fi
ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "$bag_publisher --ros-args -r chatter:=$bag_topic" "${bag_name}_talker.log"
sleep 2
ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "python3.12 -c 'import sys; from ros2cli.cli import main; sys.exit(main())' bag record -s sqlite3 -o '$bag_dir' '$bag_topic'" "${bag_name}_recorder.log"
sleep 12
ros2_owned_signal_log "$BOARD_A" "${bag_name}_recorder.log" INT
ros2_owned_stop_log "$BOARD_A" "${bag_name}_talker.log"
fetch_owned_log "$BOARD_A" "${bag_name}_recorder.log" "${bag_name}_recorder.log"
fetch_owned_log "$BOARD_A" "${bag_name}_talker.log" "${bag_name}_talker.log"
run_case "$BOARD_A" "${bag_name}_verify" "test -f '$bag_dir/metadata.yaml' && grep -Eq 'message_count: [1-9][0-9]*' '$bag_dir/metadata.yaml' && find '$bag_dir' -type f -size +0c | grep -q ."
ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "$bag_subscriber --ros-args -r chatter:=$bag_topic" "${bag_name}_play_listener.log"
sleep 3
run_case "$BOARD_A" "${bag_name}_play" "timeout 40 python3.12 -c 'import sys; from ros2cli.cli import main; sys.exit(main())' bag play '$bag_dir'"
ros2_owned_stop_log "$BOARD_A" "${bag_name}_play_listener.log"
fetch_owned_log "$BOARD_A" "${bag_name}_play_listener.log" "${bag_name}_play_listener.log"
assert_payload_pair "${bag_name}_playback" "$LOGDIR/${bag_name}_talker.log" "$LOGDIR/${bag_name}_play_listener.log"
run_case "$BOARD_A" "${bag_name}_info" "ros2 bag info '$bag_dir' && tar -czf '$ROS2_OWNED_REMOTE_DIR/$bag_name.tar.gz' -C '$bag_dir' ."
bag_archive_sha="$(remote_shell "$BOARD_A" "sha256sum '$ROS2_OWNED_REMOTE_DIR/$bag_name.tar.gz'" | tr -d '\r' | cut -d ' ' -f1)"
MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$BOARD_A" file recv "$ROS2_OWNED_REMOTE_DIR/$bag_name.tar.gz" "$(cygpath -aw "$LOGDIR/$bag_name.tar.gz")"
[[ "$(sha256sum "$LOGDIR/$bag_name.tar.gz" | cut -d ' ' -f1)" == "$bag_archive_sha" ]] || { echo 'ERROR: rosbag transfer hash mismatch' >&2; exit 1; }
printf 'ROSBAG_%s_ARCHIVE_SHA256=%s\n' "${bag_language^^}" "$bag_archive_sha" >> "$RUN_RECORD"
done

# Root LTTng ignores LTTNG_HOME for its global sockets. A private mount namespace
# gives this release its own /var/run and /dev/shm while preserving old daemons.
lttng_home="$ROS2_OWNED_REMOTE_DIR/lttng_home"
trace_path="$ROS2_OWNED_REMOTE_DIR/traces"
trace_session="trace_${ROS2_RUN_ID//[^A-Za-z0-9_]/_}"
remote_shell "$BOARD_A" "mkdir -p '$lttng_home' '$trace_path'" >/dev/null
TRACE_ENV="$ENV_A_EXPLICIT export LTTNG_HOME='$lttng_home';"
ros2_owned_launch "$BOARD_A" "$TRACE_ENV" "unshare -m -- python3.12 -I -B \$ROS2_HOME/share/ros2_ohos/trace_mount_namespace.py \$ROS2_HOME/bin/lttng-sessiond --no-kernel --verbose" lttng_sessiond.log
IFS='|' read -r trace_board trace_pid trace_start trace_record trace_tag trace_log <<< "${ROS2_OWNED_TRACKED[-1]}"
ROS2_TRACE_ANCHOR="$trace_pid"
sleep 3
trace_cli="nsenter -t $trace_pid -m -- python3.12 -c 'import sys; from ros2cli.cli import main; sys.exit(main())'"
run_case "$BOARD_A" trace_start "export LTTNG_HOME='$lttng_home'; $trace_cli trace start '$trace_session' --path '$trace_path'"
ros2_owned_launch "$BOARD_A" "$TRACE_ENV" "nsenter -t $trace_pid -m -- \$ROS2_TALKER_RAW --ros-args -r chatter:=${topic_base}_trace" trace_talker.log
sleep 5
ros2_owned_stop_log "$BOARD_A" trace_talker.log
run_case "$BOARD_A" trace_stop "export LTTNG_HOME='$lttng_home'; $trace_cli trace stop '$trace_session'"
run_case "$BOARD_A" trace_verify "find '$trace_path' -type f -name metadata | grep -q . && tar -czf '$ROS2_OWNED_REMOTE_DIR/traces.tar.gz' -C '$trace_path' ."
trace_archive_sha="$(remote_shell "$BOARD_A" "sha256sum '$ROS2_OWNED_REMOTE_DIR/traces.tar.gz'" | tr -d '\r' | cut -d ' ' -f1)"
MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$BOARD_A" file recv "$ROS2_OWNED_REMOTE_DIR/traces.tar.gz" "$(cygpath -aw "$LOGDIR/traces.tar.gz")"
[[ "$(sha256sum "$LOGDIR/traces.tar.gz" | cut -d ' ' -f1)" == "$trace_archive_sha" ]] || { echo 'ERROR: trace transfer hash mismatch' >&2; exit 1; }
mkdir "$LOGDIR/trace_payload"
tar -xzf "$LOGDIR/traces.tar.gz" -C "$LOGDIR/trace_payload"
trace_windows="$(cygpath -am "$LOGDIR/trace_payload")"
trace_drive="${trace_windows:0:1}"
trace_linux="/mnt/${trace_drive,,}/${trace_windows:3}"
MSYS2_ARG_CONV_EXCL='*' wsl.exe -d "${ROS2_TRACE_WSL_DISTRO:-Ubuntu-20.04}" -- babeltrace "$trace_linux" > "$LOGDIR/trace_decoded.log" 2> "$LOGDIR/trace_decoder.stderr.log"
grep -Eq 'ros2:(rcl_init|rcl_node_init|rclcpp_publish|rcl_publish|rmw_publish)' "$LOGDIR/trace_decoded.log" || { echo 'ERROR: no decoded ROS 2 events' >&2; exit 1; }
printf 'TRACE_ARCHIVE_SHA256=%s\n' "$trace_archive_sha" >> "$RUN_RECORD"
record_pass trace_decode trace_decoded.log
ros2_owned_adopt_namespace "$BOARD_A" "$trace_pid"
ros2_owned_stop_log "$BOARD_A" lttng_sessiond.log
ROS2_TRACE_ANCHOR=''
fetch_owned_log "$BOARD_A" lttng_sessiond.log lttng_sessiond.log
fetch_owned_log "$BOARD_A" trace_talker.log trace_talker.log

if [[ "${ROS2_ACCEPTANCE_EXPERIMENTAL_GUI:-0}" == 1 ]]; then
run_case "$BOARD_A" rqt_plugins "rqt --list-plugins | grep -q rqt_topic"
ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "\$ROS2_HOME/Lib/turtlesim/turtlesim_node" turtlesim.log
sleep 8
run_case "$BOARD_A" turtlesim_graph "ros2 node list --no-daemon | grep -Fx /turtlesim"
ros2_owned_stop_log "$BOARD_A" turtlesim.log
fetch_owned_log "$BOARD_A" turtlesim.log turtlesim.log

# RViz is accepted only as an offscreen/headless smoke: it must stay alive and
# appear in the graph.  Visible display output is explicitly outside scope.
ros2_owned_launch "$BOARD_A" "$ENV_A_EXPLICIT" "\$ROS2_HOME/Lib/rviz2/rviz2" rviz2.log
sleep 15
run_case "$BOARD_A" rviz_graph "ros2 node list --no-daemon | grep -Ex '/rviz2?'"
ros2_owned_stop_log "$BOARD_A" rviz2.log
fetch_owned_log "$BOARD_A" rviz2.log rviz2.log
if grep -Eqi 'segmentation fault|core dumped|OGRE EXCEPTION|failed to create render' "$LOGDIR/rviz2.log"; then
  echo "ERROR: RViz offscreen log contains a fatal rendering signature" >&2
  exit 1
fi
record_pass rviz_offscreen rviz2.log
else
  printf 'CASE=experimental_gui RESULT=NOT_RUN STATUS=EXPERIMENTAL\n' >> "$RUN_RECORD"
fi

# Drain adopted tracing children gracefully too. Emergency trap cleanup may
# force-kill an owned process, but that path cannot produce an acceptance PASS.
for remaining_entry in "${ROS2_OWNED_TRACKED[@]}"; do
  IFS='|' read -r remaining_board remaining_pid remaining_start remaining_record remaining_tag remaining_log <<< "$remaining_entry"
  ros2_owned_signal_log "$remaining_board" "$remaining_log" TERM
done
ros2_owned_finish
ROS2_OWNED_LOCK_BOARDS=()

for board in "$BOARD_A" "$BOARD_B"; do
  run_case "$board" cleanup "test -z \"\$(ps -ef | grep -E '[d]emo_nodes|[a]ction_tutorials|[r]osbag2_transport/(player|recorder)|[t]urtlesim_node|[r]viz2|[i]ox-roudi|[r]os2 daemon' || true)\""
  run_case "$board" artifact_tree_final "cd '$DEVICE_DIR' && sha256sum -c deploy_manifest.sha256 >/dev/null"
  run_case "$board" python_tree_final "python3.12 -I -B \$ROS2_HOME/share/ros2_ohos/verify_board_python.py --receipt \$ROS2_HOME/build_receipt.json --receipt-sha256 '$RECEIPT_SHA' --prefix \$ROS2_PYTHON_REMOTE_PREFIX --board '$board' --full-tree"
done

(cd "$LOGDIR" && find . -type f ! -name evidence.sha256 ! -name result.record -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) > "$LOGDIR/evidence.sha256"
EVIDENCE_SHA="$(sha256sum "$LOGDIR/evidence.sha256" | cut -d ' ' -f1)"
printf 'ROS2_ACCEPTANCE_RESULT V=1\nRUN_ID=%s\nRESULT=PASS\nRMW=%s\nBOARD_A=%s\nBOARD_A_OS=%s/%s/%s\nBOARD_B=%s\nBOARD_B_OS=%s/%s/%s\nDOMAIN_ID=%s\nSOURCE_SHA256=%s\nSDK_SHA256=%s\nARCHIVE_SHA256=%s\nPROVENANCE_SHA256=%s\nEVIDENCE_MANIFEST_SHA256=%s\n' \
  "$ROS2_RUN_ID" "$EXPECTED_RMW" \
  "$BOARD_A" "${BOARD_PRODUCT[$BOARD_A]}" "${BOARD_VERSION[$BOARD_A]}" "${BOARD_ARCH[$BOARD_A]}" \
  "$BOARD_B" "${BOARD_PRODUCT[$BOARD_B]}" "${BOARD_VERSION[$BOARD_B]}" "${BOARD_ARCH[$BOARD_B]}" \
  "$DOMAIN" "$SOURCE_SHA" "$SDK_SHA" "$ARCHIVE_SHA" "$PROVENANCE_SHA" "$EVIDENCE_SHA" > "$RESULT_RECORD"
printf 'BUILD_RECEIPT_SHA256=%s\nPYTHON_RUNTIME_ARCHIVE_SHA256=%s\nPYTHON_RUNTIME_TREE_SHA256=%s\nPYTHON_STAGE_TREE_SHA256=%s\n' \
  "$RECEIPT_SHA" "$PYTHON_ARCHIVE_SHA" "$PYTHON_RUNTIME_TREE_SHA" "$PYTHON_STAGE_TREE_SHA" >> "$RESULT_RECORD"
printf 'ROS2_ACCEPTANCE_RESULT=PASS RECORD=%s EVIDENCE_SHA256=%s\n' "$RESULT_RECORD" "$EVIDENCE_SHA"
