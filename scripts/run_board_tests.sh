#!/usr/bin/env bash
# Run the cross-built gtest suites of selected packages on a board.
#
# ament test binaries are not installed into install_ohos/; they live at the
# top level of build_ohos/<pkg>/. For each package this script
#   1. generates a board-side driver from build_ohos/<pkg>/CTestTestfile.cmake
#      (scripts/_parse_ctest_env.py) so the ament test fixtures (env vars,
#      library-path appends, --skip-test markers) are replayed faithfully,
#   2. pushes the package's test executables + helper .so files (layout kept)
#      plus the driver to $ROS2_HOME/tests/<pkg>/ on the board,
#   3. runs the driver and reports BOARDTEST verdict lines.
#
# Usage: ./scripts/run_board_tests.sh [board_serial] [pkg ...]
#   default board: board A; default pkgs: core set (see below)
set -uo pipefail

is_canonical_positive_decimal() {
  [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

decimal_leq() {
  local value="$1" maximum="$2"
  if ! is_canonical_positive_decimal "$value" || ! is_canonical_positive_decimal "$maximum"; then
    return 1
  fi
  if (( ${#value} < ${#maximum} )); then
    return 0
  fi
  if (( ${#value} > ${#maximum} )); then
    return 1
  fi
  [[ "$value" == "$maximum" || "$value" < "$maximum" ]]
}
cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00
BOARD_B=3e01ff55454d202020104433991c3b00

# First argument may be the documented A/B alias or a board serial, provided
# it is not a package build directory.
BOARD="$BOARD_A"
if [ $# -gt 0 ] && [ ! -d "build_ohos/$1" ]; then
  case "$1" in
    A|a) BOARD="$BOARD_A" ;;
    B|b) BOARD="$BOARD_B" ;;
    *) BOARD="$1" ;;
  esac
  shift
fi
case "$BOARD" in
  *[!A-Za-z0-9_.-]*|'')
    echo "ERROR: board target must contain only A-Za-z0-9_.-: $BOARD" >&2
    exit 2
    ;;
esac
PKGS=("$@")
if [ ${#PKGS[@]} -eq 0 ]; then
  PKGS=(rcutils rcpputils rosidl_runtime_c rosidl_runtime_cpp rmw
        rcl_yaml_param_parser rcl rcl_action rcl_lifecycle rclcpp test_msgs)
fi

declare -A REQUESTED_PACKAGE_NAMES=()
for requested_pkg in "${PKGS[@]}"; do
  if ! [[ "$requested_pkg" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ ]]; then
    echo "ERROR: package names must use only safe component characters: $requested_pkg" >&2
    exit 2
  fi
  if [[ -n "${REQUESTED_PACKAGE_NAMES[$requested_pkg]+present}" ]]; then
    echo "ERROR: duplicate requested package: $requested_pkg" >&2
    exit 2
  fi
  REQUESTED_PACKAGE_NAMES["$requested_pkg"]=1
done

export MSYS2_ARG_CONV_EXCL='*'
export PATH="$HOME/.pixi/bin:$PATH"
WS_ROOT="$(pwd -W 2>/dev/null || pwd)"
ROS2_HOME=/data/local/tmp/ros2
# This is deliberately the same per-board lock used by the targeted deployer
# and the DS/GW launchers.  A test must never copy a binary or start a board
# payload while a cooperating deployment is staging, committing, or rolling
# back that board.
ACTIVITY_LOCK_DIR="$ROS2_HOME/.mdds-activity-lock"
# Keep a verbatim board-side archive for each package.  A final verification
# manifest can therefore include the gtest XML/log bytes that produced each
# summarized BOARDTEST line, instead of treating the summary as raw evidence.
RUN_ID="${MDDS_RUN_ID:-board_$(date +%Y%m%d_%H%M%S)_$RANDOM}"
RUN_NONCE="${MDDS_BOARDTEST_RUN_NONCE:-test_$(date +%Y%m%d%H%M%S)_${RANDOM}_${RANDOM}_$$}"
LOGROOT="${MDDS_BOARDTEST_LOGROOT:-ohos_test_logs/board_tests}"
case "$RUN_ID" in
  *[!A-Za-z0-9_.-]*|'')
    echo "ERROR: MDDS_RUN_ID must contain only A-Za-z0-9_.-" >&2
    exit 2
    ;;
esac
case "$RUN_NONCE" in
  *[!A-Za-z0-9_.-]*|'')
    echo "ERROR: MDDS_BOARDTEST_RUN_NONCE must contain only A-Za-z0-9_.-" >&2
    exit 2
    ;;
esac
# Every component is constrained above before interpolation into a remote
# shell command.  The nonce makes this owner record unique even for repeated
# RUN_ID values; release is permitted only for this exact full record.
ACTIVITY_LOCK_OWNER="MDDS_ACTIVITY_LOCK MODE=TEST RUN_ID=$RUN_ID NONCE=$RUN_NONCE OWNER=run_board_tests"
ACTIVITY_LOCK_HELD=0
# HDC does not reliably propagate the remote driver's exit state.  Retain the
# shared lock unless every driver that we attempted is later proven to have
# reached its run-scoped terminal marker and its raw evidence archive is back
# on the host with a matching hash.
SAFE_TO_RELEASE=0
LOGDIR="$LOGROOT/$RUN_ID/$RUN_NONCE/board_$BOARD"
if [[ -e "$LOGDIR" || -L "$LOGDIR" ]]; then
  echo "ERROR: refusing to reuse existing board evidence directory $LOGDIR" >&2
  exit 2
fi
if ! (umask 077; mkdir -p "$LOGDIR"); then
  echo "ERROR: cannot create board evidence directory $LOGDIR" >&2
  exit 2
fi
ARCHIVE_MAX_BYTES="${MDDS_BOARDTEST_MAX_ARCHIVE_BYTES:-268435456}"
if ! is_canonical_positive_decimal "$ARCHIVE_MAX_BYTES" || ! decimal_leq "$ARCHIVE_MAX_BYTES" 1073741824; then
  echo "ERROR: MDDS_BOARDTEST_MAX_ARCHIVE_BYTES must be a decimal value from 1 to 1073741824" >&2
  exit 2
fi
ARCHIVE_STREAM_TIMEOUT_SEC="${MDDS_BOARDTEST_ARCHIVE_STREAM_TIMEOUT_SEC:-600}"
if ! is_canonical_positive_decimal "$ARCHIVE_STREAM_TIMEOUT_SEC" || ! decimal_leq "$ARCHIVE_STREAM_TIMEOUT_SEC" 3600; then
  echo "ERROR: MDDS_BOARDTEST_ARCHIVE_STREAM_TIMEOUT_SEC must be a decimal value from 1 to 3600" >&2
  exit 2
fi
TRANSFER_VERIFY_ATTEMPTS="${MDDS_BOARDTEST_TRANSFER_VERIFY_ATTEMPTS:-10}"
if ! is_canonical_positive_decimal "$TRANSFER_VERIFY_ATTEMPTS" || ! decimal_leq "$TRANSFER_VERIFY_ATTEMPTS" 60; then
  echo "ERROR: MDDS_BOARDTEST_TRANSFER_VERIFY_ATTEMPTS must be a decimal value from 1 to 60" >&2
  exit 2
fi
TRANSFER_VERIFY_READBACK_TIMEOUT_SEC="${MDDS_BOARDTEST_TRANSFER_VERIFY_READBACK_TIMEOUT_SEC:-5}"
if ! is_canonical_positive_decimal "$TRANSFER_VERIFY_READBACK_TIMEOUT_SEC" || ! decimal_leq "$TRANSFER_VERIFY_READBACK_TIMEOUT_SEC" 30; then
  echo "ERROR: MDDS_BOARDTEST_TRANSFER_VERIFY_READBACK_TIMEOUT_SEC must be a decimal value from 1 to 30" >&2
  exit 2
fi
if ! command -v base64 >/dev/null 2>&1; then
  echo "ERROR: host base64 decoder is required for hash-verified board evidence streaming" >&2
  exit 2
fi
if ! command -v timeout >/dev/null 2>&1; then
  echo "ERROR: host timeout command is required for bounded board evidence streaming" >&2
  exit 2
fi
if ! command -v sleep >/dev/null 2>&1; then
  echo "ERROR: host sleep command is required for hash-verified transfer readback" >&2
  exit 2
fi
if ! (umask 077; set -C; printf 'BOARDTEST_RUN_ID=%s\nBOARDTEST_BOARD=%s\nBOARDTEST_RUN_NONCE=%s\n' \
  "$RUN_ID" "$BOARD" "$RUN_NONCE" > "$LOGDIR/run.txt") 2>/dev/null; then
  echo "ERROR: cannot create immutable board run record $LOGDIR/run.txt" >&2
  exit 2
fi

# package name -> source dir (for test fixture resource files)
declare -A PKG_SRC=()
while read -r name path; do
  PKG_SRC["$name"]="$path"
done < <(pixi run colcon list --base-paths src 2>/dev/null | awk '{print $1, $2}')

# tests that can never pass off the build host (assert build-machine RPATH
# mechanics etc.)
is_known_skip() {
  case "$1" in
    rcutils/test_shared_library_in_run_paths) return 0 ;;  # DT_RPATH bakes host build path
  esac
  return 1
}

# hdc consumes stdin when it feels like it - never let it eat a loop's pipe
shell() { "$HDC" -t "$BOARD" shell "$*" </dev/null; }
push() {
  local source_windows
  # `hdc file send` reports a zero process status even when it cannot open a
  # long Windows source path.  The READY manifests live under a run/nonce
  # evidence directory and can exceed MAX_PATH.  cygpath -aw creates an
  # absolute extended Win32 path instead of a long relative path that HDC
  # would re-expand internally.  Keep the local regular-file gate as a
  # second fail-closed check before HDC's unreliable status is considered.
  [[ -f "$1" && ! -L "$1" ]] || return 1
  source_windows="$(cygpath -aw "$1")" || return 1
  "$HDC" -t "$BOARD" file send "$source_windows" "$2" </dev/null > /dev/null
}

# Every remotely interpolated package-relative path is generated from a local
# build/fixture file and must still be component-safe.  This keeps the READY
# manifest parse-free and prevents a caller-supplied package name or unusual
# build artifact from changing a board-side shell command.
safe_package_name() {
  [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*$ ]]
}

safe_relative_path() {
  [[ "$1" =~ ^[A-Za-z0-9][A-Za-z0-9_.-]*(/[A-Za-z0-9][A-Za-z0-9_.-]*)*$ ]]
}

valid_sha256() {
  [[ "$1" =~ ^[0-9a-f]{64}$ ]]
}

local_regular_sha() {
  local path="$1" digest
  if [[ ! -f "$path" || -L "$path" ]]; then
    echo "ERROR: refusing non-regular or symlinked local test input: $path" >&2
    return 1
  fi
  digest="$(sha256sum "$path" | cut -d ' ' -f1)"
  valid_sha256 "$digest" || return 1
  printf '%s\n' "$digest"
}

local_regular_bytes() {
  local path="$1" bytes
  if [[ ! -f "$path" || -L "$path" ]]; then
    echo "ERROR: refusing non-regular or symlinked local evidence file: $path" >&2
    return 1
  fi
  bytes="$(wc -c < "$path" | tr -d ' ')"
  [[ "$bytes" =~ ^[0-9]+$ ]] || return 1
  printf '%s\n' "$bytes"
}

# GNU tar treats a colon in a Windows absolute path (for example C:/...) as a
# remote archive separator unless this flag is present.  Board archives are
# deliberately stored under the host evidence root, so validate them through
# one wrapper rather than silently turning a successful board run into an
# unproven result on the Windows collector.
local_archive_tar() {
  tar --force-local "$@"
}

strict_hdc_single_line() {
  local value="$1"
  # Command substitution strips trailing LF.  HDC may leave one transport CR
  # behind, but any remaining CR or LF means the result is not one sentinel.
  value="${value%$'\r'}"
  [[ "$value" != *$'\r'* && "$value" != *$'\n'* ]] || return 1
  printf '%s' "$value"
}

create_exclusive_local_file() {
  local path="$1"
  [[ ! -e "$path" && ! -L "$path" ]] || return 1
  (umask 077; set -C; : > "$path") 2>/dev/null
}

REMOTE_HASH_READBACK_DIGEST=""
REMOTE_HASH_READBACK_LAST_RC=0
remote_regular_sha() { # <remote-path>
  local path="$1" raw digest rc=0
  REMOTE_HASH_READBACK_DIGEST=""
  REMOTE_HASH_READBACK_LAST_RC=0
  raw="$(timeout --kill-after=1s "$TRANSFER_VERIFY_READBACK_TIMEOUT_SEC" "$HDC" -t "$BOARD" shell "if test -f '$path' && test ! -L '$path'; then sha256sum '$path' 2>/dev/null | cut -d ' ' -f1; fi" </dev/null)" || rc=$?
  REMOTE_HASH_READBACK_LAST_RC="$rc"
  digest="$(printf '%s' "$raw" | tr -d '\r\n')"
  valid_sha256 "$digest" || return 1
  REMOTE_HASH_READBACK_DIGEST="$digest"
  return 0
}

REMOTE_HASH_WAIT_ATTEMPTS=0
REMOTE_HASH_WAIT_DIGEST=""
REMOTE_HASH_WAIT_NONZERO_READBACKS=0
REMOTE_HASH_WAIT_LAST_RC=0
wait_remote_regular_sha() { # <remote-path> <expected-sha256>
  local path="$1" expected="$2" got="" attempt
  REMOTE_HASH_WAIT_ATTEMPTS=0
  REMOTE_HASH_WAIT_DIGEST=""
  REMOTE_HASH_WAIT_NONZERO_READBACKS=0
  REMOTE_HASH_WAIT_LAST_RC=0
  for ((attempt = 1; attempt <= TRANSFER_VERIFY_ATTEMPTS; attempt++)); do
    if remote_regular_sha "$path"; then
      got="$REMOTE_HASH_READBACK_DIGEST"
    else
      got=""
    fi
    REMOTE_HASH_WAIT_ATTEMPTS="$attempt"
    REMOTE_HASH_WAIT_LAST_RC="$REMOTE_HASH_READBACK_LAST_RC"
    if [ "$REMOTE_HASH_READBACK_LAST_RC" -ne 0 ]; then
      REMOTE_HASH_WAIT_NONZERO_READBACKS=$((REMOTE_HASH_WAIT_NONZERO_READBACKS + 1))
    fi
    if [[ "$got" == "$expected" ]]; then
      REMOTE_HASH_WAIT_DIGEST="$got"
      return 0
    fi
    if [ "$attempt" -lt "$TRANSFER_VERIFY_ATTEMPTS" ]; then
      sleep 1 || return 1
    fi
  done
  REMOTE_HASH_WAIT_DIGEST="$got"
  return 1
}

remote_mkdir_verified() { # <remote-directory> <label>
  local directory="$1" label="$2" out
  out="$(shell "if mkdir -p '$directory' && test -d '$directory' && test ! -L '$directory'; then printf MDDS_BOARDTEST_DIR_READY; else printf MDDS_BOARDTEST_DIR_FAILED; fi" | tr -d '\r\n')"
  if [[ "$out" != "MDDS_BOARDTEST_DIR_READY" ]]; then
    echo "ERROR: remote mkdir verification failed for $label: ${out:-NO_MARKER}" >&2
    return 1
  fi
}

declare -A READY_MANIFEST_SOURCE=()
declare -A READY_MANIFEST_SHA=()
declare -a READY_MANIFEST_PATHS=()
READY_MANIFEST_LOCAL=""
READY_MANIFEST_SHA256=""
READY_MANIFEST_REMOTE=""
READY_RECORD_REMOTE=""
READY_RECORD_LINE=""
TERMINAL_LINE=""
DRIVER_TERMINAL_RC=""
DRIVER_TERMINAL_RECORD_VALID=0
CAPTURED_ARCHIVE=""
ARCHIVE_REMOTE_PARENT="$ROS2_HOME/.mdds-board-evidence"
ARCHIVE_REMOTE_RUN_DIR="$ARCHIVE_REMOTE_PARENT/$RUN_ID"
ARCHIVE_REMOTE_DIR="$ARCHIVE_REMOTE_RUN_DIR/$RUN_NONCE"
ARCHIVE_STREAM_READY=0
ARCHIVE_METADATA_SHA=""
ARCHIVE_METADATA_BYTES=""
ARCHIVE_METADATA_LINE=""

reset_ready_manifest() {
  READY_MANIFEST_SOURCE=()
  READY_MANIFEST_SHA=()
  READY_MANIFEST_PATHS=()
  READY_MANIFEST_LOCAL=""
  READY_MANIFEST_SHA256=""
  READY_MANIFEST_REMOTE=""
  READY_RECORD_REMOTE=""
  READY_RECORD_LINE=""
  TERMINAL_LINE=""
  DRIVER_TERMINAL_RC=""
  DRIVER_TERMINAL_RECORD_VALID=0
  CAPTURED_ARCHIVE=""
}

add_ready_manifest_file() { # <relative-path> <local-path>
  local relative="$1" local_path="$2" digest
  if ! safe_relative_path "$relative"; then
    echo "ERROR: refusing unsafe READY manifest path: $relative" >&2
    return 1
  fi
  if [[ -n "${READY_MANIFEST_SOURCE[$relative]+present}" ]]; then
    echo "ERROR: duplicate READY manifest path: $relative" >&2
    return 1
  fi
  digest="$(local_regular_sha "$local_path")" || return 1
  READY_MANIFEST_SOURCE["$relative"]="$local_path"
  READY_MANIFEST_SHA["$relative"]="$digest"
  READY_MANIFEST_PATHS+=("$relative")
}

write_ready_manifest() { # <package>
  local pkg="$1" temporary manifest digest
  manifest="$LOGDIR/${pkg}.mdds_ready_manifest_${RUN_ID}_${RUN_NONCE}"
  if [[ -e "$manifest" || -L "$manifest" ]]; then
    echo "ERROR: refusing pre-existing local READY manifest for $pkg: $manifest" >&2
    return 1
  fi
  temporary="$(mktemp "$LOGDIR/.${pkg}.ready_manifest.XXXXXX")" || return 1
  if ! {
    printf 'MDDS_BOARDTEST_MANIFEST V=1 RUN_ID=%s NONCE=%s PACKAGE=%s\n' "$RUN_ID" "$RUN_NONCE" "$pkg"
    for relative in "${READY_MANIFEST_PATHS[@]}"; do
      printf 'SHA256=%s PATH=%s\n' "${READY_MANIFEST_SHA[$relative]}" "$relative"
    done | LC_ALL=C sort
  } > "$temporary"; then
    rm -f "$temporary"
    return 1
  fi
  if ! (umask 077; set -C; cat "$temporary" > "$manifest") 2>/dev/null; then
    echo "ERROR: failed to create local READY manifest without overwrite for $pkg" >&2
    rm -f "$temporary"
    return 1
  fi
  rm -f "$temporary"
  digest="$(local_regular_sha "$manifest")" || return 1
  READY_MANIFEST_LOCAL="$manifest"
  READY_MANIFEST_SHA256="$digest"
  printf 'BOARDTEST_READY_MANIFEST package=%s path=%s sha256=%s entries=%s\n' \
    "$pkg" "$manifest" "$digest" "${#READY_MANIFEST_PATHS[@]}" | tee -a "$LOGDIR/run.txt"
}

transfer_manifest_file() { # <package-root> <relative-path>
  local package_root="$1" relative="$2" local_path expected remote parent got push_rc=0
  local_path="${READY_MANIFEST_SOURCE[$relative]}"
  expected="${READY_MANIFEST_SHA[$relative]}"
  if [[ "$(local_regular_sha "$local_path")" != "$expected" ]]; then
    echo "ERROR: local READY manifest input changed before transfer: $relative" >&2
    return 1
  fi
  parent="$(dirname "$relative")"
  if [[ "$parent" != "." ]]; then
    remote_mkdir_verified "$package_root/$parent" "package file parent $relative" || return 1
  fi
  remote="$package_root/$relative"
  push "$local_path" "$remote" || push_rc=$?
  if wait_remote_regular_sha "$remote" "$expected"; then
    got="$REMOTE_HASH_WAIT_DIGEST"
    printf 'BOARDTEST_READY_INPUT_TRANSFER path=%s hdc_rc=%s verify_attempts=%s readback_nonzero=%s readback_last_rc=%s result=EXACT\n' \
      "$relative" "$push_rc" "$REMOTE_HASH_WAIT_ATTEMPTS" "$REMOTE_HASH_WAIT_NONZERO_READBACKS" "$REMOTE_HASH_WAIT_LAST_RC" | tee -a "$LOGDIR/run.txt"
  else
    got="$REMOTE_HASH_WAIT_DIGEST"
    printf 'BOARDTEST_READY_INPUT_TRANSFER path=%s hdc_rc=%s verify_attempts=%s readback_nonzero=%s readback_last_rc=%s result=MISMATCH\n' \
      "$relative" "$push_rc" "$REMOTE_HASH_WAIT_ATTEMPTS" "$REMOTE_HASH_WAIT_NONZERO_READBACKS" "$REMOTE_HASH_WAIT_LAST_RC" | tee -a "$LOGDIR/run.txt"
    echo "ERROR: remote READY manifest input hash mismatch: $relative local=$expected remote=${got:-MISSING}" >&2
    return 1
  fi
}

precheck_remote_controls() { # <package-root> <manifest> <ready> <terminal> <package>
  local package_root="$1" manifest="$2" ready="$3" terminal="$4" pkg="$5" out
  out="$(shell "if test -e '$package_root' || test -L '$package_root'; then
    if test -d '$package_root' && test ! -L '$package_root'; then
      if test -e '$manifest' || test -L '$manifest' || test -e '$ready' || test -L '$ready' || test -e '$terminal' || test -L '$terminal'; then
        printf MDDS_BOARDTEST_CONTROL_PREEXISTS
      else
        printf MDDS_BOARDTEST_CONTROL_ABSENT
      fi
    else
      printf MDDS_BOARDTEST_PACKAGE_ROOT_INVALID
    fi
  else
    printf MDDS_BOARDTEST_CONTROL_ABSENT
  fi" | tr -d '\r\n')"
  printf 'BOARDTEST_CONTROL_PRECHECK package=%s manifest=%s ready=%s terminal=%s result=%s\n' \
    "$pkg" "$manifest" "$ready" "$terminal" "${out:-NO_MARKER}" | tee -a "$LOGDIR/run.txt"
  if [[ "$out" != "MDDS_BOARDTEST_CONTROL_ABSENT" ]]; then
    echo "ERROR: refusing stale, unreadable, or symlinked READY controls for $pkg: ${out:-NO_MARKER}" >&2
    return 1
  fi
}

prepare_remote_package() { # <package-root> <package>
  local package_root="$1" pkg="$2" tests_root="$ROS2_HOME/tests" out
  # Precheck ran before this removal, so a reused RUN_ID/NONCE cannot erase and
  # then recreate a stale hidden READY/terminal marker.  The path is derived
  # from a validated package component and stays inside the test root.
  out="$(shell "if test -d '$ROS2_HOME' && test ! -L '$ROS2_HOME' && mkdir -p '$tests_root' && test -d '$tests_root' && test ! -L '$tests_root' && rm -rf '$package_root' && mkdir -p '$package_root' && test -d '$package_root' && test ! -L '$package_root'; then printf MDDS_BOARDTEST_PACKAGE_READY; else printf MDDS_BOARDTEST_PACKAGE_SETUP_FAILED; fi" | tr -d '\r\n')"
  if [[ "$out" != "MDDS_BOARDTEST_PACKAGE_READY" ]]; then
    echo "ERROR: failed to reset and verify remote package directory for $pkg: ${out:-NO_MARKER}" >&2
    return 1
  fi
}

transfer_ready_manifest() { # <remote-manifest-path> <package>
  local remote_manifest="$1" pkg="$2" got push_rc=0
  push "$READY_MANIFEST_LOCAL" "$remote_manifest" || push_rc=$?
  if wait_remote_regular_sha "$remote_manifest" "$READY_MANIFEST_SHA256"; then
    got="$REMOTE_HASH_WAIT_DIGEST"
    printf 'BOARDTEST_READY_MANIFEST_TRANSFER package=%s hdc_rc=%s verify_attempts=%s readback_nonzero=%s readback_last_rc=%s result=EXACT\n' \
      "$pkg" "$push_rc" "$REMOTE_HASH_WAIT_ATTEMPTS" "$REMOTE_HASH_WAIT_NONZERO_READBACKS" "$REMOTE_HASH_WAIT_LAST_RC" | tee -a "$LOGDIR/run.txt"
  else
    got="$REMOTE_HASH_WAIT_DIGEST"
    printf 'BOARDTEST_READY_MANIFEST_TRANSFER package=%s hdc_rc=%s verify_attempts=%s readback_nonzero=%s readback_last_rc=%s result=MISMATCH\n' \
      "$pkg" "$push_rc" "$REMOTE_HASH_WAIT_ATTEMPTS" "$REMOTE_HASH_WAIT_NONZERO_READBACKS" "$REMOTE_HASH_WAIT_LAST_RC" | tee -a "$LOGDIR/run.txt"
    echo "ERROR: remote READY manifest hash mismatch for $pkg: local=$READY_MANIFEST_SHA256 remote=${got:-MISSING}" >&2
    return 1
  fi
}

verify_remote_ready_and_create() { # <package-root> <package> <manifest> <ready>
  local package_root="$1" pkg="$2" manifest="$3" ready="$4" remote_cmd out ready_line ready_line_sha header
  ready_line="MDDS_BOARDTEST_READY RUN_ID=$RUN_ID NONCE=$RUN_NONCE PACKAGE=$pkg MANIFEST_SHA=$READY_MANIFEST_SHA256"
  # The board's /bin/sh does not provide tr.  Bind the control record to the
  # SHA-256 of its exact expected bytes (including the one LF written by
  # printf) instead of normalizing CR/LF away.  This rejects CR, a missing
  # final LF, and every extra record or byte.
  ready_line_sha="$(printf '%s\n' "$ready_line" | sha256sum | cut -d ' ' -f1)"
  if ! valid_sha256 "$ready_line_sha"; then
    echo "ERROR: failed to derive exact READY record digest for $pkg" >&2
    return 1
  fi
  header="MDDS_BOARDTEST_MANIFEST V=1 RUN_ID=$RUN_ID NONCE=$RUN_NONCE PACKAGE=$pkg"
  # Parse the exact hash-verified manifest on the board rather than expanding
  # one shell predicate per file on the host.  That keeps large packages below
  # HDC command-length limits while still re-hashing every driver/exe/lib/
  # fixture entry before a READY record can exist.
  remote_cmd="valid=1; entries=0; if test -f '$manifest' && test ! -L '$manifest' && test \"\$(sha256sum '$manifest' 2>/dev/null | cut -d ' ' -f1)\" = '$READY_MANIFEST_SHA256' && exec 3< '$manifest'; then IFS= read -r manifest_header <&3 || valid=0; test \"\$manifest_header\" = '$header' || valid=0; while IFS= read -r manifest_entry <&3; do case \"\$manifest_entry\" in SHA256=*' PATH='*) manifest_sha=\${manifest_entry#SHA256=}; manifest_sha=\${manifest_sha%% PATH=*}; manifest_path=\${manifest_entry#* PATH=} ;; *) valid=0; break ;; esac; case \"\$manifest_sha\" in ''|*[!0-9a-f]*) valid=0 ;; esac; test \${#manifest_sha} -eq 64 || valid=0; case \"\$manifest_path\" in ''|/*|*'//'*|.|..|./*|../*|*/.|*/..|*/./*|*/../*|*[!A-Za-z0-9._/-]*) valid=0 ;; esac; if test \"\$valid\" = 1 && test -f '$package_root/'\"\$manifest_path\" && test ! -L '$package_root/'\"\$manifest_path\" && test \"\$(sha256sum '$package_root/'\"\$manifest_path\" 2>/dev/null | cut -d ' ' -f1)\" = \"\$manifest_sha\"; then entries=\$((entries + 1)); else valid=0; fi; done; exec 3<&-; if test \"\$valid\" = 1 && test \"\$entries\" -gt 0; then if (umask 077; set -C; printf '%s\\n' '$ready_line' > '$ready') 2>/dev/null && test -f '$ready' && test ! -L '$ready' && test \"\$(sha256sum '$ready' 2>/dev/null | cut -d ' ' -f1)\" = '$ready_line_sha'; then printf MDDS_BOARDTEST_READY_OK; else printf MDDS_BOARDTEST_READY_WRITE_FAILED; fi; else printf MDDS_BOARDTEST_READY_INPUT_HASH_BAD; fi; else printf MDDS_BOARDTEST_READY_MANIFEST_HASH_BAD; fi"
  out="$(shell "$remote_cmd" | tr -d '\r\n')"
  printf 'BOARDTEST_READY_REMOTE package=%s manifest_sha256=%s result=%s\n' \
    "$pkg" "$READY_MANIFEST_SHA256" "${out:-NO_MARKER}" | tee -a "$LOGDIR/run.txt"
  if [[ "$out" != "MDDS_BOARDTEST_READY_OK" ]]; then
    echo "ERROR: remote READY verification/create failed for $pkg: ${out:-NO_MARKER}" >&2
    return 1
  fi
  READY_RECORD_LINE="$ready_line"
}

verify_remote_ready_record() { # <package> <ready-path>
  local pkg="$1" ready="$2" expected expected_sha remote_sha result
  expected="MDDS_BOARDTEST_READY RUN_ID=$RUN_ID NONCE=$RUN_NONCE PACKAGE=$pkg MANIFEST_SHA=$READY_MANIFEST_SHA256"
  # The control record is an exact byte contract: its one canonical line plus
  # exactly one LF.  Do not accept a parsed-looking readback after stripping
  # CR/LF, because that would make CRLF, a missing LF, or appended records
  # indistinguishable from the expected record.
  expected_sha="$(printf '%s\n' "$expected" | sha256sum | cut -d ' ' -f1)"
  if remote_regular_sha "$ready"; then
    remote_sha="$REMOTE_HASH_READBACK_DIGEST"
  else
    remote_sha=""
  fi
  if [[ "$remote_sha" == "$expected_sha" ]]; then
    result="EXACT"
  else
    result="MISSING_OR_INVALID"
  fi
  printf 'BOARDTEST_READY_RECORD package=%s path=%s result=%s\n' \
    "$pkg" "$ready" "$result" \
    | tee -a "$LOGDIR/run.txt"
  [[ "$remote_sha" == "$expected_sha" ]]
}

verify_archive_controls() { # <package> <archive> <manifest> <ready> <terminal>
  local pkg="$1" archive="$2" manifest="$3" ready="$4" terminal="$5"
  local manifest_name ready_name terminal_name manifest_member ready_member terminal_member
  local listed manifest_digest ready_digest terminal_digest expected_ready expected_ready_sha expected_terminal_sha
  manifest_name="$(basename "$manifest")"
  ready_name="$(basename "$ready")"
  terminal_name="$(basename "$terminal")"
  manifest_member="$pkg/$manifest_name"
  ready_member="$pkg/$ready_name"
  terminal_member="$pkg/$terminal_name"
  for listed in "$manifest_member" "$ready_member" "$terminal_member"; do
    if [[ "$(local_archive_tar -tf "$archive" | tr -d '\r' | grep -Fxc "$listed")" != "1" ]]; then
      echo "ERROR: archive lacks exactly one READY control entry for $pkg: $listed" >&2
      return 1
    fi
  done
  manifest_digest="$(local_archive_tar -xOf "$archive" "$manifest_member" | sha256sum | cut -d ' ' -f1)" || return 1
  if [[ "$manifest_digest" != "$READY_MANIFEST_SHA256" ]]; then
    echo "ERROR: archive READY manifest hash mismatch for $pkg: expected=$READY_MANIFEST_SHA256 got=${manifest_digest:-MISSING}" >&2
    return 1
  fi
  expected_ready="MDDS_BOARDTEST_READY RUN_ID=$RUN_ID NONCE=$RUN_NONCE PACKAGE=$pkg MANIFEST_SHA=$READY_MANIFEST_SHA256"
  expected_ready_sha="$(printf '%s\n' "$expected_ready" | sha256sum | cut -d ' ' -f1)"
  expected_terminal_sha="$(printf '%s\n' "$TERMINAL_LINE" | sha256sum | cut -d ' ' -f1)"
  ready_digest="$(local_archive_tar -xOf "$archive" "$ready_member" | sha256sum | cut -d ' ' -f1)" || return 1
  terminal_digest="$(local_archive_tar -xOf "$archive" "$terminal_member" | sha256sum | cut -d ' ' -f1)" || return 1
  if [[ "$ready_digest" != "$expected_ready_sha" || "$terminal_digest" != "$expected_terminal_sha" ]]; then
    echo "ERROR: archive READY/terminal exact-byte digest mismatch for $pkg" >&2
    return 1
  fi
  printf 'BOARDTEST_ARCHIVE_CONTROLS package=%s manifest_sha256=%s ready=EXACT terminal=EXACT\n' \
    "$pkg" "$manifest_digest" | tee -a "$LOGDIR/run.txt"
}

# Create the shared directory atomically and then create/read back the exact
# owner record.  Existing, incomplete, or malformed locks are intentionally
# indistinguishable from a live owner: all are fail-closed and are never
# removed by this test runner.
acquire_activity_lock() {
  local out
  out="$(shell "if (umask 077; mkdir '$ACTIVITY_LOCK_DIR') 2>/dev/null; then
    if (umask 077; set -C; printf '%s\\n' '$ACTIVITY_LOCK_OWNER' > '$ACTIVITY_LOCK_DIR/owner') 2>/dev/null && \\
      test -d '$ACTIVITY_LOCK_DIR' && test ! -L '$ACTIVITY_LOCK_DIR' && \\
      test -f '$ACTIVITY_LOCK_DIR/owner' && test ! -L '$ACTIVITY_LOCK_DIR/owner' && \\
      test \"\$(cat '$ACTIVITY_LOCK_DIR/owner' 2>/dev/null)\" = '$ACTIVITY_LOCK_OWNER'; then
      printf MDDS_ACTIVITY_LOCK_ACQUIRED
    else
      printf MDDS_ACTIVITY_LOCK_OWNER_WRITE_FAILED
    fi
  else
    printf MDDS_ACTIVITY_LOCK_BUSY_OR_MALFORMED
  fi" | tr -d '\r\n')"
  printf 'BOARDTEST_ACTIVITY_LOCK board=%s owner=%s result=%s\n' \
    "$BOARD" "$ACTIVITY_LOCK_OWNER" "${out:-NO_MARKER}" | tee -a "$LOGDIR/run.txt"
  if [[ "$out" != "MDDS_ACTIVITY_LOCK_ACQUIRED" ]]; then
    echo "ERROR: MDDS activity lock is held, malformed, or could not be created on $BOARD: ${out:-NO_MARKER}" >&2
    return 1
  fi
  ACTIVITY_LOCK_HELD=1
}

# The owner comparison and non-symlink check happen on the board immediately
# before removal.  If it fails, preserve the directory for its actual owner or
# for manual recovery rather than deleting a lock that this invocation cannot
# prove it owns.
release_activity_lock() {
  local out
  [ "$ACTIVITY_LOCK_HELD" -eq 1 ] || return 0
  out="$(shell "if test -d '$ACTIVITY_LOCK_DIR' && test ! -L '$ACTIVITY_LOCK_DIR' && \\
      test -f '$ACTIVITY_LOCK_DIR/owner' && test ! -L '$ACTIVITY_LOCK_DIR/owner' && \\
      test \"\$(cat '$ACTIVITY_LOCK_DIR/owner' 2>/dev/null)\" = '$ACTIVITY_LOCK_OWNER'; then
    rm -f '$ACTIVITY_LOCK_DIR/owner' && rmdir '$ACTIVITY_LOCK_DIR' && printf MDDS_ACTIVITY_LOCK_RELEASED
  else
    printf MDDS_ACTIVITY_LOCK_NOT_OWNED
  fi" | tr -d '\r\n')"
  printf 'BOARDTEST_ACTIVITY_LOCK_RELEASE board=%s owner=%s result=%s\n' \
    "$BOARD" "$ACTIVITY_LOCK_OWNER" "${out:-NO_MARKER}" | tee -a "$LOGDIR/run.txt"
  if [[ "$out" != "MDDS_ACTIVITY_LOCK_RELEASED" ]]; then
    echo "ERROR: could not release this run's MDDS activity lock on $BOARD: ${out:-NO_MARKER}" >&2
    return 1
  fi
  ACTIVITY_LOCK_HELD=0
}

finish_activity_lock() {
  local rc="$1"
  # Avoid recursing through EXIT if a board connection fails while releasing.
  trap - EXIT INT TERM HUP
  if [ "$ACTIVITY_LOCK_HELD" -eq 1 ] && [ "$SAFE_TO_RELEASE" -ne 1 ]; then
    echo "ERROR: board-driver completion is unproven; retaining activity lock for manual recovery" >&2
    [ "$rc" -eq 0 ] && rc=1
  elif [ "$ACTIVITY_LOCK_HELD" -eq 1 ] && ! release_activity_lock; then
    echo "ERROR: board test activity lock remains for manual recovery" >&2
    [ "$rc" -eq 0 ] && rc=1
  fi
  exit "$rc"
}

handle_activity_signal() {
  # HDC does not provide a durable PID identity for the synchronous board test
  # driver.  On an interrupt we cannot prove it has stopped, so retain the
  # activity lock for explicit recovery instead of allowing a deploy or a
  # second test run to overlap it.
  echo "ERROR: interrupted by $1; retaining board-test activity lock for manual recovery" >&2
  trap - EXIT INT TERM HUP
  exit 128
}

trap 'finish_activity_lock "$?"' EXIT
trap 'handle_activity_signal INT' INT
trap 'handle_activity_signal TERM' TERM
trap 'handle_activity_signal HUP' HUP

# Archive the untouched board test directory after its driver exits.  Board A
# can wedge HDC when a file receive operation retrieves a tar file, so evidence
# travels exclusively as bounded Base64 over an HDC shell stream.  The decoded
# local bytes must match an independently announced remote hash; HDC status and
# stderr are retained as diagnostics, not accepted as integrity authority.
prepare_archive_stream_dir() {
  local raw out
  raw="$(shell "if command -v base64 >/dev/null 2>&1 && command -v head >/dev/null 2>&1 && head -c 0 /dev/null >/dev/null 2>&1; then ensure_mdds_dir() { dir=\$1; if test -e \"\$dir\" || test -L \"\$dir\"; then test -d \"\$dir\" && test ! -L \"\$dir\"; else (umask 077; mkdir \"\$dir\") && test -d \"\$dir\" && test ! -L \"\$dir\"; fi; }; parent='$ARCHIVE_REMOTE_PARENT'; run_dir='$ARCHIVE_REMOTE_RUN_DIR'; nonce_dir='$ARCHIVE_REMOTE_DIR'; if ensure_mdds_dir \"\$parent\" && ensure_mdds_dir \"\$run_dir\"; then if test -e \"\$nonce_dir\" || test -L \"\$nonce_dir\"; then printf MDDS_BOARDTEST_ARCHIVE_RUN_PREEXISTS; elif (umask 077; mkdir \"\$nonce_dir\") && test -d \"\$nonce_dir\" && test ! -L \"\$nonce_dir\"; then printf MDDS_BOARDTEST_ARCHIVE_STREAM_READY; else printf MDDS_BOARDTEST_ARCHIVE_RUN_CREATE_FAILED; fi; else printf MDDS_BOARDTEST_ARCHIVE_PARENT_INVALID; fi; else printf MDDS_BOARDTEST_ARCHIVE_STREAM_UNAVAILABLE; fi" || true)"
  out="$(strict_hdc_single_line "$raw")" || out=""
  printf 'BOARDTEST_ARCHIVE_TRANSPORT board=%s run_id=%s nonce=%s result=%s\n' \
    "$BOARD" "$RUN_ID" "$RUN_NONCE" "${out:-NO_MARKER}" | tee -a "$LOGDIR/run.txt"
  if [[ "$out" != "MDDS_BOARDTEST_ARCHIVE_STREAM_READY" ]]; then
    echo "ERROR: cannot create isolated Base64 archive directory on $BOARD: ${out:-NO_MARKER}" >&2
    return 1
  fi
  ARCHIVE_STREAM_READY=1
}

parse_archive_metadata() { # <raw-output> <package>
  local raw="$1" pkg="$2" line sha bytes
  local -a fields=()
  ARCHIVE_METADATA_SHA=""
  ARCHIVE_METADATA_BYTES=""
  ARCHIVE_METADATA_LINE=""
  line="$(strict_hdc_single_line "$raw")" || return 1
  read -r -a fields <<< "$line"
  if [[ "${#fields[@]}" -ne 7 ||
        "${fields[0]}" != "MDDS_BOARDTEST_ARCHIVE_META" ||
        "${fields[1]}" != "V=1" ||
        "${fields[2]}" != "RUN_ID=$RUN_ID" ||
        "${fields[3]}" != "NONCE=$RUN_NONCE" ||
        "${fields[4]}" != "PACKAGE=$pkg" ||
        "${fields[5]}" != SHA256=* ||
        "${fields[6]}" != BYTES=* ]]; then
    return 1
  fi
  sha="${fields[5]#SHA256=}"
  bytes="${fields[6]#BYTES=}"
  valid_sha256 "$sha" || return 1
  is_canonical_positive_decimal "$bytes" || return 1
  decimal_leq "$bytes" "$ARCHIVE_MAX_BYTES" || return 1
  [[ "$line" == "MDDS_BOARDTEST_ARCHIVE_META V=1 RUN_ID=$RUN_ID NONCE=$RUN_NONCE PACKAGE=$pkg SHA256=$sha BYTES=$bytes" ]] || return 1
  ARCHIVE_METADATA_SHA="$sha"
  ARCHIVE_METADATA_BYTES="$bytes"
  ARCHIVE_METADATA_LINE="$line"
}

capture_package_evidence() { # <package>
  local pkg="$1" remote_archive local_archive
  local metadata_out metadata_err metadata_raw metadata_rc=0 metadata_out_bytes metadata_out_sha metadata_err_bytes metadata_err_sha
  local stream_b64 stream_tmp stream_err want want_bytes got got_bytes
  local stream_rc=0 decode_rc=0 encoded_bytes err_bytes err_sha
  local final_bytes final_sha metadata_result
  if [ "$ARCHIVE_STREAM_READY" -ne 1 ]; then
    echo "ERROR: Base64 archive transport was not prepared before driver execution" >&2
    return 1
  fi
  remote_archive="$ARCHIVE_REMOTE_DIR/${pkg}.tar"
  local_archive="$LOGDIR/${pkg}.remote.tar"
  metadata_out="$LOGDIR/${pkg}.archive_create.stdout"
  metadata_err="$LOGDIR/${pkg}.archive_create.stderr"
  stream_err="$LOGDIR/${pkg}.archive_stream.stderr"
  if [[ -e "$local_archive" || -L "$local_archive" ||
        -e "$metadata_out" || -L "$metadata_out" ||
        -e "$metadata_err" || -L "$metadata_err" ||
        -e "$stream_err" || -L "$stream_err" ]]; then
    echo "ERROR: refusing pre-existing local board evidence target for $pkg" >&2
    return 1
  fi
  if ! create_exclusive_local_file "$metadata_out" || ! create_exclusive_local_file "$metadata_err"; then
    echo "ERROR: cannot create exclusive archive-create diagnostics for $pkg" >&2
    return 1
  fi
  timeout "$ARCHIVE_STREAM_TIMEOUT_SEC" "$HDC" -t "$BOARD" shell "archive='$remote_archive'; if test -e \"\$archive\" || test -L \"\$archive\"; then exit 70; fi; tar -C '$ROS2_HOME/tests' -cf \"\$archive\" '$pkg' || exit 70; test -f \"\$archive\" && test ! -L \"\$archive\" || exit 70; archive_sha=\$(sha256sum \"\$archive\" 2>/dev/null | cut -d ' ' -f1) || exit 70; case \"\$archive_sha\" in ''|*[!0-9a-f]*) exit 70 ;; esac; test \${#archive_sha} -eq 64 || exit 70; set -- \$(wc -c < \"\$archive\") || exit 70; archive_bytes=\$1; case \"\$archive_bytes\" in ''|0|0*|*[!0-9]*) exit 70 ;; esac; printf 'MDDS_BOARDTEST_ARCHIVE_META V=1 RUN_ID=$RUN_ID NONCE=$RUN_NONCE PACKAGE=$pkg SHA256=%s BYTES=%s' \"\$archive_sha\" \"\$archive_bytes\"" \
    </dev/null > "$metadata_out" 2> "$metadata_err" || metadata_rc=$?
  metadata_out_bytes="$(local_regular_bytes "$metadata_out")" || return 1
  metadata_out_sha="$(local_regular_sha "$metadata_out")" || return 1
  metadata_err_bytes="$(local_regular_bytes "$metadata_err")" || return 1
  metadata_err_sha="$(local_regular_sha "$metadata_err")" || return 1
  metadata_raw="$(< "$metadata_out")"
  metadata_result=INVALID
  if parse_archive_metadata "$metadata_raw" "$pkg"; then
    metadata_result=VALID
  fi
  printf 'BOARDTEST_ARCHIVE_CREATE package=%s remote=%s result=%s transport_rc=%s stdout_bytes=%s stdout_sha256=%s stderr_bytes=%s stderr_sha256=%s\n' \
    "$pkg" "$remote_archive" "$metadata_result" "$metadata_rc" "$metadata_out_bytes" "$metadata_out_sha" "$metadata_err_bytes" "$metadata_err_sha" | tee -a "$LOGDIR/run.txt"
  if [[ "$metadata_result" != VALID ]]; then
    echo "ERROR: archive metadata was not one bounded canonical record for $pkg" >&2
    return 1
  fi
  want="$ARCHIVE_METADATA_SHA"
  want_bytes="$ARCHIVE_METADATA_BYTES"
  stream_b64="$(mktemp "$LOGDIR/.${pkg}.archive-base64.XXXXXX")" || return 1
  stream_tmp="$(mktemp "$LOGDIR/.${pkg}.archive-decoded.XXXXXX")" || { rm -f "$stream_b64"; return 1; }
  if ! create_exclusive_local_file "$stream_err"; then
    echo "ERROR: cannot create exclusive archive-stream stderr record for $pkg" >&2
    rm -f "$stream_b64" "$stream_tmp"
    return 1
  fi
  # head -c binds the amount sent to the announced size.  A replacement or
  # growth race cannot make the host consume beyond its budget; a shrink or
  # content change is rejected by the decoded byte count and SHA-256 below.
  timeout "$ARCHIVE_STREAM_TIMEOUT_SEC" "$HDC" -t "$BOARD" shell "if test -f '$remote_archive' && test ! -L '$remote_archive'; then head -c '$want_bytes' '$remote_archive' | base64; fi" \
    </dev/null > "$stream_b64" 2>> "$stream_err" || stream_rc=$?
  encoded_bytes="$(local_regular_bytes "$stream_b64")" || { rm -f "$stream_b64" "$stream_tmp"; return 1; }
  tr -d '\r' < "$stream_b64" | base64 -d > "$stream_tmp" 2>> "$stream_err" || decode_rc=$?
  err_bytes="$(local_regular_bytes "$stream_err")" || { rm -f "$stream_b64" "$stream_tmp"; return 1; }
  err_sha="$(local_regular_sha "$stream_err")" || { rm -f "$stream_b64" "$stream_tmp"; return 1; }
  printf 'BOARDTEST_ARCHIVE_STREAM package=%s transport=base64_shell remote_bytes=%s encoded_bytes=%s transport_rc=%s decode_rc=%s stderr_bytes=%s stderr_sha256=%s\n' \
    "$pkg" "$want_bytes" "$encoded_bytes" "$stream_rc" "$decode_rc" "$err_bytes" "$err_sha" | tee -a "$LOGDIR/run.txt"
  rm -f "$stream_b64"
  if [ "$decode_rc" -ne 0 ]; then
    echo "ERROR: failed to Base64-decode board evidence archive for $pkg" >&2
    rm -f "$stream_tmp"
    return 1
  fi
  got_bytes="$(local_regular_bytes "$stream_tmp")" || { rm -f "$stream_tmp"; return 1; }
  got="$(local_regular_sha "$stream_tmp")" || { rm -f "$stream_tmp"; return 1; }
  if [ "$got_bytes" != "$want_bytes" ] || [ "$got" != "$want" ]; then
    echo "ERROR: board evidence archive stream mismatch for $pkg local_bytes=$got_bytes remote_bytes=$want_bytes local_sha=$got remote_sha=$want" >&2
    rm -f "$stream_tmp"
    return 1
  fi
  if ! (set -C; cat "$stream_tmp" > "$local_archive") 2>/dev/null; then
    echo "ERROR: cannot publish exclusive local board evidence archive for $pkg" >&2
    rm -f "$stream_tmp"
    return 1
  fi
  rm -f "$stream_tmp"
  final_bytes="$(local_regular_bytes "$local_archive")" || return 1
  final_sha="$(local_regular_sha "$local_archive")" || return 1
  if [ "$final_bytes" != "$want_bytes" ] || [ "$final_sha" != "$want" ]; then
    echo "ERROR: published board evidence archive hash mismatch for $pkg" >&2
    return 1
  fi
  printf 'BOARDTEST_ARCHIVE package=%s remote=%s sha256=%s bytes=%s\n' \
    "$pkg" "$remote_archive" "$final_sha" "$final_bytes" | tee -a "$LOGDIR/run.txt"
  CAPTURED_ARCHIVE="$local_archive"
}

# HDC can disconnect after it has started the board-side driver.  A returned
# command substitution or an empty stdout is therefore not completion proof.
# The board shell writes this exact terminal record only after run_tests_board
# returns; the record itself is then included in the hash-verified archive.
verify_driver_terminal() { # <package> <terminal-path> <manifest-sha256>
  local pkg="$1" terminal_path="$2" manifest_sha="$3" raw line prefix rc result canonical_sha remote_sha
  # Keep the record-validity state separate from the driver verdict.  A
  # non-zero RC is still an authentic terminal record that must be retained
  # and checked in the evidence archive, but it is never a successful gate.
  TERMINAL_LINE=""
  DRIVER_TERMINAL_RC=""
  DRIVER_TERMINAL_RECORD_VALID=0
  # Read only the first logical line for the canonical RC parser.  The
  # separate SHA-256 comparison below binds that parsed line to the complete
  # remote file, so an omitted LF, CRLF, trailing blank line, extra record, or
  # any other byte cannot be accepted.  HDC may append one transport CR to a
  # no-newline printf result; removing only that framing byte is safe because
  # the remote file itself is never normalized before its digest is checked.
  raw="$(shell "if test -f '$terminal_path' && test ! -L '$terminal_path'; then IFS= read -r terminal_line < '$terminal_path' || :; printf '%s' \"\$terminal_line\"; fi" || true)"
  line="${raw%$'\r'}"
  prefix="MDDS_BOARDTEST_TERMINAL RUN_ID=$RUN_ID NONCE=$RUN_NONCE PACKAGE=$pkg MANIFEST_SHA=$manifest_sha RC="
  case "$line" in
    "$prefix"*)
      rc=${line#"$prefix"}
      # The board shell writes $? in canonical decimal.  Rejecting other
      # spellings makes a forged or malformed terminal record fail closed.
      case "$rc" in
        0) ;;
        [1-9]*)
          case "$rc" in *[!0-9]*) rc="" ;; esac
          ;;
        *) rc="" ;;
      esac
      ;;
    *) rc="" ;;
  esac
  if [ -z "$rc" ]; then
    result="MISSING_OR_INVALID"
  else
    canonical_sha="$(printf '%s\n' "$line" | sha256sum | cut -d ' ' -f1)"
    if remote_regular_sha "$terminal_path"; then
      remote_sha="$REMOTE_HASH_READBACK_DIGEST"
    else
      remote_sha=""
    fi
    if [[ "$remote_sha" != "$canonical_sha" ]]; then
      result="MISSING_OR_NONCANONICAL"
    else
      TERMINAL_LINE="$line"
      DRIVER_TERMINAL_RC="$rc"
      DRIVER_TERMINAL_RECORD_VALID=1
      if [ "$rc" = "0" ]; then
        result="RC=0 OK"
      else
        result="RC=$rc NONZERO_FAILURE"
      fi
    fi
  fi
  printf 'BOARDTEST_DRIVER_TERMINAL package=%s path=%s result=%s\n' \
    "$pkg" "$terminal_path" "$result" \
    | tee -a "$LOGDIR/run.txt"
  if [ "$DRIVER_TERMINAL_RECORD_VALID" -eq 1 ] && [ "$DRIVER_TERMINAL_RC" = "0" ]; then
    return 0
  fi
  # A valid terminal record with a non-zero driver status is deliberately a
  # failure.  The caller still captures and verifies its archive before
  # retaining the activity lock and failing the package gate.
  [ "$DRIVER_TERMINAL_RECORD_VALID" -eq 1 ] && return 2
  return 1
}

