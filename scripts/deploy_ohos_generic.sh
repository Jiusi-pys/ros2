#!/usr/bin/env bash
# Build a provenance-bound generic ROS 2 archive and atomically deploy it to
# KaihongOS/RK3588A boards without requiring any MDDS launcher or artifact.
set -euo pipefail
DEPLOY_CONTROLLER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
DEPLOY_CONTROLLER_FILE="$DEPLOY_CONTROLLER_DIR/$(basename "${BASH_SOURCE[0]}")"
# A corrected deployment controller may deploy an immutable build workspace
# without changing the source snapshot recorded by that build.
cd "${ROS2_DEPLOY_WORKSPACE:-$(dirname "$0")/..}"

usage() {
  cat <<'EOF'
Usage: OHOS_BUILD_RECEIPT=/path/to/ohos_build_receipt.json [RMW=rmw_fastrtps_cpp] \
       ./scripts/deploy_ohos_generic.sh [--prepare-only] [board ...]

The default RMW is rmw_fastrtps_cpp.  The release gate must prove that exact
implementation before the resulting deployment can be called accepted.
EOF
}

PREPARE_ONLY=0
if [[ "${1:-}" == "--prepare-only" ]]; then
  PREPARE_ONLY=1
  shift
elif [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

RMW="${RMW:-rmw_fastrtps_cpp}"
device_dir="${ROS2_DEVICE_DIR:-/data/local/tmp/ros2-generic}"
[[ "$device_dir" =~ ^/data/local/tmp/ros2-[A-Za-z0-9][A-Za-z0-9_.-]*$ ]] || {
  echo "ERROR: ROS2_DEVICE_DIR must name one isolated /data/local/tmp/ros2-* directory" >&2
  exit 2
}
case "$RMW" in
  rmw_fastrtps_cpp|rmw_cyclonedds_cpp) ;;
  *) echo "ERROR: RMW must be rmw_fastrtps_cpp or rmw_cyclonedds_cpp" >&2; exit 2 ;;
esac

BUILD_RECEIPT="${OHOS_BUILD_RECEIPT:-}"
if [[ -z "$BUILD_RECEIPT" || ! -f "$BUILD_RECEIPT" || -L "$BUILD_RECEIPT" ]]; then
  echo "ERROR: OHOS_BUILD_RECEIPT must name this candidate's completed clean-build receipt" >&2
  exit 2
fi

if [[ -z "${HDC:-}" ]]; then
  if [[ -n "${LOCALAPPDATA:-}" ]]; then
    local_appdata_posix="$(cygpath -u "$LOCALAPPDATA")"
    HDC="$local_appdata_posix/OpenHarmony/Sdk/23/toolchains/hdc.exe"
  else
    HDC=/c/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe
  fi
fi
if [[ -z "${OHOS_NATIVE_SDK:-}" ]]; then
  if [[ -n "${LOCALAPPDATA:-}" ]]; then
    OHOS_NATIVE_SDK="$(cygpath -u "$LOCALAPPDATA")/OpenHarmony/Sdk/23/native"
  else
    OHOS_NATIVE_SDK=/c/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native
  fi
fi

