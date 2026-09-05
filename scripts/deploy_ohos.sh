#!/usr/bin/env bash
# Hash-sealed, rollback-capable full ROS 2 deployment for KaihongOS/RK3588A.
# Run from the workspace root in Git Bash:
#   ./scripts/deploy_ohos.sh [board ...]
# Optional: RMW=rmw_mdds (or another safe identifier) pins the default RMW.
set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/lib/mdds_sha256_manifest.sh

HDC="${HDC:-/c/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
RMW="${RMW:-}"
DEVICE_DIR=/data/local/tmp/ros2
DEVICE_PARENT=/data/local/tmp
ARCHIVE="$(pwd)/ros2_ohos_install.tar.gz"
ENV_TEMPLATE="$(pwd)/scripts/env_ohos.template.sh"
BOARDS=("$@")
if [ ${#BOARDS[@]} -eq 0 ]; then
  BOARDS=(3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00)
fi

if [[ -n "$RMW" && ! "$RMW" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
  echo "ERROR: RMW must be empty or one safe implementation identifier" >&2
  exit 2
fi
declare -A SEEN_BOARDS=()
for board in "${BOARDS[@]}"; do
  [[ "$board" =~ ^[A-Za-z0-9_.-]+$ ]] || {
    echo "ERROR: unsafe board identifier: $board" >&2
    exit 2
  }
  if [[ -n "${SEEN_BOARDS[$board]+present}" ]]; then
    echo "ERROR: duplicate board identifier: $board" >&2
    exit 2
  fi
  SEEN_BOARDS["$board"]=1
done

for tool in "$HDC" tar sha256sum find sort xargs cygpath cp mv sed wc grep seq sleep mktemp; do
  if [[ "$tool" == */* ]]; then
    [ -x "$tool" ] || { echo "ERROR: required executable not found: $tool" >&2; exit 2; }
  else
    command -v "$tool" >/dev/null 2>&1 || { echo "ERROR: required tool not found: $tool" >&2; exit 2; }
  fi
done
[ -d install_ohos ] || { echo "ERROR: install_ohos/ not found; run scripts/build_ohos.sh first" >&2; exit 1; }
[ -f "$ENV_TEMPLATE" ] && [ ! -L "$ENV_TEMPLATE" ] || {
  echo "ERROR: environment template must be one regular non-symlink file: $ENV_TEMPLATE" >&2
  exit 1
}
for reserved in deploy_manifest.sha256 env.sh .mdds_deploy_complete .mdds-activity-lock; do
  if [[ -e "install_ohos/$reserved" || -L "install_ohos/$reserved" ]]; then
    echo "ERROR: install_ohos contains reserved deployment control path: $reserved" >&2
    exit 1
  fi
done

required_paths=(
  bin/mdds_token_exec
  Lib/demo_nodes_cpp/talker
  Lib/demo_nodes_cpp/listener
  Lib/libmdds.so
  Lib/librmw_mdds.so
  Lib/mdds_gateway/mdds_gateway
  share/rmw_mdds/config/ohos_dsoftbus.env
)
for path in "${required_paths[@]}"; do
  if [[ ! -f "install_ohos/$path" || -L "install_ohos/$path" ]]; then
    echo "ERROR: required deployment artifact must be one regular non-symlink file: install_ohos/$path" >&2
    exit 1
  fi
done

# Flatten vendor DSOs and include the exact OHOS libc++ runtime.  Missing
# required inputs are fatal rather than hidden by `|| true`.
for vendordir in install_ohos/opt/*_vendor/lib; do
  [ -d "$vendordir" ] || continue
  shopt -s nullglob
  vendor_libs=("$vendordir"/lib*.so*)
  shopt -u nullglob
  [ ${#vendor_libs[@]} -eq 0 ] || cp -a "${vendor_libs[@]}" install_ohos/Lib/
done
OHOS_NATIVE="${OHOS_NATIVE_SDK:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/native}"
LIBCXX="$OHOS_NATIVE/llvm/lib/aarch64-linux-ohos/libc++_shared.so"
[ -f "$LIBCXX" ] || { echo "ERROR: OHOS libc++ runtime not found: $LIBCXX" >&2; exit 1; }
cp -f "$LIBCXX" install_ohos/Lib/

if [ -f install_ohos/Lib/python3.12/dist-packages/PyKDL.so ]; then
  mv install_ohos/Lib/python3.12/dist-packages/PyKDL.so \
    install_ohos/Lib/site-packages/PyKDL.cpython-312-aarch64-linux-ohos.so
fi
[ ! -d install_ohos/share/ament_index ] || \
  find install_ohos/share/ament_index -type f -exec sed -i 's/\r$//' {} +

TMP_BASE="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
HOST_TMP="$(mktemp -d "$TMP_BASE/mdds-full-deploy.XXXXXX")"
case "$HOST_TMP" in "$TMP_BASE"/mdds-full-deploy.*) ;; *) exit 70 ;; esac
cleanup_host() {
  case "$HOST_TMP" in "$TMP_BASE"/mdds-full-deploy.*) rm -rf -- "$HOST_TMP" ;; esac
}
trap cleanup_host EXIT
MANIFEST="$HOST_TMP/install.sha256"
MANIFEST_RAW="$HOST_TMP/install.raw.sha256"
ENV_FILE="$HOST_TMP/env.sh"
ARCHIVE_TMP="$HOST_TMP/install.tar.gz"

(cd install_ohos && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) > "$MANIFEST_RAW"
[ -s "$MANIFEST_RAW" ] || { echo "ERROR: generated raw install manifest is empty" >&2; exit 1; }
mdds_normalize_sha256_manifest "$MANIFEST_RAW" "$MANIFEST"
[ -s "$MANIFEST" ] || { echo "ERROR: generated install manifest is empty" >&2; exit 1; }
# Prove the normalized text is still accepted by the producer-side
# implementation before it crosses the host/board tool boundary.
(cd install_ohos && sha256sum -c "$MANIFEST" >/dev/null)
MANIFEST_SHA="$(sha256sum "$MANIFEST" | cut -d ' ' -f1)"
tar -C install_ohos -czf "$ARCHIVE_TMP" .
ARCHIVE_SHA="$(sha256sum "$ARCHIVE_TMP" | cut -d ' ' -f1)"
ARCHIVE_BYTES="$(wc -c < "$ARCHIVE_TMP" | tr -d ' ')"

RUN_ID="${MDDS_DEPLOY_RUN_ID:-deploy_$(date +%Y%m%dT%H%M%S)_${RANDOM}_${RANDOM}_$$}"
[[ "$RUN_ID" =~ ^[A-Za-z0-9_.-]+$ ]] || {
  echo "ERROR: MDDS_DEPLOY_RUN_ID must contain only A-Za-z0-9_.-" >&2
  exit 2
}
MARKER="MDDS_DEPLOY_COMPLETE V=1 RUN_ID=$RUN_ID ARCHIVE_SHA256=$ARCHIVE_SHA MANIFEST_SHA256=$MANIFEST_SHA"
{
  printf 'MDDS_DEPLOY_EXPECTED_MARKER=%q\n' "$MARKER"
  cat "$ENV_TEMPLATE"
  [ -z "$RMW" ] || printf 'export RMW_IMPLEMENTATION=%s\n' "$RMW"
} > "$ENV_FILE"
ENV_SHA="$(sha256sum "$ENV_FILE" | cut -d ' ' -f1)"

# Publish only after every local digest is fixed.  The familiar archive path
# remains available to release tooling, but is replaced atomically.
mv -f "$ARCHIVE_TMP" "$ARCHIVE"
printf 'DEPLOY_LOCAL RUN_ID=%s ARCHIVE_SHA256=%s ARCHIVE_BYTES=%s MANIFEST_SHA256=%s ENV_SHA256=%s\n' \
  "$RUN_ID" "$ARCHIVE_SHA" "$ARCHIVE_BYTES" "$MANIFEST_SHA" "$ENV_SHA"

export MSYS2_ARG_CONV_EXCL='*'
remote() { "$HDC" -t "$1" shell "$2" </dev/null; }
strict_line() {
  local value
  value="$1"
  # HDC may append one transport CR.  Strip only that byte and reject every
  # other CR/LF so a malformed multi-record response cannot normalize into a
  # successful transaction marker.
  value="${value%$'\r'}"
  [[ "$value" != *$'\r'* && "$value" != *$'\n'* ]] || return 1
  printf '%s' "$value"
}
remote_sha() { # board path
  remote "$1" "if test -f '$2' && test ! -L '$2'; then sha256sum '$2' 2>/dev/null | cut -d ' ' -f1; fi" | tr -d '\r\n '
}
send_verified() { # board local remote expected
  local board="$1" local_path="$2" remote_path="$3" expected="$4" got="" attempt send_rc=0
  "$HDC" -t "$board" file send "$(cygpath -aw "$local_path")" "$remote_path" </dev/null >/dev/null || send_rc=$?
  for attempt in $(seq 1 10); do
    got="$(remote_sha "$board" "$remote_path" || true)"
    if [ "$got" = "$expected" ]; then
      if [ "$send_rc" -ne 0 ]; then
        echo "ERROR: hdc file send returned $send_rc despite an exact readback on $board: $remote_path" >&2
        return 1
      fi
      return 0
    fi
    sleep 1
  done
  echo "ERROR: transfer failed on $board: $remote_path hdc_rc=$send_rc expected=$expected got=${got:-MISSING}" >&2
  return 1
}

release_owned_lock() { # board owner
  local board="$1" owner="$2" out
  out="$(remote "$board" "lock='$DEVICE_DIR/.mdds-activity-lock'; if test -d \"\$lock\" && test ! -L \"\$lock\" && test -f \"\$lock/owner\" && test ! -L \"\$lock/owner\" && grep -Fqx '$owner' \"\$lock/owner\" && test \"\$(find \"\$lock\" -mindepth 1 -maxdepth 1)\" = \"\$lock/owner\"; then if rm -f \"\$lock/owner\" && rmdir \"\$lock\"; then printf DEPLOY_LOCK_RELEASED; else printf DEPLOY_LOCK_RELEASE_FAILED; fi; else printf DEPLOY_LOCK_NOT_OWNED; fi" || true)"
  out="$(strict_line "$out" || true)"
  printf 'DEPLOY_LOCK_RELEASE board=%s owner=%s result=%s\n' \
    "$board" "$owner" "${out:-NO_MARKER}"
  [ "$out" = DEPLOY_LOCK_RELEASED ]
}

deploy_board() {
  local board="$1" nonce owner stage backup archive_remote manifest_remote env_remote out
  nonce="${RUN_ID}_${board}"
  owner="MDDS_ACTIVITY_LOCK MODE=DEPLOY RUN_ID=$RUN_ID NONCE=$nonce OWNER=deploy_ohos"
  stage="$DEVICE_PARENT/.ros2-stage-$nonce"
  backup="$DEVICE_PARENT/.ros2-backup-$nonce"
  archive_remote="$DEVICE_PARENT/.ros2-archive-$nonce.tar.gz"
  manifest_remote="$DEVICE_PARENT/.ros2-manifest-$nonce.sha256"
  env_remote="$DEVICE_PARENT/.ros2-env-$nonce.sh"

  echo "== deploy to $board =="
  out="$(remote "$board" "if mkdir -p '$DEVICE_DIR' && test -d '$DEVICE_DIR' && test ! -L '$DEVICE_DIR' && (umask 077; mkdir '$DEVICE_DIR/.mdds-activity-lock') 2>/dev/null; then if (umask 077; set -C; printf '%s\\n' '$owner' > '$DEVICE_DIR/.mdds-activity-lock/owner') 2>/dev/null && test -d '$DEVICE_DIR/.mdds-activity-lock' && test ! -L '$DEVICE_DIR/.mdds-activity-lock' && test -f '$DEVICE_DIR/.mdds-activity-lock/owner' && test ! -L '$DEVICE_DIR/.mdds-activity-lock/owner' && test \"\$(cat '$DEVICE_DIR/.mdds-activity-lock/owner' 2>/dev/null)\" = '$owner' && test \"\$(find '$DEVICE_DIR/.mdds-activity-lock' -mindepth 1 -maxdepth 1)\" = '$DEVICE_DIR/.mdds-activity-lock/owner'; then printf DEPLOY_LOCK_ACQUIRED; else printf DEPLOY_LOCK_OWNER_FAILED; fi; else printf DEPLOY_LOCK_BUSY; fi" || true)"
  out="$(strict_line "$out" || true)"
  [ "$out" = DEPLOY_LOCK_ACQUIRED ] || {
    # The remote transaction may have created the lock and written our exact
    # owner before a later verification step failed.  Release only that
    # byte-for-byte owner; a busy/foreign lock is deliberately preserved.
    release_owned_lock "$board" "$owner" || \
      echo "ERROR: partial deployment lock remains on $board for manual recovery" >&2
    echo "ERROR: deployment lock unavailable on $board: ${out:-NO_MARKER}" >&2
    return 1
  }

  out="$(remote "$board" "if test -e '$stage' || test -L '$stage' || test -e '$backup' || test -L '$backup' || test -e '$archive_remote' || test -L '$archive_remote' || test -e '$manifest_remote' || test -L '$manifest_remote' || test -e '$env_remote' || test -L '$env_remote'; then printf DEPLOY_PATH_CONFLICT; elif (umask 077; mkdir '$stage') && test -d '$stage' && test ! -L '$stage'; then printf DEPLOY_STAGE_CREATED; else printf DEPLOY_STAGE_CREATE_FAILED; fi" || true)"
  out="$(strict_line "$out" || true)"
  if [ "$out" != DEPLOY_STAGE_CREATED ]; then
    release_owned_lock "$board" "$owner" || \
      echo "ERROR: deployment lock remains on $board for manual recovery" >&2
    echo "ERROR: isolated stage unavailable on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi

  if ! send_verified "$board" "$ARCHIVE" "$archive_remote" "$ARCHIVE_SHA" || \
     ! send_verified "$board" "$MANIFEST" "$manifest_remote" "$MANIFEST_SHA" || \
     ! send_verified "$board" "$ENV_FILE" "$env_remote" "$ENV_SHA"; then
    remote "$board" "rm -rf '$stage'; rm -f '$archive_remote' '$manifest_remote' '$env_remote'" >/dev/null 2>&1 || true
    release_owned_lock "$board" "$owner" || \
      echo "ERROR: deployment lock remains on $board for manual recovery" >&2
    return 1
  fi

  out="$(remote "$board" "set -- \$(wc -c < '$archive_remote' 2>/dev/null); remote_bytes=\$1; if test \"\$(sha256sum '$archive_remote' | cut -d ' ' -f1)\" != '$ARCHIVE_SHA' || test \"\$remote_bytes\" != '$ARCHIVE_BYTES'; then printf DEPLOY_ARCHIVE_BAD; elif ! tar -xzf '$archive_remote' -C '$stage'; then printf DEPLOY_EXTRACT_FAILED; elif ! cp '$manifest_remote' '$stage/deploy_manifest.sha256' || ! cp '$env_remote' '$stage/env.sh'; then printf DEPLOY_CONTROL_COPY_FAILED; elif test \"\$(sha256sum '$stage/deploy_manifest.sha256' | cut -d ' ' -f1)\" != '$MANIFEST_SHA' || test \"\$(sha256sum '$stage/env.sh' | cut -d ' ' -f1)\" != '$ENV_SHA'; then printf DEPLOY_CONTROL_HASH_BAD; elif ! (cd '$stage' && sha256sum -c deploy_manifest.sha256 >/dev/null 2>&1); then printf DEPLOY_TREE_HASH_BAD; elif ! test -f '$stage/bin/mdds_token_exec' || test -L '$stage/bin/mdds_token_exec' || ! test -f '$stage/Lib/demo_nodes_cpp/talker' || test -L '$stage/Lib/demo_nodes_cpp/talker' || ! test -f '$stage/Lib/demo_nodes_cpp/listener' || test -L '$stage/Lib/demo_nodes_cpp/listener' || ! test -f '$stage/Lib/libmdds.so' || test -L '$stage/Lib/libmdds.so' || ! test -f '$stage/Lib/librmw_mdds.so' || test -L '$stage/Lib/librmw_mdds.so' || ! test -f '$stage/Lib/mdds_gateway/mdds_gateway' || test -L '$stage/Lib/mdds_gateway/mdds_gateway' || ! test -f '$stage/share/rmw_mdds/config/ohos_dsoftbus.env' || test -L '$stage/share/rmw_mdds/config/ohos_dsoftbus.env'; then printf DEPLOY_REQUIRED_MISSING; elif ! find '$stage/Lib' -type f -exec chmod +x {} + || ! find '$stage/bin' -type f -exec chmod +x {} + || ! chmod +x '$stage/env.sh'; then printf DEPLOY_CHMOD_FAILED; elif { test -e '$stage/lib' || test -L '$stage/lib'; } && ! test -L '$stage/lib'; then printf DEPLOY_LIB_CONFLICT; elif ! test -e '$stage/lib' && ! test -L '$stage/lib' && ! ln -s Lib '$stage/lib'; then printf DEPLOY_LIB_LINK_FAILED; elif ! (umask 077; mkdir '$stage/.mdds-activity-lock') || ! (umask 077; set -C; printf '%s\\n' '$owner' > '$stage/.mdds-activity-lock/owner') 2>/dev/null || test \"\$(cat '$stage/.mdds-activity-lock/owner' 2>/dev/null)\" != '$owner'; then printf DEPLOY_STAGE_LOCK_FAILED; elif ! (umask 077; set -C; printf '%s\\n' '$MARKER' > '$stage/.mdds_deploy_complete') 2>/dev/null; then printf DEPLOY_MARKER_FAILED; else printf DEPLOY_STAGE_VERIFIED; fi" || true)"
  out="$(strict_line "$out" || true)"
  if [ "$out" != DEPLOY_STAGE_VERIFIED ]; then
    remote "$board" "rm -rf '$stage'; rm -f '$archive_remote' '$manifest_remote' '$env_remote'" >/dev/null 2>&1 || true
    release_owned_lock "$board" "$owner" || \
      echo "ERROR: deployment lock remains on $board for manual recovery" >&2
    echo "ERROR: staged tree verification failed on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi

  out="$(remote "$board" "if ! mv '$DEVICE_DIR' '$backup'; then printf DEPLOY_BACKUP_RENAME_FAILED; elif mv '$stage' '$DEVICE_DIR'; then printf DEPLOY_COMMITTED; elif mv '$backup' '$DEVICE_DIR'; then printf DEPLOY_COMMIT_ROLLED_BACK; else printf DEPLOY_COMMIT_ROLLBACK_FAILED; fi" || true)"
  out="$(strict_line "$out" || true)"
  if [ "$out" != DEPLOY_COMMITTED ]; then
    if [ "$out" = DEPLOY_BACKUP_RENAME_FAILED ] || [ "$out" = DEPLOY_COMMIT_ROLLED_BACK ]; then
      remote "$board" "rm -rf '$stage'; rm -f '$archive_remote' '$manifest_remote' '$env_remote'" >/dev/null 2>&1 || true
      release_owned_lock "$board" "$owner" || \
        echo "ERROR: deployment lock remains on $board for manual recovery" >&2
    fi
    echo "ERROR: atomic deployment commit failed on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi

  out="$(remote "$board" "if test \"\$(cat '$DEVICE_DIR/.mdds_deploy_complete' 2>/dev/null)\" = '$MARKER' && test \"\$(sha256sum '$DEVICE_DIR/deploy_manifest.sha256' | cut -d ' ' -f1)\" = '$MANIFEST_SHA' && test \"\$(sha256sum '$DEVICE_DIR/env.sh' | cut -d ' ' -f1)\" = '$ENV_SHA' && (cd '$DEVICE_DIR' && sha256sum -c deploy_manifest.sha256 >/dev/null 2>&1) && test -x '$DEVICE_DIR/bin/mdds_token_exec' && test -x '$DEVICE_DIR/Lib/demo_nodes_cpp/talker' && test -x '$DEVICE_DIR/Lib/demo_nodes_cpp/listener' && test -x '$DEVICE_DIR/Lib/mdds_gateway/mdds_gateway' && . '$DEVICE_DIR/env.sh' >/dev/null 2>&1 && test \"\$ROS2_CLI_WRAPPER\" = '$DEVICE_DIR/env.sh:ros2'; then printf DEPLOY_POSTCHECK_OK; else printf DEPLOY_POSTCHECK_FAILED; fi" || true)"
  out="$(strict_line "$out" || true)"
  if [ "$out" != DEPLOY_POSTCHECK_OK ]; then
    out="$(remote "$board" "failed='$DEVICE_PARENT/.ros2-failed-$nonce'; if test -f '$DEVICE_DIR/.mdds-activity-lock/owner' && test ! -L '$DEVICE_DIR/.mdds-activity-lock/owner' && grep -Fqx '$owner' '$DEVICE_DIR/.mdds-activity-lock/owner' && test -d '$backup' && test ! -L '$backup' && test -f '$backup/.mdds-activity-lock/owner' && test ! -L '$backup/.mdds-activity-lock/owner' && grep -Fqx '$owner' '$backup/.mdds-activity-lock/owner' && ! test -e \"\$failed\" && ! test -L \"\$failed\"; then if mv '$DEVICE_DIR' \"\$failed\"; then if mv '$backup' '$DEVICE_DIR'; then if rm -rf \"\$failed\" && rm -f '$archive_remote' '$manifest_remote' '$env_remote'; then printf DEPLOY_POSTCHECK_ROLLED_BACK; else printf DEPLOY_POSTCHECK_ROLLBACK_CLEANUP_FAILED; fi; else mv \"\$failed\" '$DEVICE_DIR' >/dev/null 2>&1 || true; printf DEPLOY_POSTCHECK_ROLLBACK_FAILED; fi; else printf DEPLOY_POSTCHECK_ROLLBACK_FAILED; fi; else printf DEPLOY_POSTCHECK_ROLLBACK_PRECONDITION_FAILED; fi" || true)"
    out="$(strict_line "$out" || true)"
    if [ "$out" = DEPLOY_POSTCHECK_ROLLED_BACK ] || [ "$out" = DEPLOY_POSTCHECK_ROLLBACK_CLEANUP_FAILED ]; then
      release_owned_lock "$board" "$owner" || \
        echo "ERROR: restored deployment lock remains on $board for manual recovery" >&2
    fi
    echo "ERROR: committed deployment failed post-check on $board: ${out:-NO_ROLLBACK_MARKER}" >&2
    return 1
  fi

  out="$(remote "$board" "if test -d '$DEVICE_DIR/.mdds-activity-lock' && test ! -L '$DEVICE_DIR/.mdds-activity-lock' && test -f '$DEVICE_DIR/.mdds-activity-lock/owner' && test ! -L '$DEVICE_DIR/.mdds-activity-lock/owner' && grep -Fqx '$owner' '$DEVICE_DIR/.mdds-activity-lock/owner' && test \"\$(find '$DEVICE_DIR/.mdds-activity-lock' -mindepth 1 -maxdepth 1)\" = '$DEVICE_DIR/.mdds-activity-lock/owner'; then if rm -rf '$backup' && rm -f '$archive_remote' '$manifest_remote' '$env_remote' && rm -f '$DEVICE_DIR/.mdds-activity-lock/owner' && rmdir '$DEVICE_DIR/.mdds-activity-lock'; then printf DEPLOY_FINALIZED; else printf DEPLOY_FINALIZE_CLEANUP_FAILED; fi; else printf DEPLOY_FINALIZE_NOT_OWNED; fi" || true)"
  out="$(strict_line "$out" || true)"
  [ "$out" = DEPLOY_FINALIZED ] || {
    echo "ERROR: deployment committed but cleanup/lock release is unproven on $board: ${out:-NO_MARKER}" >&2
    return 1
  }
  printf 'DEPLOY_BOARD board=%s result=PASS archive_sha256=%s manifest_sha256=%s env_sha256=%s\n' \
    "$board" "$ARCHIVE_SHA" "$MANIFEST_SHA" "$ENV_SHA"
}

for board in "${BOARDS[@]}"; do
  deploy_board "$board"
done
echo "== deploy done: all boards hash-verified =="
