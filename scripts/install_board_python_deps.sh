#!/usr/bin/env bash
# Atomically install the exact hash-bound ROS Python overlay on RK3588A boards.
set -euo pipefail
cd "$(dirname "$0")/.."

PREFLIGHT_ONLY=0
if [ "${1:-}" = "--preflight-only" ]; then
  PREFLIGHT_ONLY=1
  shift
fi

PYTHON="${HOST_PYTHON:-$(pwd)/.pixi/envs/default/python.exe}"
if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3 || true)"
fi
[ -n "$PYTHON" ] && [ -x "$PYTHON" ] || { echo "host Python 3 is required" >&2; exit 1; }

LOCK="${PYTHON_TARGET_LOCK:-$(pwd)/scripts/python/ohos_python.lock.json}"
MANAGER="$(pwd)/scripts/python_target.py"
ARTIFACT_MANAGER="$(pwd)/scripts/python_runtime_artifact.py"
SITE_PKGS="${PYTHON_SITEPKGS_DIR:-$(pwd)/python_target/sitepkgs}"
PY_TARGET="${PYTHON_TARGET_ROOT:-$(pwd)/python_target/usr}"
STAGE_MARKER=.ros2-ohos-python-stage.json
DEPLOYMENT_MARKER=.ros2-ohos-python-deployment.json
FILE_MANIFEST=.ros2-ohos-python-files.sha256
PATH_INVENTORY=.ros2-ohos-python-paths.txt

# Fail closed before the first HDC invocation. Command substitution preserves
# the verifier's non-zero status (unlike mapfile + process substitution).
[ -d "$SITE_PKGS" ] || { echo "Python dependency stage is missing: $SITE_PKGS" >&2; exit 1; }
ENTRY_OUTPUT="$("$PYTHON" "$MANAGER" --lock "$LOCK" verify-stage \
  --site "$SITE_PKGS" --print-entries | tr -d '\r')" || exit 1
[ -n "$ENTRY_OUTPUT" ] || { echo "Python dependency stage is empty" >&2; exit 1; }
mapfile -t ENTRIES <<<"$ENTRY_OUTPUT"
"$PYTHON" "$MANAGER" --lock "$LOCK" verify-runtime --root "$PY_TARGET" >/dev/null

for name in "${ENTRIES[@]}"; do
  [[ "$name" =~ ^[-A-Za-z0-9_.+]+$ ]] || {
    echo "unsafe staged entry name: $name" >&2
    exit 1
  }
  [ "$name" != . ] && [ "$name" != .. ] && [[ "$name" != -* ]] || {
    echo "unsafe staged entry name: $name" >&2
    exit 1
  }
  [ -e "$SITE_PKGS/$name" ] || { echo "staged entry vanished: $name" >&2; exit 1; }
done

RUNTIME_MANIFEST="${PYTHON_RUNTIME_ARTIFACT_MANIFEST:-}"
RUNTIME_ARCHIVE="${PYTHON_RUNTIME_ARTIFACT_ARCHIVE:-}"
REQUIRE_RUNTIME_ARTIFACT="${PYTHON_REQUIRE_RUNTIME_ARTIFACT:-0}"
case "$REQUIRE_RUNTIME_ARTIFACT" in
  0|1) ;;
  *) echo "PYTHON_REQUIRE_RUNTIME_ARTIFACT must be 0 or 1" >&2; exit 2 ;;
esac
if [ -n "$RUNTIME_MANIFEST" ] || [ -n "$RUNTIME_ARCHIVE" ]; then
  [ -n "$RUNTIME_MANIFEST" ] && [ -f "$RUNTIME_MANIFEST" ] || {
    echo "runtime artifact manifest is missing: $RUNTIME_MANIFEST" >&2
    exit 1
  }
  [ -n "$RUNTIME_ARCHIVE" ] && [ -f "$RUNTIME_ARCHIVE" ] || {
    echo "runtime artifact archive is missing: $RUNTIME_ARCHIVE" >&2
    exit 1
  }
  "$PYTHON" "$ARTIFACT_MANAGER" --lock "$LOCK" verify \
    --archive "$RUNTIME_ARCHIVE" --manifest "$RUNTIME_MANIFEST" >/dev/null
elif [ "$REQUIRE_RUNTIME_ARTIFACT" -eq 1 ] || [ "$PREFLIGHT_ONLY" -eq 0 ]; then
  echo "hash-bound runtime artifact is required but was not supplied" >&2
  exit 1