for tool in "$HDC" tar sha256sum find sort xargs cygpath cp mv sed wc grep seq sleep mktemp pixi; do
  if [[ "$tool" == */* ]]; then
    [[ -x "$tool" ]] || { echo "ERROR: required executable not found: $tool" >&2; exit 2; }
  else
    command -v "$tool" >/dev/null 2>&1 || { echo "ERROR: required tool not found: $tool" >&2; exit 2; }
  fi
done
[[ -d install_ohos && ! -L install_ohos ]] || {
  echo "ERROR: install_ohos must be a non-symlink directory" >&2
  exit 2
}
runtime_bootstrap="$DEPLOY_CONTROLLER_DIR/runtime_config/sitecustomize.py"
[[ -f "$runtime_bootstrap" && ! -L "$runtime_bootstrap" ]] || {
  echo 'ERROR: ROS Python runtime bootstrap is missing' >&2; exit 2;
}
bootstrap_sha="$(sha256sum "$runtime_bootstrap" | cut -d ' ' -f1)"
controller_sha="$(sha256sum "$DEPLOY_CONTROLLER_FILE" | cut -d ' ' -f1)"
bootstrap_dir="/data/local/tmp/ros2-core-config/python-$bootstrap_sha"
bootstrap_remote="$bootstrap_dir/sitecustomize.py"
[[ -f scripts/env_ohos_generic.template.sh && ! -L scripts/env_ohos_generic.template.sh ]] || {
  echo "ERROR: generic environment template is missing" >&2
  exit 2
}

required_paths=(
  Lib/demo_nodes_cpp/talker
  Lib/demo_nodes_cpp/listener
  Lib/demo_nodes_py/talker-script.py
  Lib/librclcpp.so
  Lib/librmw_implementation.so
  Lib/librmw_fastrtps_cpp.so
  Lib/librmw_cyclonedds_cpp.so
  Lib/site-packages/rclpy/__init__.py
  Lib/site-packages/ros2cli/__init__.py
  bin/lttng
  Lib/lttng/libexec/lttng-consumerd
)
for relative in "${required_paths[@]}"; do
  [[ -f "install_ohos/$relative" && ! -L "install_ohos/$relative" ]] || {
    echo "ERROR: required generic ROS 2 artifact is missing: install_ohos/$relative" >&2
    exit 2
  }
done
for reserved in deploy_manifest.sha256 env.sh release_provenance.json build_receipt.json .ros2_deploy_complete .ros2-activity-lock; do
  if [[ -e "install_ohos/$reserved" || -L "install_ohos/$reserved" ]]; then
    echo "ERROR: install_ohos contains reserved generic deployment path: $reserved" >&2
    exit 2
  fi
done

# Deployment must never repair or normalize the build output.  The completed
# build receipt binds the exact tree that is archived below.
if find install_ohos ! -type f ! -type d -print -quit | grep -q .; then
  echo "ERROR: install_ohos contains a symlink or special entry" >&2
  exit 2
fi

tmp_base="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
host_tmp="$(mktemp -d "$tmp_base/ros2-ohos-generic-deploy.XXXXXX")"
case "$host_tmp" in "$tmp_base"/ros2-ohos-generic-deploy.*) ;; *) exit 70 ;; esac
cleanup_host() {
  case "$host_tmp" in "$tmp_base"/ros2-ohos-generic-deploy.*) rm -rf -- "$host_tmp" ;; esac
}
trap cleanup_host EXIT

manifest_raw="$host_tmp/install.raw.sha256"
manifest="$host_tmp/deploy_manifest.sha256"
archive_tmp="$host_tmp/ros2_ohos_generic_install.tar.gz"
provenance="$host_tmp/release_provenance.json"
env_file="$host_tmp/env.sh"
archive="$PWD/ros2_ohos_generic_install.tar.gz"
provenance_out="$PWD/ros2_ohos_generic_release_provenance.json"

(cd install_ohos && find . -type f -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) > "$manifest_raw"
pixi run python scripts/normalize_ohos_manifest.py "$manifest_raw" "$manifest"
(cd install_ohos && sha256sum -c "$manifest" >/dev/null)
manifest_sha="$(sha256sum "$manifest" | cut -d ' ' -f1)"
tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner --format=gnu \
  -C install_ohos -czf "$archive_tmp" .
archive_sha="$(sha256sum "$archive_tmp" | cut -d ' ' -f1)"
archive_bytes="$(wc -c < "$archive_tmp" | tr -d '[:space:]')"

collector=(
  pixi run python scripts/collect_ohos_release_provenance.py
  --workspace "$PWD"
  --lock ros2.ohos.lock.repos
  --archive "$archive_tmp"
  --install-manifest "$manifest"
  --install-root "$PWD/install_ohos"
  --sdk-root "$OHOS_NATIVE_SDK"
  --rmw "$RMW"
  --build-receipt "$BUILD_RECEIPT"
  --output "$provenance"
)
"${collector[@]}"
pixi run python "$DEPLOY_CONTROLLER_DIR/runtime_config_binding.py" "$provenance" "$bootstrap_sha" "$controller_sha"
provenance_sha="$(sha256sum "$provenance" | cut -d ' ' -f1)"
readarray -t provenance_fields < <(pixi run python - "$provenance" <<'PY' | tr -d '\r'
import json
import sys
p = json.load(open(sys.argv[1], encoding="utf-8"))
py = p["inputs"]["python"]
for value in (
    p["source_snapshot_sha256"],
    p["sdk"]["fingerprint_sha256"],
    p["inputs"]["clean_build_receipt"]["sha256"],
    py["lock"]["sha256"],
    py["runtime_archive_sha256"],
    py["runtime_tree_sha256"],
    py["runtime_entry_count"],
    py["runtime_payload_bytes"],
    py["runtime_interface_sha256"],
    py["runtime_library_sha256"],
    py["stage_marker"]["sha256"],
    py["stage_tree_sha256"],
    py["provenance_mode"],
    (py.get("source_build_receipt") or {}).get("sha256", "NOT_APPLICABLE"),
    py.get("source_lock_sha256") or "NOT_APPLICABLE",
    py.get("source_build_recipe_sha256") or "NOT_APPLICABLE",
):
    print(value)
PY
)
[[ "${#provenance_fields[@]}" -eq 16 ]] || {
  echo "ERROR: provenance collector omitted release/Python binding fields" >&2
  exit 2
}
source_sha="${provenance_fields[0]}"
sdk_sha="${provenance_fields[1]}"
build_receipt_sha="${provenance_fields[2]}"
python_lock_sha="${provenance_fields[3]}"
python_archive_sha="${provenance_fields[4]}"
python_runtime_tree_sha="${provenance_fields[5]}"
python_runtime_entries="${provenance_fields[6]}"
python_runtime_bytes="${provenance_fields[7]}"
python_runtime_interface_sha="${provenance_fields[8]}"
python_runtime_library_sha="${provenance_fields[9]}"
python_stage_marker_sha="${provenance_fields[10]}"
python_stage_tree_sha="${provenance_fields[11]}"
python_provenance_mode="${provenance_fields[12]}"
python_source_receipt_sha="${provenance_fields[13]}"
python_source_lock_sha="${provenance_fields[14]}"
python_source_recipe_sha="${provenance_fields[15]}"
for digest in "$source_sha" "$sdk_sha" "$build_receipt_sha" "$python_lock_sha" \
  "$python_archive_sha" "$python_runtime_tree_sha" "$python_runtime_interface_sha" \
  "$python_runtime_library_sha" "$python_stage_marker_sha" "$python_stage_tree_sha"; do
  [[ "$digest" =~ ^[0-9a-f]{64}$ ]] || {
    echo "ERROR: provenance collector returned malformed release/Python digest: $digest" >&2
    exit 2
  }
done
[[ "$python_runtime_entries" =~ ^[1-9][0-9]*$ && "$python_runtime_bytes" =~ ^[1-9][0-9]*$ ]] || {
  echo "ERROR: provenance collector returned malformed Python runtime cardinality" >&2
  exit 2
}
case "$python_provenance_mode" in
  source-reproducible)
    for digest in "$python_source_receipt_sha" "$python_source_lock_sha" "$python_source_recipe_sha"; do
      [[ "$digest" =~ ^[0-9a-f]{64}$ ]] || { echo "ERROR: incomplete Python source-build binding" >&2; exit 2; }
    done
    ;;
  artifact-reproducible-not-source-reproducible)
    [[ "$python_source_receipt_sha:$python_source_lock_sha:$python_source_recipe_sha" == NOT_APPLICABLE:NOT_APPLICABLE:NOT_APPLICABLE ]] || {
      echo "ERROR: artifact-only Python has contradictory source-build bindings" >&2; exit 2;
    }
    ;;
  *) echo "ERROR: unexpected Python runtime provenance mode: $python_provenance_mode" >&2; exit 2 ;;
esac
python_remote_prefix="${PYTHON_REMOTE_PREFIX:-/data/python312-rk3588a-verify-${python_archive_sha:0:12}}"
[[ "$python_remote_prefix" =~ ^/data/[A-Za-z0-9._+-]+$ ]] || {
  echo "ERROR: unsafe PYTHON_REMOTE_PREFIX: $python_remote_prefix" >&2
  exit 2
}
python_remote_overlay="$python_remote_prefix/ros2-site-packages"
[[ "$(sha256sum "$BUILD_RECEIPT" | cut -d ' ' -f1)" == "$build_receipt_sha" ]] || {
  echo "ERROR: clean-build receipt changed during deployment preparation" >&2
  exit 2
}

run_id="${ROS2_DEPLOY_RUN_ID:-ros2_deploy_$(date -u +%Y%m%dT%H%M%SZ)_${RANDOM}_${RANDOM}_$$}"
[[ "$run_id" =~ ^[A-Za-z0-9_.-]+$ ]] || {
  echo "ERROR: ROS2_DEPLOY_RUN_ID contains unsafe characters" >&2
  exit 2
}
marker="ROS2_DEPLOY_COMPLETE V=1 RUN_ID=$run_id RMW=$RMW ARCHIVE_SHA256=$archive_sha MANIFEST_SHA256=$manifest_sha PROVENANCE_SHA256=$provenance_sha BUILD_RECEIPT_SHA256=$build_receipt_sha SOURCE_SHA256=$source_sha SDK_SHA256=$sdk_sha PYTHON_LOCK_SHA256=$python_lock_sha PYTHON_RUNTIME_ARCHIVE_SHA256=$python_archive_sha PYTHON_RUNTIME_TREE_SHA256=$python_runtime_tree_sha PYTHON_STAGE_TREE_SHA256=$python_stage_tree_sha"
{
  printf 'ROS2_DEPLOY_EXPECTED_MARKER=%q\n' "$marker"
  printf 'export ROS2_HOME=%q\n' "$device_dir"
  printf 'export RMW_IMPLEMENTATION=%q\n' "$RMW"
  printf 'export ROS2_ARCHIVE_SHA256=%q\n' "$archive_sha"
  printf 'export ROS2_RELEASE_PROVENANCE_SHA256=%q\n' "$provenance_sha"
  printf 'export ROS2_SOURCE_SNAPSHOT_SHA256=%q\n' "$source_sha"
  printf 'export ROS2_SDK_FINGERPRINT_SHA256=%q\n' "$sdk_sha"
  printf 'export ROS2_BUILD_RECEIPT_SHA256=%q\n' "$build_receipt_sha"
  printf 'export ROS2_PYTHON_REMOTE_PREFIX=%q\n' "$python_remote_prefix"
  printf 'export ROS2_PYTHON_REMOTE_OVERLAY=%q\n' "$python_remote_overlay"
  printf 'export ROS2_PYTHON_LOCK_SHA256=%q\n' "$python_lock_sha"
  printf 'export ROS2_PYTHON_RUNTIME_ARCHIVE_SHA256=%q\n' "$python_archive_sha"
  printf 'export ROS2_PYTHON_RUNTIME_TREE_SHA256=%q\n' "$python_runtime_tree_sha"
  printf 'export ROS2_PYTHON_RUNTIME_ENTRY_COUNT=%q\n' "$python_runtime_entries"
  printf 'export ROS2_PYTHON_RUNTIME_PAYLOAD_BYTES=%q\n' "$python_runtime_bytes"
  printf 'export ROS2_PYTHON_RUNTIME_INTERFACE_SHA256=%q\n' "$python_runtime_interface_sha"
  printf 'export ROS2_PYTHON_RUNTIME_LIBRARY_SHA256=%q\n' "$python_runtime_library_sha"
  printf 'export ROS2_PYTHON_STAGE_MARKER_SHA256=%q\n' "$python_stage_marker_sha"
  printf 'export ROS2_PYTHON_STAGE_TREE_SHA256=%q\n' "$python_stage_tree_sha"
  printf 'export ROS2_PYTHON_PROVENANCE_MODE=%q\n' "$python_provenance_mode"
  printf 'export ROS2_PYTHON_SOURCE_BUILD_RECEIPT_SHA256=%q\n' "$python_source_receipt_sha"
  printf 'export ROS2_PYTHON_SOURCE_LOCK_SHA256=%q\n' "$python_source_lock_sha"
  printf 'export ROS2_PYTHON_SOURCE_BUILD_RECIPE_SHA256=%q\n' "$python_source_recipe_sha"
  cat scripts/env_ohos_generic.template.sh
  printf 'export ROS2_PYTHON_BOOTSTRAP_DIR=%q\n' "$bootstrap_dir"
  printf 'export ROS2_PYTHON_BOOTSTRAP_SHA256=%q\n' "$bootstrap_sha"
  cat <<'BOOTSTRAP_ENV'
if [ ! -f "$ROS2_PYTHON_BOOTSTRAP_DIR/sitecustomize.py" ] || \
   [ -L "$ROS2_PYTHON_BOOTSTRAP_DIR/sitecustomize.py" ] || \
   [ "$(sha256sum "$ROS2_PYTHON_BOOTSTRAP_DIR/sitecustomize.py" | cut -d ' ' -f1)" != "$ROS2_PYTHON_BOOTSTRAP_SHA256" ]; then
  echo 'ERROR: ROS Python bootstrap differs from deployment provenance' >&2
  return 70 2>/dev/null || exit 70
fi
export PYTHONPATH="$ROS2_PYTHON_BOOTSTRAP_DIR:$PYTHONPATH"
export ROS_DISTRO=jazzy ROS_VERSION=2 ROS_PYTHON_VERSION=3
BOOTSTRAP_ENV
} > "$env_file"
env_sha="$(sha256sum "$env_file" | cut -d ' ' -f1)"

mv -f "$archive_tmp" "$archive"
cp -f "$provenance" "$provenance_out"
printf 'ROS2_DEPLOY_LOCAL run_id=%s rmw=%s archive_sha256=%s archive_bytes=%s manifest_sha256=%s provenance_sha256=%s build_receipt_sha256=%s source_sha256=%s sdk_sha256=%s python_runtime_archive_sha256=%s python_runtime_tree_sha256=%s python_stage_tree_sha256=%s\n' \
  "$run_id" "$RMW" "$archive_sha" "$archive_bytes" "$manifest_sha" "$provenance_sha" \
  "$build_receipt_sha" "$source_sha" "$sdk_sha" "$python_archive_sha" \
  "$python_runtime_tree_sha" "$python_stage_tree_sha"

if [[ "$PREPARE_ONLY" -eq 1 ]]; then
  printf 'ROS2_DEPLOY_PREPARE_ONLY result=PASS archive=%s provenance=%s\n' "$archive" "$provenance_out"
  exit 0
fi

boards=("$@")
if [[ ${#boards[@]} -eq 0 ]]; then
  boards=(3e01ff55454d202020104033bf453b00 3e01ff55454d202020104433991c3b00)
fi
declare -A seen=()
for board in "${boards[@]}"; do
  [[ "$board" =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "ERROR: unsafe board identifier: $board" >&2; exit 2; }
  [[ -z "${seen[$board]+present}" ]] || { echo "ERROR: duplicate board: $board" >&2; exit 2; }
  seen["$board"]=1
done

export MSYS2_ARG_CONV_EXCL='*'
device_parent=/data/local/tmp
global_lock="$device_parent/.ros2-generic-deploy.lock"

remote() { "$HDC" -t "$1" shell "$2" </dev/null; }
remote_line() {
  local value="$1"
  value="${value%$'\r'}"
  [[ "$value" != *$'\r'* && "$value" != *$'\n'* ]] || return 1
  printf '%s' "$value"
}
remote_sha() {
  remote "$1" "if test -f '$2' && test ! -L '$2'; then sha256sum '$2' 2>/dev/null | cut -d ' ' -f1; fi" | tr -d '\r\n '
}
send_verified() {
  local board="$1" local_path="$2" remote_path="$3" expected="$4" attempt got
  "$HDC" -t "$board" file send "$(cygpath -aw "$local_path")" "$remote_path" </dev/null >/dev/null
  for attempt in $(seq 1 10); do
    got="$(remote_sha "$board" "$remote_path" || true)"
    [[ "$got" == "$expected" ]] && return 0
    sleep 1
  done
  echo "ERROR: transfer digest mismatch board=$board path=$remote_path expected=$expected got=${got:-MISSING}" >&2
  return 1
}

deploy_one() {
  local board="$1" nonce owner stage backup archive_remote manifest_remote provenance_remote receipt_remote env_remote out commit_state
  local bootstrap_setup bootstrap_stage
  nonce="${run_id}_${board}"
  owner="ROS2_GENERIC_DEPLOY V=1 RUN_ID=$run_id BOARD=$board"
  stage="$device_parent/.ros2-generic-stage-$nonce"
  backup="$device_parent/.ros2-generic-backup-$nonce"
  archive_remote="$device_parent/.ros2-generic-$nonce.tar.gz"
  manifest_remote="$device_parent/.ros2-generic-$nonce.sha256"
  provenance_remote="$device_parent/.ros2-generic-$nonce.provenance.json"
  receipt_remote="$device_parent/.ros2-generic-$nonce.build-receipt.json"
  env_remote="$device_parent/.ros2-generic-$nonce.env.sh"
  bootstrap_stage="$bootstrap_dir/.sitecustomize-$nonce.py"
  bootstrap_setup="$(remote "$board" "if test -L /data/local/tmp/ros2-core-config || test -L '$bootstrap_dir'; then printf BOOTSTRAP_UNSAFE; elif mkdir -p '$bootstrap_dir'; then printf BOOTSTRAP_READY; else printf BOOTSTRAP_FAILED; fi" | tr -d '\r\n')"
  [[ "$bootstrap_setup" == BOOTSTRAP_READY ]] || { echo 'ERROR: cannot prepare ROS bootstrap directory' >&2; return 1; }
  if [[ "$(remote_sha "$board" "$bootstrap_remote")" != "$bootstrap_sha" ]]; then
    send_verified "$board" "$runtime_bootstrap" "$bootstrap_stage" "$bootstrap_sha" || return 1
    bootstrap_setup="$(remote "$board" "if test -e '$bootstrap_remote' || test -L '$bootstrap_remote'; then printf BOOTSTRAP_CONFLICT; elif chmod 644 '$bootstrap_stage' && mv '$bootstrap_stage' '$bootstrap_remote'; then printf BOOTSTRAP_INSTALLED; else printf BOOTSTRAP_FAILED; fi" | tr -d '\r\n')"
    [[ "$bootstrap_setup" == BOOTSTRAP_INSTALLED ]] || { echo 'ERROR: ROS bootstrap activation failed' >&2; return 1; }
  fi

  out="$(remote "$board" "if test -e '$global_lock' || test -L '$global_lock'; then printf ROS2_DEPLOY_LOCK_BUSY; elif (umask 077; mkdir '$global_lock') && (umask 077; set -C; printf '%s\\n' '$owner' > '$global_lock/owner') 2>/dev/null && test \"\$(cat '$global_lock/owner' 2>/dev/null)\" = '$owner'; then printf ROS2_DEPLOY_LOCK_ACQUIRED; else printf ROS2_DEPLOY_LOCK_FAILED; fi" || true)"
  out="$(remote_line "$out" || true)"
  [[ "$out" == ROS2_DEPLOY_LOCK_ACQUIRED ]] || {
    echo "ERROR: generic deploy lock unavailable on $board: ${out:-NO_MARKER}" >&2
    return 1
  }

  release_lock() {
    local release
    release="$(remote "$board" "if test -d '$global_lock' && test ! -L '$global_lock' && test -f '$global_lock/owner' && test ! -L '$global_lock/owner' && test \"\$(cat '$global_lock/owner' 2>/dev/null)\" = '$owner'; then rm -f '$global_lock/owner' && rmdir '$global_lock' && printf ROS2_DEPLOY_LOCK_RELEASED; else printf ROS2_DEPLOY_LOCK_NOT_OWNED; fi" || true)"
    release="$(remote_line "$release" || true)"
    [[ "$release" == ROS2_DEPLOY_LOCK_RELEASED ]]
  }

  out="$(remote "$board" "if test -e '$stage' || test -L '$stage' || test -e '$backup' || test -L '$backup'; then printf ROS2_DEPLOY_PATH_CONFLICT; elif (umask 077; mkdir '$stage'); then printf ROS2_DEPLOY_STAGE_READY; else printf ROS2_DEPLOY_STAGE_FAILED; fi" || true)"
  out="$(remote_line "$out" || true)"
  if [[ "$out" != ROS2_DEPLOY_STAGE_READY ]]; then
    release_lock || true
    echo "ERROR: cannot create isolated stage on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi

  if ! send_verified "$board" "$archive" "$archive_remote" "$archive_sha" || \
     ! send_verified "$board" "$manifest" "$manifest_remote" "$manifest_sha" || \
     ! send_verified "$board" "$provenance" "$provenance_remote" "$provenance_sha" || \
     ! send_verified "$board" "$BUILD_RECEIPT" "$receipt_remote" "$build_receipt_sha" || \
     ! send_verified "$board" "$env_file" "$env_remote" "$env_sha"; then
    remote "$board" "rm -rf '$stage'; rm -f '$archive_remote' '$manifest_remote' '$provenance_remote' '$receipt_remote' '$env_remote'" >/dev/null 2>&1 || true
    release_lock || true
    return 1
  fi

  out="$(remote "$board" "set -- \$(wc -c < '$archive_remote' 2>/dev/null); if test \"\$1\" != '$archive_bytes'; then printf ROS2_DEPLOY_ARCHIVE_SIZE_BAD; elif ! tar -xzf '$archive_remote' -C '$stage'; then printf ROS2_DEPLOY_EXTRACT_FAILED; elif ! cp '$manifest_remote' '$stage/deploy_manifest.sha256' || ! cp '$provenance_remote' '$stage/release_provenance.json' || ! cp '$receipt_remote' '$stage/build_receipt.json' || ! cp '$env_remote' '$stage/env.sh'; then printf ROS2_DEPLOY_CONTROL_COPY_FAILED; elif test \"\$(sha256sum '$stage/release_provenance.json' | cut -d ' ' -f1)\" != '$provenance_sha' || test \"\$(sha256sum '$stage/build_receipt.json' | cut -d ' ' -f1)\" != '$build_receipt_sha' || test \"\$(sha256sum '$stage/env.sh' | cut -d ' ' -f1)\" != '$env_sha'; then printf ROS2_DEPLOY_CONTROL_HASH_BAD; elif ! (cd '$stage' && sha256sum -c deploy_manifest.sha256 >/dev/null 2>&1); then printf ROS2_DEPLOY_TREE_HASH_BAD; elif ! test -f '$stage/Lib/demo_nodes_cpp/talker' || ! test -f '$stage/Lib/demo_nodes_cpp/listener' || ! test -f '$stage/Lib/librmw_fastrtps_cpp.so' || ! test -f '$stage/Lib/librmw_cyclonedds_cpp.so' || ! test -f '$stage/Lib/site-packages/rclpy/__init__.py' || ! test -f '$stage/Lib/site-packages/ros2cli/__init__.py'; then printf ROS2_DEPLOY_REQUIRED_MISSING; elif ! find '$stage/Lib' -type f -exec chmod +x {} + || ! find '$stage/bin' -type f -exec chmod +x {} + || ! chmod +x '$stage/env.sh'; then printf ROS2_DEPLOY_CHMOD_FAILED; elif { test -e '$stage/lib' || test -L '$stage/lib'; } && ! test -L '$stage/lib'; then printf ROS2_DEPLOY_LIB_CONFLICT; elif ! test -e '$stage/lib' && ! test -L '$stage/lib' && ! ln -s Lib '$stage/lib'; then printf ROS2_DEPLOY_LIB_LINK_FAILED; elif ! (umask 077; set -C; printf '%s\\n' '$marker' > '$stage/.ros2_deploy_complete') 2>/dev/null; then printf ROS2_DEPLOY_MARKER_FAILED; else printf ROS2_DEPLOY_STAGE_VERIFIED; fi" || true)"
  out="$(remote_line "$out" || true)"
  if [[ "$out" != ROS2_DEPLOY_STAGE_VERIFIED ]]; then
    remote "$board" "rm -rf '$stage'; rm -f '$archive_remote' '$manifest_remote' '$provenance_remote' '$receipt_remote' '$env_remote'" >/dev/null 2>&1 || true
    release_lock || true
    echo "ERROR: staged generic ROS 2 tree failed verification on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi

  out="$(remote "$board" "if test -e '$device_dir' || test -L '$device_dir'; then if mv '$device_dir' '$backup' && mv '$stage' '$device_dir'; then printf ROS2_DEPLOY_COMMITTED_WITH_BACKUP; else test -e '$device_dir' || mv '$backup' '$device_dir' >/dev/null 2>&1 || true; printf ROS2_DEPLOY_COMMIT_FAILED; fi; elif mv '$stage' '$device_dir'; then printf ROS2_DEPLOY_COMMITTED_NEW; else printf ROS2_DEPLOY_COMMIT_FAILED; fi" || true)"
  out="$(remote_line "$out" || true)"
  if [[ "$out" != ROS2_DEPLOY_COMMITTED_WITH_BACKUP && "$out" != ROS2_DEPLOY_COMMITTED_NEW ]]; then
    release_lock || true
    echo "ERROR: atomic generic deployment failed on $board: ${out:-NO_MARKER}" >&2
    return 1
  fi
  commit_state="$out"

  post="$(remote "$board" "if test \"\$(cat '$device_dir/.ros2_deploy_complete' 2>/dev/null)\" = '$marker' && test \"\$(sha256sum '$device_dir/release_provenance.json' | cut -d ' ' -f1)\" = '$provenance_sha' && (cd '$device_dir' && sha256sum -c deploy_manifest.sha256 >/dev/null 2>&1) && . '$device_dir/env.sh' >/dev/null 2>&1 && test \"\$RMW_IMPLEMENTATION\" = '$RMW' && python3.12 -c 'import rclpy, ros2cli' >/dev/null 2>&1; then printf ROS2_DEPLOY_POSTCHECK_OK; else printf ROS2_DEPLOY_POSTCHECK_FAILED; fi" || true)"
  post="$(remote_line "$post" || true)"
  if [[ "$post" != ROS2_DEPLOY_POSTCHECK_OK ]]; then
    # Retain the actual environment/import error before rollback removes a new
    # failed prefix. A failed boolean alone is insufficient porting evidence.
    remote "$board" ". '$device_dir/env.sh'; env_rc=\$?; printf 'ROS2_POSTCHECK_ENV_RC=%s\\n' \"\$env_rc\"; if test \"\$env_rc\" = 0; then python3.12 -B -c 'import rclpy, ros2cli'; printf 'ROS2_POSTCHECK_IMPORT_RC=%s\\n' \"\$?\"; fi" >&2 || true
    if [[ "$commit_state" == ROS2_DEPLOY_COMMITTED_WITH_BACKUP ]]; then
      rollback="$(remote "$board" "failed='$device_parent/.ros2-generic-failed-$nonce'; if test -d '$backup' && test ! -L '$backup' && ! test -e \"\$failed\" && ! test -L \"\$failed\" && mv '$device_dir' \"\$failed\" && mv '$backup' '$device_dir'; then rm -rf \"\$failed\"; printf ROS2_DEPLOY_ROLLED_BACK; else printf ROS2_DEPLOY_ROLLBACK_FAILED; fi" || true)"
    else
      rollback="$(remote "$board" "if test -d '$device_dir' && test ! -L '$device_dir' && test \"\$(cat '$device_dir/.ros2_deploy_complete' 2>/dev/null)\" = '$marker' && rm -rf '$device_dir'; then printf ROS2_DEPLOY_REMOVED_FAILED_NEW; else printf ROS2_DEPLOY_ROLLBACK_FAILED; fi" || true)"
    fi
    rollback="$(remote_line "$rollback" || true)"
    release_lock || true
    echo "ERROR: generic deployment postcheck failed on $board; rollback=$rollback" >&2
    return 1
  fi

  remote "$board" "if test -d '$backup' && test ! -L '$backup'; then rm -rf '$backup'; fi; rm -f '$archive_remote' '$manifest_remote' '$provenance_remote' '$receipt_remote' '$env_remote'" >/dev/null 2>&1 || true
  release_lock || { echo "ERROR: deployed but could not release owned lock on $board" >&2; return 1; }
  printf 'ROS2_DEPLOY_BOARD board=%s os=KaihongOS rmw=%s result=PASS archive_sha256=%s provenance_sha256=%s source_sha256=%s sdk_sha256=%s\n' \
    "$board" "$RMW" "$archive_sha" "$provenance_sha" "$source_sha" "$sdk_sha"
}

for board in "${boards[@]}"; do
  deploy_one "$board"
done
printf 'ROS2_DEPLOY_ALL result=PASS boards=%s rmw=%s provenance_sha256=%s\n' "${#boards[@]}" "$RMW" "$provenance_sha"