# A failed HDC target selection used to look like a clean 0/0/0 test result.
# Verify the exact selected board before copying or executing any test binary.
if ! shell "printf BOARDTEST_HDC_READY" | tr -d '\r' | grep -Fxq BOARDTEST_HDC_READY; then
  echo "ERROR: cannot contact selected board $BOARD through $HDC" >&2
  exit 2
fi
archive_capability_raw="$(shell "if command -v base64 >/dev/null 2>&1 && command -v head >/dev/null 2>&1 && head -c 0 /dev/null >/dev/null 2>&1; then printf MDDS_BOARDTEST_ARCHIVE_STREAM_READY; else printf MDDS_BOARDTEST_ARCHIVE_STREAM_UNAVAILABLE; fi" || true)"
archive_capability="$(strict_hdc_single_line "$archive_capability_raw")" || archive_capability=""
printf 'BOARDTEST_ARCHIVE_CAPABILITY board=%s result=%s\n' \
  "$BOARD" "${archive_capability:-NO_MARKER}" | tee -a "$LOGDIR/run.txt"
if [[ "$archive_capability" != "MDDS_BOARDTEST_ARCHIVE_STREAM_READY" ]]; then
  echo "ERROR: selected board $BOARD lacks usable base64/head -c archive streaming before payload transfer" >&2
  exit 2