fi

SOURCE_MODE=artifact-reproducible-not-source-reproducible
SOURCE_SHA=NOT_APPLICABLE
SOURCE_LOCK_SHA=NOT_APPLICABLE
SOURCE_RECIPE_SHA=NOT_APPLICABLE
if [ -n "$RUNTIME_MANIFEST" ]; then
  SOURCE_OUTPUT="$("$PYTHON" scripts/print_python_source_binding.py --manifest "$RUNTIME_MANIFEST" --lock "$LOCK" | tr -d '\r')" || exit 1
  mapfile -t SOURCE_FIELDS <<<"$SOURCE_OUTPUT"
  [ "${#SOURCE_FIELDS[@]}" -eq 5 ] || exit 1
  SOURCE_MODE="${SOURCE_FIELDS[0]}"
  SOURCE_SHA="${SOURCE_FIELDS[2]}"
  SOURCE_LOCK_SHA="${SOURCE_FIELDS[3]}"
  SOURCE_RECIPE_SHA="${SOURCE_FIELDS[4]}"
fi

if [ "$PREFLIGHT_ONLY" -eq 1 ]; then
  echo "python_dependency_preflight=PASS"
  echo "managed_entry_count=${#ENTRIES[@]}"
  "$PYTHON" - "$SITE_PKGS/$STAGE_MARKER" <<'PY'
import json
import sys
payload = json.load(open(sys.argv[1], encoding="utf-8"))
print("python_stage_tree_sha256=" + payload["stage_tree_sha256"])
PY
  exit 0
fi

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
[ -x "$HDC" ] || { echo "HDC is not executable: $HDC" >&2; exit 1; }
BOARDS=("$@")
if [ ${#BOARDS[@]} -eq 0 ]; then
  BOARDS=(3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00)
fi
declare -A SEEN_BOARDS=()
for board in "${BOARDS[@]}"; do
  [[ "$board" =~ ^[A-Za-z0-9._:-]+$ ]] || { echo "unsafe board serial: $board" >&2; exit 2; }
  [ -z "${SEEN_BOARDS[$board]+present}" ] || { echo "duplicate board serial: $board" >&2; exit 2; }
  SEEN_BOARDS[$board]=1
done

PY_REMOTE_PREFIX="${PYTHON_REMOTE_PREFIX:-/data/python312-rk3588a}"
REMOTE_OVERLAY="${PYTHON_REMOTE_OVERLAY:-$PY_REMOTE_PREFIX/ros2-site-packages}"
[[ "$PY_REMOTE_PREFIX" =~ ^/data/[A-Za-z0-9._+-]+$ ]] || {
  echo "refusing unsafe Python runtime prefix: $PY_REMOTE_PREFIX" >&2
  exit 2
}
[[ "$REMOTE_OVERLAY" =~ ^/data/[A-Za-z0-9._+-]+/ros2-site-packages$ ]] || {
  echo "refusing unsafe Python overlay path: $REMOTE_OVERLAY" >&2
  exit 2
}
[ "$REMOTE_OVERLAY" = "$PY_REMOTE_PREFIX/ros2-site-packages" ] || {
  echo "overlay must belong to the selected Python runtime prefix" >&2
  exit 2
}
PY_PREFIX="$PY_REMOTE_PREFIX/usr"
PY_BIN="$PY_PREFIX/bin/python3.12"
PY_SOURCE_CHECK=true
if [ "$SOURCE_MODE" = source-reproducible ]; then
  PY_SOURCE_CHECK="test -f '$PY_REMOTE_PREFIX/PYTHON_SOURCE_BUILD_RECEIPT.json' && test ! -L '$PY_REMOTE_PREFIX/PYTHON_SOURCE_BUILD_RECEIPT.json' && test \"\$(sha256sum '$PY_REMOTE_PREFIX/PYTHON_SOURCE_BUILD_RECEIPT.json' | cut -d ' ' -f1)\" = '$SOURCE_SHA'"
fi
PY_CRYPTO_ENV="OPENSSL_CONF='$PY_PREFIX/etc/ssl/openssl.cnf' OPENSSL_MODULES='$PY_PREFIX/lib/ossl-modules' SSL_CERT_FILE='$PY_PREFIX/etc/ssl/cert.pem' SSL_CERT_DIR='$PY_PREFIX/etc/ssl/certs'"

LOCAL_STAGE_MARKER="$SITE_PKGS/$STAGE_MARKER"
STAGE_MARKER_SHA="$("$PYTHON" -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$LOCAL_STAGE_MARKER" | tr -d '\r')"
RUNTIME_SHA="$("$PYTHON" - "$LOCK" <<'PY' | tr -d '\r'
import json
from pathlib import Path
import sys
lock = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
item = next(value for value in lock["runtime_interface"]["files"]
            if value["path"] == "lib/libpython3.12.so.1.0")
print(item["sha256"])
PY
)"
LOCK_SHA="$("$PYTHON" -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$LOCK" | tr -d '\r')"
RUNTIME_COUNTS="$("$PYTHON" - "$RUNTIME_MANIFEST" <<'PY' | tr -d '\r'
import json, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
for key in ("runtime_entry_count", "runtime_payload_bytes"):
    assert type(value[key]) is int and value[key] > 0
    print(value[key])
