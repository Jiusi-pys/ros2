#!/usr/bin/env bash
# Bootstrap a verified CPython runtime artifact into an isolated board prefix.
set -euo pipefail
cd "$(dirname "$0")/.."

usage() {
  cat >&2 <<'EOF'
usage: ./scripts/deploy_python_runtime_artifact.sh ARCHIVE.tar.gz [board_serial ...]

The destination is always an isolated /data/*-verify-<archive-hash> prefix.
The existing /data/python312-rk3588a runtime is never replaced.
EOF
}

[ "$#" -ge 1 ] || { usage; exit 2; }
ARCHIVE="$1"
shift
case "$ARCHIVE" in
  [A-Za-z]:*) ARCHIVE="$(cygpath -u "$ARCHIVE")" ;;
esac
[ -f "$ARCHIVE" ] || { echo "runtime archive is missing: $ARCHIVE" >&2; exit 1; }
MANIFEST="$ARCHIVE.manifest.json"
[ -f "$MANIFEST" ] || { echo "runtime artifact manifest is missing: $MANIFEST" >&2; exit 1; }

PYTHON="${HOST_PYTHON:-$(pwd)/.pixi/envs/default/python.exe}"
if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3 || true)"
fi
[ -n "$PYTHON" ] && [ -x "$PYTHON" ] || { echo "host Python 3 is required" >&2; exit 1; }
LOCK="${PYTHON_TARGET_LOCK:-$(pwd)/scripts/python/ohos_python.lock.json}"
TOOL="$(pwd)/scripts/python_runtime_artifact.py"
TOOL_SHA="$(sha256sum "$TOOL" | cut -d ' ' -f1)"
HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
[ -x "$HDC" ] || { echo "HDC is not executable: $HDC" >&2; exit 1; }

"$PYTHON" "$TOOL" --lock "$LOCK" verify --archive "$ARCHIVE" --manifest "$MANIFEST"
SOURCE_OUTPUT="$("$PYTHON" scripts/print_python_source_binding.py --manifest "$MANIFEST" --lock "$LOCK" | tr -d '\r')" || exit 1
mapfile -t SOURCE_FIELDS <<<"$SOURCE_OUTPUT"
[ "${#SOURCE_FIELDS[@]}" -eq 5 ] || exit 1
SOURCE_MODE="${SOURCE_FIELDS[0]}"
SOURCE_FILE="$(dirname "$MANIFEST")/${SOURCE_FIELDS[1]}"
SOURCE_SHA="${SOURCE_FIELDS[2]}"
SOURCE_LOCK_SHA="${SOURCE_FIELDS[3]}"
SOURCE_RECIPE_SHA="${SOURCE_FIELDS[4]}"
ARTIFACT_OUTPUT="$("$PYTHON" - "$MANIFEST" <<'PY' | tr -d '\r'
import json
import sys
p = json.load(open(sys.argv[1], encoding="utf-8"))
print(p["archive"]["sha256"])
print(p["runtime_tree_sha256"])
print(p["runtime_entry_count"])
print(p["runtime_payload_bytes"])
print(p["python_lock_sha256"])
PY
)" || exit 1
mapfile -t ARTIFACT_FIELDS <<<"$ARTIFACT_OUTPUT"
[ "${#ARTIFACT_FIELDS[@]}" -eq 5 ] || exit 1
ARCHIVE_SHA="${ARTIFACT_FIELDS[0]}"
TREE_SHA="${ARTIFACT_FIELDS[1]}"
ENTRY_COUNT="${ARTIFACT_FIELDS[2]}"
PAYLOAD_BYTES="${ARTIFACT_FIELDS[3]}"
LOCK_SHA="${ARTIFACT_FIELDS[4]}"

DEST_PREFIX="${PYTHON_RUNTIME_TEST_PREFIX:-/data/python312-rk3588a-verify-${ARCHIVE_SHA:0:12}}"
[[ "$DEST_PREFIX" =~ ^/data/[A-Za-z0-9._+-]+-verify-[0-9a-f]{12}$ ]] || {
  echo "destination must be an isolated hash-suffixed verification prefix: $DEST_PREFIX" >&2
  exit 2
}
[[ "$DEST_PREFIX" == *"-verify-${ARCHIVE_SHA:0:12}" ]] || {
  echo "runtime prefix suffix does not match the archive SHA-256" >&2; exit 2;
}
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

send_file() {
  local board="$1"
  local source="$2"
  local destination="$3"
  MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$board" file send \
    "$(cygpath -w "$source")" "$destination" >/dev/null
}
. scripts/lib/python_deploy_lease.sh

deploy_one_board() {
  local board="$1"
  local run_id
  local remote_archive
  local remote_tool
  local remote_stage
  local evidence_dir
  local deployment_marker
  local deployment_marker_sha
  local artifact_manifest_sha
  local tree_command
  local python_env
  local output
  local evidence
  local source_check_stage='true'
  local source_check_final='true'
  local stage_owned=0
  local evidence_payload

  run_id="$(date -u +%Y%m%dT%H%M%SZ)-$$-${RANDOM}${RANDOM}"
  remote_archive="/data/local/tmp/python-runtime-$run_id.tar.gz"
  remote_tool="/data/local/tmp/python-runtime-artifact-$run_id.py"
  remote_stage="$DEST_PREFIX.stage.$run_id"
  evidence_dir="ohos_test_logs/python_runtime/$board"
  mkdir -p "$evidence_dir"
  deployment_marker="$evidence_dir/deployment_$run_id.json"
  # Preserve the literal board path in the hash-bound marker when this script is
  # launched from Git Bash with a Windows host Python.
  MSYS2_ARG_CONV_EXCL='--remote-prefix=' \
    "$PYTHON" "$TOOL" --lock "$LOCK" create-deployment-marker \
    --artifact-manifest "$MANIFEST" --output "$deployment_marker" \
    --board "$board" "--remote-prefix=$DEST_PREFIX" >/dev/null || return 1
  "$PYTHON" scripts/print_python_source_binding.py --manifest "$MANIFEST" --lock "$LOCK" \
    --deployment-marker "$deployment_marker" >/dev/null || return 1
  deployment_marker_sha="$("$PYTHON" -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$deployment_marker" | tr -d '\r')"
  artifact_manifest_sha="$("$PYTHON" -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$MANIFEST" | tr -d '\r')"

  cleanup_remote() {
    MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$board" shell \
      "rm -f '$remote_archive' '$remote_tool'; if test '$stage_owned' = 1 && test ! -L '$remote_stage'; then rm -rf '$remote_stage'; fi" >/dev/null 2>&1 || true
  }

  echo "== $board -> $DEST_PREFIX"
  python_deploy_acquire_lease "$board" "$DEST_PREFIX" "runtime-$run_id" || return 1
  if ! run_shell_marker "$board" \
    "test ! -e '$DEST_PREFIX' && test ! -L '$DEST_PREFIX' && test ! -e '$remote_stage' && test ! -L '$remote_stage' && test ! -e '$remote_archive' && test ! -L '$remote_archive' && test ! -e '$remote_tool' && test ! -L '$remote_tool' && command -v gzip >/dev/null && command -v tar >/dev/null && command -v sha256sum >/dev/null && echo PYTHON_RUNTIME_STAGE_READY" \
    PYTHON_RUNTIME_STAGE_READY >/dev/null; then
    # Nothing is owned yet. Never remove a pre-existing conflicting path.
    return 1
  fi
  if ! send_file "$board" "$ARCHIVE" "$remote_archive" || \
     ! send_file "$board" "$TOOL" "$remote_tool"; then
    cleanup_remote
    return 1
  fi
  if ! run_shell_marker "$board" \
    "set -- \$(sha256sum '$remote_archive'); test \"\$1\" = '$ARCHIVE_SHA' && mkdir '$remote_stage' && echo PYTHON_RUNTIME_STAGE_CREATED" \
    PYTHON_RUNTIME_STAGE_CREATED >/dev/null; then
    cleanup_remote
    return 1
  fi
  stage_owned=1
  if ! run_shell_marker "$board" \
    "gzip -dc '$remote_archive' | tar -xf - -C '$remote_stage' && echo PYTHON_RUNTIME_EXTRACT_OK" \
    PYTHON_RUNTIME_EXTRACT_OK >/dev/null; then
    cleanup_remote
    return 1
  fi

  python_env="OPENSSL_CONF='$remote_stage/usr/etc/ssl/openssl.cnf' OPENSSL_MODULES='$remote_stage/usr/lib/ossl-modules' SSL_CERT_FILE='$remote_stage/usr/etc/ssl/cert.pem' SSL_CERT_DIR='$remote_stage/usr/etc/ssl/certs' LD_LIBRARY_PATH='$remote_stage/usr/lib' LD_PRELOAD='$remote_stage/usr/lib/libpython3.12.so.1.0'"
  tree_command="test ! -L '$remote_tool' && test \"\$(sha256sum '$remote_tool' | cut -d ' ' -f1)\" = '$TOOL_SHA' && $python_env '$remote_stage/usr/bin/python3.12' -I -B '$remote_tool' tree --runtime-usr '$remote_stage/usr' --expect-sha256 '$TREE_SHA' --expect-entries '$ENTRY_COUNT' --expect-bytes '$PAYLOAD_BYTES'"
  if ! run_shell_marker "$board" "$tree_command && echo PYTHON_RUNTIME_TREE_OK" \
    PYTHON_RUNTIME_TREE_OK >/dev/null; then
    cleanup_remote
    return 1
  fi

  if [ "$SOURCE_MODE" = source-reproducible ]; then
    if ! send_file "$board" "$SOURCE_FILE" "$remote_stage/PYTHON_SOURCE_BUILD_RECEIPT.json"; then
      cleanup_remote
      return 1
    fi
    source_check_stage="test -f '$remote_stage/PYTHON_SOURCE_BUILD_RECEIPT.json' && test ! -L '$remote_stage/PYTHON_SOURCE_BUILD_RECEIPT.json' && test \"\$(sha256sum '$remote_stage/PYTHON_SOURCE_BUILD_RECEIPT.json' | cut -d ' ' -f1)\" = '$SOURCE_SHA'"
    source_check_final="test -f '$DEST_PREFIX/PYTHON_SOURCE_BUILD_RECEIPT.json' && test ! -L '$DEST_PREFIX/PYTHON_SOURCE_BUILD_RECEIPT.json' && test \"\$(sha256sum '$DEST_PREFIX/PYTHON_SOURCE_BUILD_RECEIPT.json' | cut -d ' ' -f1)\" = '$SOURCE_SHA'"
  fi
  tree_command="$source_check_stage && $tree_command"

  if ! send_file "$board" "$MANIFEST" "$remote_stage/PYTHON_RUNTIME_ARTIFACT.manifest.json" || \
     ! send_file "$board" "$deployment_marker" "$remote_stage/PYTHON_RUNTIME_DEPLOYMENT.json"; then
    cleanup_remote
    return 1
  fi
  if ! run_shell_marker "$board" \
    "set -- \$(sha256sum '$remote_stage/PYTHON_RUNTIME_ARTIFACT.manifest.json'); test \"\$1\" = '$artifact_manifest_sha' && set -- \$(sha256sum '$remote_stage/PYTHON_RUNTIME_DEPLOYMENT.json'); test \"\$1\" = '$deployment_marker_sha' && $tree_command && mv '$remote_stage' '$DEST_PREFIX' && echo PYTHON_RUNTIME_COMMIT_OK" \
    PYTHON_RUNTIME_COMMIT_OK >/dev/null; then
    cleanup_remote
    return 1
  fi

  python_env="OPENSSL_CONF='$DEST_PREFIX/usr/etc/ssl/openssl.cnf' OPENSSL_MODULES='$DEST_PREFIX/usr/lib/ossl-modules' SSL_CERT_FILE='$DEST_PREFIX/usr/etc/ssl/cert.pem' SSL_CERT_DIR='$DEST_PREFIX/usr/etc/ssl/certs' LD_LIBRARY_PATH='$DEST_PREFIX/usr/lib' LD_PRELOAD='$DEST_PREFIX/usr/lib/libpython3.12.so.1.0'"
  tree_command="$source_check_final && test ! -L '$remote_tool' && test \"\$(sha256sum '$remote_tool' | cut -d ' ' -f1)\" = '$TOOL_SHA' && $python_env '$DEST_PREFIX/usr/bin/python3.12' -I -B '$remote_tool' tree --runtime-usr '$DEST_PREFIX/usr' --expect-sha256 '$TREE_SHA' --expect-entries '$ENTRY_COUNT' --expect-bytes '$PAYLOAD_BYTES'"
  if ! output="$(run_shell_marker "$board" \
    "set -- \$(sha256sum '$DEST_PREFIX/PYTHON_RUNTIME_ARTIFACT.manifest.json'); test \"\$1\" = '$artifact_manifest_sha' && set -- \$(sha256sum '$DEST_PREFIX/PYTHON_RUNTIME_DEPLOYMENT.json'); test \"\$1\" = '$deployment_marker_sha' && $tree_command && $python_env '$DEST_PREFIX/usr/bin/python3.12' -I -B -c 'import platform,sys,sysconfig; assert sys.version_info[:3] == (3,12,7); assert sysconfig.get_config_var(\"SOABI\") == \"cpython-312-aarch64-linux-ohos\"; assert platform.machine() in (\"aarch64\",\"arm64\"); print(\"PYTHON_RUNTIME_FINAL_OK version=3.12.7 soabi=cpython-312-aarch64-linux-ohos arch=\"+platform.machine())'" \
    PYTHON_RUNTIME_FINAL_OK)"; then
    # The isolated destination did not pass readback. It was created by this
    # invocation and is safe to roll back without touching the base runtime.
    MSYS2_ARG_CONV_EXCL='*' "$HDC" -t "$board" shell "if test ! -L '$DEST_PREFIX' && test -f '$DEST_PREFIX/PYTHON_RUNTIME_DEPLOYMENT.json' && test ! -L '$DEST_PREFIX/PYTHON_RUNTIME_DEPLOYMENT.json' && test \"\$(sha256sum '$DEST_PREFIX/PYTHON_RUNTIME_DEPLOYMENT.json' | cut -d ' ' -f1)\" = '$deployment_marker_sha'; then rm -rf '$DEST_PREFIX'; fi" >/dev/null 2>&1 || true
    cleanup_remote
    return 1
  fi
  printf '%s\n' "$output"
  cleanup_remote

  evidence="$evidence_dir/deploy_$run_id.record"
  evidence_payload="$(
    echo "RESULT=PASS"
    echo "BOARD_SERIAL=$board"
    echo "PYTHON_REMOTE_PREFIX=$DEST_PREFIX"
    echo "PYTHON_RUNTIME_MARKER=$DEST_PREFIX/PYTHON_RUNTIME_DEPLOYMENT.json"
    echo "PYTHON_RUNTIME_ARTIFACT_MARKER=$DEST_PREFIX/PYTHON_RUNTIME_ARTIFACT.manifest.json"
    echo "PYTHON_OVERLAY=$DEST_PREFIX/ros2-site-packages"
    echo "PYTHON_LOCK_SHA256=$LOCK_SHA"
    echo "PYTHON_RUNTIME_ARCHIVE_SHA256=$ARCHIVE_SHA"
    echo "PYTHON_RUNTIME_TREE_SHA256=$TREE_SHA"
    echo "PYTHON_RUNTIME_ENTRY_COUNT=$ENTRY_COUNT"
    echo "PYTHON_RUNTIME_PAYLOAD_BYTES=$PAYLOAD_BYTES"
    echo "PYTHON_RUNTIME_DEPLOYMENT_MARKER_SHA256=$deployment_marker_sha"
    echo "PYTHON_RUNTIME_ARTIFACT_MARKER_SHA256=$artifact_manifest_sha"
    echo "PYTHON_PROVENANCE_MODE=$SOURCE_MODE"
    echo "PYTHON_SOURCE_BUILD_RECEIPT_SHA256=$SOURCE_SHA"
    echo "PYTHON_SOURCE_LOCK_SHA256=$SOURCE_LOCK_SHA"
    echo "PYTHON_SOURCE_BUILD_RECIPE_SHA256=$SOURCE_RECIPE_SHA"
    echo "PYTHON_RUNTIME_AUDIT_TOOL_SHA256=$TOOL_SHA"
    echo "PYTHON_DEPLOYMENT_LEASE_CLEANUP=PASS"
    printf '%s\n' "$output"
  )" || return 1
  printf '%s\n' "$evidence_payload" > "$evidence.tmp" || return 1
  python_deploy_release_lease || return 1
  mv "$evidence.tmp" "$evidence" || return 1
  echo "python runtime evidence: $evidence"
}

for board in "${BOARDS[@]}"; do
  deploy_one_board "$board" || exit 1
done

echo "python_runtime_isolated_prefix=$DEST_PREFIX"
echo "python_runtime_overlay_path=$DEST_PREFIX/ros2-site-packages"