fi

# From this point until EXIT, the lock covers every remote test-directory
# mutation, payload send, test process, and raw-evidence capture below.
if ! acquire_activity_lock; then
  exit 3
fi
if ! prepare_archive_stream_dir; then
  # This failure happens before any package payload or driver launch.  The
  # archive directory may remain for audit, but there is no live test whose
  # ownership would require retaining the shared board activity lock.
  SAFE_TO_RELEASE=1
  exit 4
fi

total_pass=0; total_fail=0; total_skip=0
failed_tests=()
archive_fail=0
driver_completion_fail=0
driver_terminal_rc_fail=0
attempted_packages=0

for pkg in "${PKGS[@]}"; do
  dir="build_ohos/$pkg"
  [ -f "$dir/CTestTestfile.cmake" ] || { echo "== $pkg: no CTestTestfile, skipped"; continue; }
  # collect native test executables + helper libs (build-tree layout kept)
  mapfile -t exes < <(find "$dir" -type f \
    -not -path "*/CMakeFiles/*" -not -path "*/.cmake/*" \
    -not -path "*/gtest/*" -not -path "*/gmock/*" \
    -exec file {} + \
    | grep "ELF 64-bit" | grep -i "aarch64" | grep -iE "executable|interpreter" | cut -d: -f1 \
    | grep -v "/benchmark_")
  [ ${#exes[@]} -eq 0 ] && { echo "== $pkg: no test executables, skipped"; continue; }
  mapfile -t libs < <(find "$dir" -type f -name "*.so" -not -path "*/.cmake/*" -not -path "*/CMakeFiles/*")
  echo "== $pkg: ${#exes[@]} executables, ${#libs[@]} helper libs"
  # driver script replaying the ament test fixtures
  driver="build_ohos/$pkg/run_tests_board.sh"
  if ! pixi run python scripts/_parse_ctest_env.py \
    "$WS_ROOT/$dir/CTestTestfile.cmake" "$pkg" "$WS_ROOT" | tr -d '\r' > "$driver"; then
    echo "ERROR: failed to generate board driver for $pkg" >&2
    driver_completion_fail=1
    break
  fi

  reset_ready_manifest
  package_root="$ROS2_HOME/tests/$pkg"
  manifest_path="$package_root/.mdds_boardtest_manifest_${RUN_ID}_${RUN_NONCE}"
  ready_path="$package_root/.mdds_boardtest_ready_${RUN_ID}_${RUN_NONCE}"
  terminal_path="$package_root/.mdds_boardtest_terminal_${RUN_ID}_${RUN_NONCE}"
  tmpfix=""
  package_setup_failed=0

  add_ready_manifest_file "run_tests_board.sh" "$driver" || package_setup_failed=1
  for f in "${exes[@]}" ${libs[@]+"${libs[@]}"}; do
    rel="${f#$dir/}"
    add_ready_manifest_file "$rel" "$f" || { package_setup_failed=1; break; }
  done
  # fixture resource files referenced relative to the test working directory
  # (e.g. rcutils' <cwd>/test/dummy_readable_file.txt). Files go through CR
  # stripping (the Windows checkout has CRLF and tests assert byte counts
  # computed on LF content), except known binary formats.
  srcdir="${PKG_SRC[$pkg]:-}"
  has_fixtures=0
  if [ "$package_setup_failed" -eq 0 ] && [ -n "$srcdir" ] && [ -d "$srcdir/test" ]; then
    has_fixtures=1
    tmpfix="$(mktemp -d)" || package_setup_failed=1
    mapfile -d '' -t fixture_paths < <(cd "$srcdir" && find test -type f -print0 | LC_ALL=C sort -z)
    for rel in "${fixture_paths[@]}"; do
      if ! safe_relative_path "$rel"; then
        echo "ERROR: refusing unsafe fixture path for $pkg: $rel" >&2
        package_setup_failed=1
        break
      fi
      case "$rel" in
        *.so|*.png|*.jpg|*.jpeg|*.bin|*.db3|*.mcap|*.bag|*.gz|*.zip|*.urdf)
          fixture_local="$srcdir/$rel"
          ;;
        *)
          mkdir -p "$tmpfix/$(dirname "$rel")" || { package_setup_failed=1; break; }
          tr -d '\r' < "$srcdir/$rel" > "$tmpfix/$rel" || { package_setup_failed=1; break; }
          fixture_local="$tmpfix/$rel"
          ;;
      esac
      add_ready_manifest_file "$rel" "$fixture_local" || { package_setup_failed=1; break; }
    done
  fi
  if [ "$package_setup_failed" -ne 0 ]; then
    [ -z "$tmpfix" ] || rm -rf "$tmpfix"
    driver_completion_fail=1
    break
  fi
  if ! write_ready_manifest "$pkg"; then
    [ -z "$tmpfix" ] || rm -rf "$tmpfix"
    driver_completion_fail=1
    break
  fi
  # All three controls are checked before clearing the package tree.  That
  # makes a caller-selected reused RUN_ID/NONCE fail closed rather than erase
  # and recreate a terminal/READY/manifest from a previous invocation.
  if ! precheck_remote_controls "$package_root" "$manifest_path" "$ready_path" "$terminal_path" "$pkg" ||
    ! prepare_remote_package "$package_root" "$pkg" ||
    ! transfer_ready_manifest "$manifest_path" "$pkg"; then
    [ -z "$tmpfix" ] || rm -rf "$tmpfix"
    driver_completion_fail=1
    break
  fi
  for rel in "${READY_MANIFEST_PATHS[@]}"; do
    if ! transfer_manifest_file "$package_root" "$rel"; then
      package_setup_failed=1
      break
    fi
  done
  if [ "$package_setup_failed" -eq 0 ] && [ "$has_fixtures" -eq 1 ]; then
    remote_mkdir_verified "$package_root/test" "fixture directory $pkg" || package_setup_failed=1
  fi
  # tests using the compiled-in BUILD_DIR macro get the host path; mirror it
  # (relative, so "C:" becomes a plain directory) under the test dir
  if [ "$package_setup_failed" -eq 0 ]; then
    remote_mkdir_verified "$package_root/$WS_ROOT/build_ohos/$pkg" "BUILD_DIR mirror $pkg" || package_setup_failed=1
  fi
  # likewise for macros baking the package SOURCE dir (e.g. rviz_common's
  # _TEST_PLUGIN_DESCRIPTIONS): point the mirrored src path at the pushed
  # test/ fixtures via a symlink
  if [ "$package_setup_failed" -eq 0 ] && [ "$has_fixtures" -eq 1 ]; then
    abssrc="$(cd "$srcdir" && (pwd -W 2>/dev/null || pwd))"
    setup_out="$(shell "if mkdir -p '$package_root/$abssrc' && test -d '$package_root/$abssrc' && test ! -L '$package_root/$abssrc' && ln -s '$package_root/test' '$package_root/$abssrc/test' && test -L '$package_root/$abssrc/test'; then printf MDDS_BOARDTEST_SOURCE_MIRROR_READY; else printf MDDS_BOARDTEST_SOURCE_MIRROR_FAILED; fi" | tr -d '\r\n')"
    if [[ "$setup_out" != "MDDS_BOARDTEST_SOURCE_MIRROR_READY" ]]; then
      echo "ERROR: failed to create source fixture mirror for $pkg: ${setup_out:-NO_MARKER}" >&2
      package_setup_failed=1
    fi
  fi
  # hdc file send does not preserve the exec bit
  if [ "$package_setup_failed" -eq 0 ]; then
    setup_out="$(shell "if cd '$package_root' && find . -type f -exec chmod +x {} +; then printf MDDS_BOARDTEST_CHMOD_READY; else printf MDDS_BOARDTEST_CHMOD_FAILED; fi" | tr -d '\r\n')"
    if [[ "$setup_out" != "MDDS_BOARDTEST_CHMOD_READY" ]]; then
      echo "ERROR: failed to mark transferred test files executable for $pkg: ${setup_out:-NO_MARKER}" >&2
      package_setup_failed=1
    fi
  fi
  if [ "$package_setup_failed" -eq 0 ] && ! verify_remote_ready_and_create "$package_root" "$pkg" "$manifest_path" "$ready_path"; then
    package_setup_failed=1
  fi
  if [ "$package_setup_failed" -eq 0 ] && ! verify_remote_ready_record "$pkg" "$ready_path"; then
    package_setup_failed=1
  fi
  [ -z "$tmpfix" ] || rm -rf "$tmpfix"
  if [ "$package_setup_failed" -ne 0 ]; then
    echo "ERROR: READY setup did not complete for $pkg; no board driver will run and the activity lock is retained" >&2
    driver_completion_fail=1
    break
  fi
  # HDC's status is not completion authority.  The guard rechecks the exact
  # manifest hash and READY line immediately before executing the driver, then
  # emits a create-only terminal bound to the same package and manifest hash.
  ready_record_sha="$(printf '%s\n' "$READY_RECORD_LINE" | sha256sum | cut -d ' ' -f1)"
  if ! valid_sha256 "$ready_record_sha"; then
    echo "ERROR: failed to derive exact READY record digest before driver for $pkg" >&2
    driver_completion_fail=1
    break
  fi
  attempted_packages=$((attempted_packages + 1))
  out="$(shell "cd '$package_root' || exit 70; if test -f '$manifest_path' && test ! -L '$manifest_path' && test \"\$(sha256sum '$manifest_path' 2>/dev/null | cut -d ' ' -f1)\" = '$READY_MANIFEST_SHA256' && test -f '$ready_path' && test ! -L '$ready_path' && test \"\$(sha256sum '$ready_path' 2>/dev/null | cut -d ' ' -f1)\" = '$ready_record_sha'; then rc=0; sh ./run_tests_board.sh || rc=\$?; terminal_line=\"MDDS_BOARDTEST_TERMINAL RUN_ID=$RUN_ID NONCE=$RUN_NONCE PACKAGE=$pkg MANIFEST_SHA=$READY_MANIFEST_SHA256 RC=\$rc\"; terminal_sha=\"\$(printf '%s\\n' \"\$terminal_line\" | sha256sum | cut -d ' ' -f1)\"; if (umask 077; set -C; printf '%s\\n' \"\$terminal_line\" > '$terminal_path') 2>/dev/null && test -f '$terminal_path' && test ! -L '$terminal_path' && test \"\$(sha256sum '$terminal_path' 2>/dev/null | cut -d ' ' -f1)\" = \"\$terminal_sha\"; then exit \"\$rc\"; else exit 70; fi; else exit 70; fi" || true)"
  driver_stdout="$LOGDIR/${pkg}.driver.stdout"
  driver_stdout_ok=1
  if ! (set -C; printf '%s\n' "$out" > "$driver_stdout") 2>/dev/null; then
    echo "ERROR: refusing to overwrite driver stdout evidence for $pkg: $driver_stdout" >&2
    driver_stdout_ok=0
    driver_completion_fail=1
  fi
  terminal_record_ok=1
  terminal_rc_ok=1
  archive_ok=1
  [ "$driver_stdout_ok" -eq 1 ] || terminal_record_ok=0
  if ! verify_driver_terminal "$pkg" "$terminal_path" "$READY_MANIFEST_SHA256"; then
    if [ "$DRIVER_TERMINAL_RECORD_VALID" -eq 1 ]; then
      terminal_rc_ok=0
      driver_terminal_rc_fail=1
      printf 'BOARDTEST_DRIVER_TERMINAL_FAILURE package=%s rc=%s action=RETAIN_LOCK_AND_FAIL_GATE\n' \
        "$pkg" "$DRIVER_TERMINAL_RC" | tee -a "$LOGDIR/run.txt"
      echo "ERROR: board driver for $pkg recorded non-zero terminal RC=$DRIVER_TERMINAL_RC; retaining activity lock and failing gate" >&2
    else
      terminal_record_ok=0
      driver_completion_fail=1
    fi
  fi
  verify_remote_ready_record "$pkg" "$ready_path" || { terminal_record_ok=0; driver_completion_fail=1; }
  if ! capture_package_evidence "$pkg"; then
    archive_ok=0
    archive_fail=1
  elif [ "$terminal_record_ok" -eq 1 ] && ! verify_archive_controls "$pkg" "$CAPTURED_ARCHIVE" "$manifest_path" "$ready_path" "$terminal_path"; then
    archive_ok=0
    archive_fail=1
  fi
  while IFS= read -r line; do
    case "$line" in
      "BOARDTEST "*" PASS"*)
        total_pass=$((total_pass+1)) ;;
      "BOARDTEST "*" SKIP"*)
        total_skip=$((total_skip+1)) ;;
      BOARDTEST*FAIL*)
        name="$(printf '%s' "$line" | awk '{print $2}')"
        if is_known_skip "$pkg/$name"; then
          total_skip=$((total_skip+1))
          echo "   SKIP(known) $pkg/$name"
          continue
        fi
        total_fail=$((total_fail+1)); failed_tests+=("$pkg/$name")
        echo "   FAIL $pkg/$name"
        shell "grep -E 'FAILED|Failure|Error' $ROS2_HOME/tests/$pkg/$name.log 2>/dev/null | head -3" | sed 's/^/      /'
        ;;
    esac
  done <<< "$out"
  if [ "$terminal_record_ok" -ne 1 ] || [ "$terminal_rc_ok" -ne 1 ] || [ "$archive_ok" -ne 1 ]; then
    echo "ERROR: terminal/READY/archive provenance or terminal RC failed for $pkg; retaining activity lock and stopping later packages" >&2
    break
  fi