PY
)" || exit 1
mapfile -t RUNTIME_COUNT_FIELDS <<<"$RUNTIME_COUNTS"
[ "${#RUNTIME_COUNT_FIELDS[@]}" -eq 2 ] || exit 1
ARTIFACT_TOOL_SHA="$(sha256sum "$ARTIFACT_MANAGER" | cut -d ' ' -f1)"

run_shell_marker() {
  local board="$1"
  local command="$2"
  local marker="$3"
  local output
  local status
  set +e
  output="$(MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$board" shell "set -e; $command" 2>&1)"
  status=$?
  set -e
  printf '%s\n' "$output"
  if [ "$status" -ne 0 ] || ! grep -Eq "^${marker}([[:space:]]|$)" <<<"$output"; then
    echo "device command failed for $board: expected $marker (hdc rc=$status)" >&2
    return 1
  fi
}

send_path() {
  local board="$1"
  local source="$2"
  local destination="$3"
  local source_win
  source_win="$(cygpath -w "$source")"
  MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$board" file send "$source_win" "$destination" >/dev/null
}
. scripts/lib/python_deploy_lease.sh

rollback_overlay() {
  local board="$1"
  local remote_stage="$2"
  local remote_backup="$3"
  # Never delete the backup if both names exist: that state needs inspection.
  MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$board" shell \
    "if [ -e '$remote_backup' ] && [ ! -e '$REMOTE_OVERLAY' ]; then mv '$remote_backup' '$REMOTE_OVERLAY'; fi; rm -rf '$remote_stage'; echo PYTHON_OVERLAY_ROLLBACK_DONE" \
    >/dev/null 2>&1 || true
}

install_one_board() {
  local board="$1"
  local run_id
  local remote_stage
  local remote_backup
  local evidence_dir
  local deployment_marker_local
  local file_manifest_local
  local path_inventory_local
  local deployment_marker_sha
  local file_manifest_sha
  local path_inventory_sha
  local stage_tree_sha
  local archive_sha
  local runtime_tree_sha
  local verify_command
  local verify_output
  local final_output
  local evidence
  local commit_command
  local tree_verify_command
  local final_tree_verify_command
  local remote_actual_inventory
  local final_actual_inventory
  local existing_overlay_check
  local evidence_payload
  local runtime_audit_file
  local runtime_audit_command

  run_id="$(date -u +%Y%m%dT%H%M%SZ)-$$-${RANDOM}${RANDOM}"
  remote_stage="$REMOTE_OVERLAY.stage.$run_id"
  remote_backup="$REMOTE_OVERLAY.rollback"
  evidence_dir="ohos_test_logs/python_deps/$board"
  mkdir -p "$evidence_dir"
  deployment_marker_local="$evidence_dir/deployment_$run_id.json"
  file_manifest_local="$evidence_dir/files_$run_id.sha256"
  path_inventory_local="$evidence_dir/paths_$run_id.txt"

  "$PYTHON" "$MANAGER" --lock "$LOCK" create-overlay-manifests \
    --site "$SITE_PKGS" \
    --file-manifest "$file_manifest_local" \
    --path-inventory "$path_inventory_local" >/dev/null || return 1

  marker_args=(
    --lock "$LOCK" create-deployment-marker
    --site "$SITE_PKGS"
    --output "$deployment_marker_local"
    --board "$board"
    "--remote-runtime-prefix=$PY_REMOTE_PREFIX"
    "--remote-overlay=$REMOTE_OVERLAY"
    --file-manifest "$file_manifest_local"
    --path-inventory "$path_inventory_local"
    --require-runtime-artifact
  )
  if [ -n "$RUNTIME_MANIFEST" ]; then
    marker_args+=(--runtime-artifact-manifest "$RUNTIME_MANIFEST")
  fi
  # Git Bash otherwise rewrites /data/... arguments passed to the Windows
  # host Python into C:/Program Files/Git/data/..., corrupting provenance.
  # Keep conversion enabled for local /c/... paths and exclude only these
  # named POSIX board-path options.
  MSYS2_ARG_CONV_EXCL='--remote-runtime-prefix=;--remote-overlay=' \
    "$PYTHON" "$MANAGER" "${marker_args[@]}" >/dev/null || return 1
  "$PYTHON" scripts/print_python_source_binding.py --manifest "$RUNTIME_MANIFEST" --lock "$LOCK" \
    --deployment-marker "$deployment_marker_local" >/dev/null || return 1

  deployment_marker_sha="$("$PYTHON" -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$deployment_marker_local" | tr -d '\r')"
  readarray -t deployment_fields < <("$PYTHON" - "$deployment_marker_local" <<'PY' | tr -d '\r'
import json
import sys
p = json.load(open(sys.argv[1], encoding="utf-8"))
print(p["python_stage_tree_sha256"])
print(p["python_runtime_archive_sha256"] or "UNAVAILABLE")
print(p["python_runtime_tree_sha256"] or "UNAVAILABLE")
print(p["python_overlay_file_manifest_sha256"])
print(p["python_overlay_path_inventory_sha256"])
PY
  )
  stage_tree_sha="${deployment_fields[0]}"
  archive_sha="${deployment_fields[1]}"
  runtime_tree_sha="${deployment_fields[2]}"
  file_manifest_sha="${deployment_fields[3]}"
  path_inventory_sha="${deployment_fields[4]}"
  existing_overlay_check="test ! -L '$REMOTE_OVERLAY' && test ! -L '$remote_stage' && test ! -L '$remote_backup' && if test -e '$REMOTE_OVERLAY'; then $PY_CRYPTO_ENV LD_LIBRARY_PATH='$PY_PREFIX/lib' LD_PRELOAD='$PY_PREFIX/lib/libpython3.12.so.1.0' '$PY_BIN' -I -B -c 'import json,pathlib; p=pathlib.Path(\"$REMOTE_OVERLAY/$DEPLOYMENT_MARKER\"); assert p.is_file() and not p.is_symlink(); d=json.loads(p.read_text()); assert d.get(\"complete\") is True; assert d.get(\"remote_runtime_prefix\") == \"$PY_REMOTE_PREFIX\"; assert d.get(\"remote_overlay\") == \"$REMOTE_OVERLAY\"; assert d.get(\"python_runtime_archive_sha256\") == \"$archive_sha\"'; fi"

  echo "== $board"
  python_deploy_acquire_lease "$board" "$PY_REMOTE_PREFIX" "overlay-$run_id" || return 1
  runtime_audit_file="/data/local/tmp/python-overlay-runtime-audit-$run_id.py"
  if ! run_shell_marker "$board" "test ! -e '$runtime_audit_file' && test ! -L '$runtime_audit_file' && echo PYTHON_RUNTIME_AUDIT_READY" PYTHON_RUNTIME_AUDIT_READY >/dev/null; then return 1; fi
  send_path "$board" "$ARTIFACT_MANAGER" "$runtime_audit_file" || return 1
  PY_DEPLOY_TEMP_FILE="$runtime_audit_file"
  PY_DEPLOY_TEMP_SHA="$ARTIFACT_TOOL_SHA"
  runtime_audit_command="test ! -L '$runtime_audit_file' && test \"\$(sha256sum '$runtime_audit_file' | cut -d ' ' -f1)\" = '$ARTIFACT_TOOL_SHA' && $PY_CRYPTO_ENV LD_LIBRARY_PATH='$PY_PREFIX/lib' LD_PRELOAD='$PY_PREFIX/lib/libpython3.12.so.1.0' '$PY_BIN' -I -B '$runtime_audit_file' tree --runtime-usr '$PY_PREFIX' --expect-sha256 '$runtime_tree_sha' --expect-entries '${RUNTIME_COUNT_FIELDS[0]}' --expect-bytes '${RUNTIME_COUNT_FIELDS[1]}'"
  run_shell_marker "$board" "$runtime_audit_command && echo PYTHON_OVERLAY_RUNTIME_TREE_INITIAL_OK" PYTHON_OVERLAY_RUNTIME_TREE_INITIAL_OK || return 1
  if ! run_shell_marker "$board" \
    "test -d '$PY_REMOTE_PREFIX' && test ! -L '$PY_REMOTE_PREFIX' && test -d '$PY_PREFIX' && test ! -L '$PY_PREFIX' || exit 1; ( $PY_SOURCE_CHECK ) || exit 1; set -- \$(sha256sum '$PY_PREFIX/lib/libpython3.12.so.1.0'); test \"\$1\" = '$RUNTIME_SHA' || exit 1; ( $existing_overlay_check ) || exit 1; test ! -e '$remote_stage' && test ! -e '$remote_backup' && mkdir '$remote_stage' && echo PYTHON_OVERLAY_STAGE_READY" \
    PYTHON_OVERLAY_STAGE_READY >/dev/null; then
    # Preflight did not establish ownership. A conflicting stage/backup may
    # belong to someone else and must not be removed or restored by this run.
    return 1
  fi

  for name in "${ENTRIES[@]}"; do
    if ! send_path "$board" "$SITE_PKGS/$name" "$remote_stage/$name"; then
      echo "failed to send staged Python entry to $board: $name" >&2
      rollback_overlay "$board" "$remote_stage" "$remote_backup"
      return 1
    fi
  done
  if ! send_path "$board" "$LOCAL_STAGE_MARKER" "$remote_stage/$STAGE_MARKER" || \
     ! send_path "$board" "$file_manifest_local" "$remote_stage/$FILE_MANIFEST" || \
     ! send_path "$board" "$path_inventory_local" "$remote_stage/$PATH_INVENTORY" || \
     ! send_path "$board" "$deployment_marker_local" "$remote_stage/$DEPLOYMENT_MARKER"; then
    echo "failed to send Python provenance markers to $board" >&2
    rollback_overlay "$board" "$remote_stage" "$remote_backup"
    return 1
  fi

  if ! run_shell_marker "$board" \
    "set -- \$(sha256sum '$remote_stage/$STAGE_MARKER'); test \"\$1\" = '$STAGE_MARKER_SHA' && set -- \$(sha256sum '$remote_stage/$FILE_MANIFEST'); test \"\$1\" = '$file_manifest_sha' && set -- \$(sha256sum '$remote_stage/$PATH_INVENTORY'); test \"\$1\" = '$path_inventory_sha' && set -- \$(sha256sum '$remote_stage/$DEPLOYMENT_MARKER'); test \"\$1\" = '$deployment_marker_sha' && echo PYTHON_OVERLAY_HASHES_OK" \
    PYTHON_OVERLAY_HASHES_OK >/dev/null; then
    rollback_overlay "$board" "$remote_stage" "$remote_backup"
    return 1
  fi

  # HDC does not preserve empty directories. Recreate only the directories in
  # the already hash-verified expected-path inventory before comparing trees.
  if ! run_shell_marker "$board" \
    "cd '$remote_stage' && while IFS=' ' read -r kind path; do if [ \"\$kind\" = d ]; then mkdir -p \"./\$path\"; fi; done < '$remote_stage/$PATH_INVENTORY' && echo PYTHON_OVERLAY_DIRS_READY" \
    PYTHON_OVERLAY_DIRS_READY >/dev/null; then
    rollback_overlay "$board" "$remote_stage" "$remote_backup"
    return 1
  fi

  remote_actual_inventory="/data/local/tmp/ros2-python-paths-$run_id.txt"
  tree_verify_command="set -e; cd '$remote_stage'; if find . ! -type f ! -type d -print | grep -q .; then echo PYTHON_OVERLAY_SPECIAL_ENTRY >&2; exit 1; fi; { find . -type d -print | sed -e '/^\.$/d' -e 's|^\./|d |'; find . -type f -print | sed -e 's|^\./|f |'; } | LC_ALL=C sort > '$remote_actual_inventory'; if ! cmp '$remote_stage/$PATH_INVENTORY' '$remote_actual_inventory'; then echo PYTHON_OVERLAY_PATH_MISMATCH >&2; wc -l '$remote_stage/$PATH_INVENTORY' '$remote_actual_inventory' >&2; sha256sum '$remote_stage/$PATH_INVENTORY' '$remote_actual_inventory' >&2; exit 1; fi; rm -f '$remote_actual_inventory'; if ! sha256sum -c '$remote_stage/$FILE_MANIFEST' >/dev/null; then echo PYTHON_OVERLAY_FILE_HASH_MISMATCH >&2; exit 1; fi"
  if ! run_shell_marker "$board" \
    "$tree_verify_command; echo PYTHON_OVERLAY_TREE_OK" PYTHON_OVERLAY_TREE_OK; then
    MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$board" shell "rm -f '$remote_actual_inventory'" >/dev/null 2>&1 || true
    rollback_overlay "$board" "$remote_stage" "$remote_backup"
    return 1
  fi

  verify_command="$PY_CRYPTO_ENV LD_LIBRARY_PATH='$PY_PREFIX/lib' LD_PRELOAD='$PY_PREFIX/lib/libpython3.12.so.1.0' '$PY_BIN' -I -B -c 'import os,platform,sys,sysconfig; sys.path.insert(0,\"$remote_stage\"); import numpy,yaml,psutil,lark,catkin_pkg,argcomplete,packaging,setuptools,pip,em,lxml.etree,cryptography.fernet,cffi,pycparser,pytest,pytest_timeout,pytest_repeat,pytest_rerunfailures,pytest_mock,colcon_core,colcon_cmake,colcon_ros,colcon_test_result,colcon_python_setup_py; overlay=os.path.realpath(\"$remote_stage\")+\"/\"; assert all(os.path.realpath(m.__file__).startswith(overlay) for m in (numpy,yaml,psutil,lark,catkin_pkg)); assert sys.version_info[:3] == (3,12,7); assert sysconfig.get_config_var(\"SOABI\") == \"cpython-312-aarch64-linux-ohos\"; assert platform.machine() in (\"aarch64\",\"arm64\"); print(\"PYTHON_DEPS_OK version=3.12.7 soabi=cpython-312-aarch64-linux-ohos arch=\" + platform.machine() + \" numpy=\" + numpy.__version__)'"
  if ! verify_output="$(run_shell_marker "$board" "$verify_command" PYTHON_DEPS_OK)"; then
    rollback_overlay "$board" "$remote_stage" "$remote_backup"
    return 1
  fi

  commit_command="( $runtime_audit_command ) || exit 1; ( $tree_verify_command ) || exit 1; if [ -e '$REMOTE_OVERLAY' ]; then mv '$REMOTE_OVERLAY' '$remote_backup'; fi; if mv '$remote_stage' '$REMOTE_OVERLAY'; then set -- \$(sha256sum '$REMOTE_OVERLAY/$DEPLOYMENT_MARKER'); fi; if [ \"\${1:-}\" = '$deployment_marker_sha' ]; then echo PYTHON_OVERLAY_COMMIT_OK; else echo \"PYTHON_OVERLAY_COMMIT_HASH=\${1:-missing}\" >&2; rm -rf '$REMOTE_OVERLAY'; if [ -e '$remote_backup' ]; then mv '$remote_backup' '$REMOTE_OVERLAY'; fi; echo PYTHON_OVERLAY_COMMIT_FAILED >&2; exit 1; fi"
  if ! run_shell_marker "$board" "$commit_command" PYTHON_OVERLAY_COMMIT_OK; then
    rollback_overlay "$board" "$remote_stage" "$remote_backup"
    return 1
  fi

  final_actual_inventory="/data/local/tmp/ros2-python-final-paths-$run_id.txt"
  final_tree_verify_command="set -e; cd '$REMOTE_OVERLAY'; if find . ! -type f ! -type d -print | grep -q .; then echo PYTHON_OVERLAY_SPECIAL_ENTRY >&2; exit 1; fi; { find . -type d -print | sed -e '/^\.$/d' -e 's|^\./|d |'; find . -type f -print | sed -e 's|^\./|f |'; } | LC_ALL=C sort > '$final_actual_inventory'; if ! cmp '$REMOTE_OVERLAY/$PATH_INVENTORY' '$final_actual_inventory'; then echo PYTHON_OVERLAY_PATH_MISMATCH >&2; exit 1; fi; rm -f '$final_actual_inventory'; if ! sha256sum -c '$REMOTE_OVERLAY/$FILE_MANIFEST' >/dev/null; then echo PYTHON_OVERLAY_FILE_HASH_MISMATCH >&2; exit 1; fi"
  verify_command="( $PY_SOURCE_CHECK ) || exit 1; $PY_CRYPTO_ENV LD_LIBRARY_PATH='$PY_PREFIX/lib' LD_PRELOAD='$PY_PREFIX/lib/libpython3.12.so.1.0' '$PY_BIN' -I -B -c 'import sys; sys.path.insert(0,\"$REMOTE_OVERLAY\"); import os,numpy,yaml,psutil; overlay=os.path.realpath(\"$REMOTE_OVERLAY\")+\"/\"; assert all(os.path.realpath(m.__file__).startswith(overlay) for m in (numpy,yaml,psutil)); print(\"PYTHON_OVERLAY_FINAL_OK numpy=\"+numpy.__version__)'"
  if ! final_output="$(run_shell_marker "$board" \
      "( $runtime_audit_command ) || exit 1; $final_tree_verify_command; $verify_command" PYTHON_OVERLAY_FINAL_OK)"; then
    MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$board" shell \
      "rm -f '$final_actual_inventory'; rm -rf '$REMOTE_OVERLAY'; if [ -e '$remote_backup' ]; then mv '$remote_backup' '$REMOTE_OVERLAY'; fi" \
      >/dev/null 2>&1 || true
    return 1
  fi
  verify_output="$verify_output