done

echo
echo "== board test summary: $total_pass passed, $total_fail failed, $total_skip skipped =="
if [ $((total_pass + total_fail + total_skip)) -eq 0 ]; then
  echo "ERROR: no BOARDTEST verdicts were collected; refusing to report a pass" >&2
  exit 1
fi
if [ ${#failed_tests[@]} -gt 0 ]; then
  printf '   %s\n' "${failed_tests[@]}"
fi
if [ "$archive_fail" -ne 0 ]; then
  echo "ERROR: one or more board raw-evidence archives could not be captured" >&2
fi
if [ "$driver_completion_fail" -ne 0 ]; then
  echo "ERROR: one or more board drivers lack a verified terminal marker; retaining lock" >&2
fi
if [ "$driver_terminal_rc_fail" -ne 0 ]; then
  echo "ERROR: one or more board drivers recorded a non-zero terminal RC; retaining lock and failing gate" >&2
fi
if [ "$archive_fail" -ne 0 ] || [ "$driver_completion_fail" -ne 0 ] || [ "$driver_terminal_rc_fail" -ne 0 ]; then
  exit 1
fi
# Release requires a verified successful terminal (RC=0) as well as the
# matching archive.  Any driver-reported test/assertion failure leaves the
# activity lock for explicit recovery instead of turning lost HDC stdout into
# a false successful gate.
SAFE_TO_RELEASE=1
[ "$total_fail" -eq 0 ]