$final_output"
  if ! run_shell_marker "$board" \
    "if [ -e '$remote_backup' ]; then rm -rf '$remote_backup'; fi; echo PYTHON_OVERLAY_BACKUP_RELEASED" \
    PYTHON_OVERLAY_BACKUP_RELEASED >/dev/null; then
    return 1
  fi
  printf '%s\n' "$verify_output"

  evidence="$evidence_dir/install_$run_id.record"
  evidence_payload="$(
    echo "RESULT=PASS"
    echo "BOARD_SERIAL=$board"
    echo "PYTHON_VERSION=3.12.7"
    echo "PYTHON_SOABI=cpython-312-aarch64-linux-ohos"
    echo "PYTHON_REMOTE_PREFIX=$PY_REMOTE_PREFIX"
    echo "PYTHON_REMOTE_OVERLAY=$REMOTE_OVERLAY"
    echo "PYTHON_RUNTIME_LIB_SHA256=$RUNTIME_SHA"
    echo "PYTHON_RUNTIME_ARCHIVE_SHA256=$archive_sha"
    echo "PYTHON_RUNTIME_TREE_SHA256=$runtime_tree_sha"
    echo "PYTHON_STAGE_MARKER_SHA256=$STAGE_MARKER_SHA"
    echo "PYTHON_STAGE_TREE_SHA256=$stage_tree_sha"
    echo "PYTHON_DEPLOYMENT_MARKER_SHA256=$deployment_marker_sha"
    echo "PYTHON_OVERLAY_FILE_MANIFEST_SHA256=$file_manifest_sha"
    echo "PYTHON_OVERLAY_PATH_INVENTORY_SHA256=$path_inventory_sha"
    echo "PYTHON_LOCK_SHA256=$LOCK_SHA"
    echo "PYTHON_PROVENANCE_MODE=$SOURCE_MODE"
    echo "PYTHON_SOURCE_BUILD_RECEIPT_SHA256=$SOURCE_SHA"
    echo "PYTHON_SOURCE_LOCK_SHA256=$SOURCE_LOCK_SHA"
    echo "PYTHON_SOURCE_BUILD_RECIPE_SHA256=$SOURCE_RECIPE_SHA"
    echo "PYTHON_RUNTIME_FULL_TREE_AUDIT=PASS"
    echo "PYTHON_RUNTIME_AUDIT_TOOL_SHA256=$ARTIFACT_TOOL_SHA"
    echo "PYTHON_DEPLOYMENT_LEASE_CLEANUP=PASS"
    echo "MANAGED_ENTRY_COUNT=${#ENTRIES[@]}"
    printf '%s\n' "$verify_output"
  )" || return 1
  printf '%s\n' "$evidence_payload" > "$evidence.tmp" || return 1
  python_deploy_release_lease || return 1
  mv "$evidence.tmp" "$evidence" || return 1
  echo "python dependency evidence: $evidence"
}

for board in "${BOARDS[@]}"; do
  install_one_board "$board" || exit 1
done
