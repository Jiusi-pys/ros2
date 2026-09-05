#!/usr/bin/env bash
# Rebuild the complete KaihongOS CPython runtime from fixed public inputs.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workspace_root="$(cd "$script_dir/../.." && pwd)"
source_lock="$script_dir/source_build.lock.json"
output=
cache=

usage() {
  cat <<'EOF'
Usage: rebuild_source_release.sh --output EMPTY_PATH [--cache VERIFIED_ARCHIVE_DIR]

OUTPUT is create-only and is the sole owned build workspace.  CACHE may contain
already downloaded files, but every byte is copied and checked against the
repository source lock before use.  Missing public inputs are downloaded.
EOF
}

while (($#)); do
  case "$1" in
    --output) output="${2:?missing --output value}"; shift 2 ;;
    --cache) cache="${2:?missing --cache value}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done
test -n "$output" || { usage >&2; exit 2; }
output="$(realpath -m "$output")"
if [ -n "$cache" ]; then
  cache="$(realpath "$cache")"
  test -d "$cache"
fi
test ! -e "$output" || { echo "source build output already exists: $output" >&2; exit 1; }

unset CC CXX AR RANLIB STRIP CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LIBS \
  CONFIG_SITE PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR \
  PYTHONHOME PYTHONPATH _PYTHON_SYSCONFIGDATA_NAME
export LC_ALL=C
export LANG=C
export TZ=UTC
export SOURCE_DATE_EPOCH=1727740800
export PYTHONHASHSEED=0
export PYTHONDONTWRITEBYTECODE=1
export GIT_CONFIG_GLOBAL=/dev/null
export GIT_CONFIG_SYSTEM=/dev/null

mkdir -p "$output/inputs/downloads" "$output/release"
trace="$output/source-build.trace"
exec > >(tee "$trace") 2>&1

sha256() { sha256sum "$1" | cut -d' ' -f1; }
verify() {
  local path="$1" expected="$2" actual
  actual="$(sha256 "$path")"
  test "$actual" = "$expected" || {
    echo "SHA-256 mismatch for $path: expected $expected, got $actual" >&2
    exit 1
  }
}
fetch() {
  local filename="$1" url="$2" expected="$3" destination part
  destination="$output/inputs/downloads/$filename"
  if [ -n "$cache" ] && [ -f "$cache/$filename" ]; then
    verify "$cache/$filename" "$expected"
    cp --reflink=auto "$cache/$filename" "$destination"
  else
    part="$destination.part"
    curl --fail --location --proto '=https' --tlsv1.2 --retry 3 \
      --output "$part" "$url"
    mv "$part" "$destination"
  fi
  verify "$destination" "$expected"
}
locked() {
  python3 - "$source_lock" "$@" <<'PY'
import json, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
for component in sys.argv[2:]:
    value = value[int(component)] if component.isdigit() else value[component]
print(value)
PY
}
tree_hash() {
  local root="$1"
  (
    cd "$root"
    (
      find . -type f -print0 | sort -z | xargs -0 sha256sum
      find . -type l -printf 'L %p -> %l\n' | sort
    ) | sha256sum | cut -d' ' -f1
  )
}
assert_pristine_source() {
  local phase="$1" actual bytecode cache_dir
  bytecode="$(find "$output/cpython-source" -type f -name '*.pyc' -print -quit)"
  if [ -n "$bytecode" ]; then
    echo "prepared CPython source gained bytecode during $phase: $bytecode" >&2
    exit 1
  fi
  cache_dir="$(find "$output/cpython-source" -type d -name __pycache__ -print -quit)"
  if [ -n "$cache_dir" ]; then
    echo "prepared CPython source gained __pycache__ during $phase: $cache_dir" >&2
    exit 1
  fi
  actual="$(tree_hash "$output/cpython-source")"
  test "$actual" = "$(locked inputs prepared_source tree_sha256)" || {
    echo "prepared CPython source changed during $phase: $actual" >&2
    exit 1
  }
}
recipe_hash() {
  python3 -B - "$workspace_root" "$source_lock" <<'PY'
import importlib.util
from pathlib import Path
import sys

workspace = Path(sys.argv[1])
lock_path = Path(sys.argv[2])
spec = importlib.util.spec_from_file_location(
    "python_runtime_artifact", workspace / "scripts/python_runtime_artifact.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
lock = module.load_json_object(lock_path, "Python source input lock")
print(module.build_recipe_record(workspace, lock)["sha256"])
PY
}

source_lock_sha_before="$(sha256 "$source_lock")"
recipe_sha_before="$(recipe_hash)"
printf 'python_source_lock_sha256=%s\npython_source_build_recipe_sha256=%s\n' \
  "$source_lock_sha_before" "$recipe_sha_before" \
  > "$output/source-input-freeze.txt"

fetch "$(locked inputs cpython filename)" "$(locked inputs cpython url)" \
  "$(locked inputs cpython sha256)"
fetch "$(locked inputs ohos_clang filename)" "$(locked inputs ohos_clang url)" \
  "$(locked inputs ohos_clang sha256)"
dependency_count="$(python3 - "$source_lock" <<'PY'
import json, sys
print(len(json.load(open(sys.argv[1], encoding="utf-8"))["inputs"]["dependencies"]))
PY
)"
for ((index=0; index<dependency_count; ++index)); do
  fetch "$(locked inputs dependencies "$index" filename)" \
    "$(locked inputs dependencies "$index" url)" \
    "$(locked inputs dependencies "$index" sha256)"
done

sdk_filename="$(locked inputs ohos_native_sdk filename)"
sdk_sha="$(locked inputs ohos_native_sdk lfs_oid_sha256)"
if [ -n "$cache" ] && [ -f "$cache/$sdk_filename" ]; then
  verify "$cache/$sdk_filename" "$sdk_sha"
  cp --reflink=auto "$cache/$sdk_filename" "$output/inputs/downloads/$sdk_filename"
else
  sdk_git="$output/inputs/sdk-git"
  git init -q "$sdk_git"
  git -C "$sdk_git" remote add origin "$(locked inputs ohos_native_sdk repository)"
  git -C "$sdk_git" lfs install --local --skip-smudge
  git -C "$sdk_git" fetch --depth=1 origin "$(locked inputs ohos_native_sdk commit)"
  git -C "$sdk_git" checkout -q --detach FETCH_HEAD
  GIT_LFS_SKIP_SMUDGE=0 git -C "$sdk_git" lfs pull \
    --include="$(locked inputs ohos_native_sdk path)" --exclude=''
  cp --reflink=auto "$sdk_git/$(locked inputs ohos_native_sdk path)" \
    "$output/inputs/downloads/$sdk_filename"
fi
verify "$output/inputs/downloads/$sdk_filename" "$sdk_sha"
test "$(stat -c %s "$output/inputs/downloads/$sdk_filename")" = \
  "$(locked inputs ohos_native_sdk bytes)"

jiusi="$output/inputs/jiusi-python"
git init -q "$jiusi"
git -C "$jiusi" remote add origin "$(locked inputs jiusi_python url)"
git -C "$jiusi" fetch --depth=1 origin "$(locked inputs jiusi_python commit)"
git -C "$jiusi" checkout -q --detach FETCH_HEAD
test "$(git -C "$jiusi" rev-parse HEAD)" = "$(locked inputs jiusi_python commit)"
verify "$jiusi/$(locked inputs jiusi_python config_sub path)" \
  "$(locked inputs jiusi_python config_sub sha256)"
verify "$jiusi/$(locked inputs jiusi_python config_site path)" \
  "$(locked inputs jiusi_python config_site sha256)"

mkdir "$output/toolchain"
tar -xzf "$output/inputs/downloads/$(locked inputs ohos_clang filename)" \
  --strip-components=1 -C "$output/toolchain"
verify "$output/toolchain/bin/clang" "$(locked inputs ohos_clang clang_sha256)"
verify "$output/toolchain/bin/llvm-readelf" "$(locked inputs ohos_clang llvm_readelf_sha256)"

mkdir "$output/sdk"
unzip -q "$output/inputs/downloads/$sdk_filename" -d "$output/sdk"
native="$output/sdk/native"
verify "$native/oh-uni-package.json" "$(locked inputs ohos_native_sdk metadata_sha256)"
sysroot_hash="$(tree_hash "$native/sysroot")"
test "$sysroot_hash" = "$(locked inputs ohos_native_sdk sysroot_tree_sha256)" || {
  echo "OHOS SDK sysroot tree mismatch: $sysroot_hash" >&2
  exit 1
}

bash "$script_dir/prepare_cpython_source.sh" \
  "$output/inputs/downloads/$(locked inputs cpython filename)" \
  "$jiusi" "$script_dir/ohos-platform-triplet.patch" "$output/cpython-source"
prepared_hash="$(tree_hash "$output/cpython-source")"
test "$prepared_hash" = "$(locked inputs prepared_source tree_sha256)" || {
  echo "prepared CPython source tree mismatch: $prepared_hash" >&2
  exit 1
}
bash "$script_dir/rebuild_host_python.sh" "$output/cpython-source" "$output/host-build"
assert_pristine_source host-build

mkdir -p "$output/deps/downloads"
for index in $(seq 0 $((dependency_count - 1))); do
  filename="$(locked inputs dependencies "$index" filename)"
  cp --reflink=auto "$output/inputs/downloads/$filename" "$output/deps/downloads/$filename"
done
bash "$script_dir/rebuild_python_dependencies.sh" "$output/deps" \
  "$output/toolchain" "$native/sysroot" \
  "$jiusi/$(locked inputs jiusi_python config_sub path)"

bash "$script_dir/rebuild_cpython_ohos.sh" \
  --source "$output/cpython-source" \
  --host-python "$output/host-build/python" \
  --toolchain "$output/toolchain" \
  --sysroot "$native/sysroot" \
  --deps-prefix "$output/deps/prefix" \
  --output "$output/target-build"
bash "$script_dir/verify_cpython_runtime.sh" \
  "$output/target-build/runtime/usr" "$output/target-build/build" \
  "$output/toolchain/bin/llvm-readelf"
assert_pristine_source target-build

source_lock_sha_after="$(sha256 "$source_lock")"
recipe_sha_after="$(recipe_hash)"
test "$source_lock_sha_after" = "$source_lock_sha_before" || {
  echo "Python source input lock changed during the build" >&2
  exit 1
}
test "$recipe_sha_after" = "$recipe_sha_before" || {
  echo "Python source build recipe changed during the build" >&2
  exit 1
}

archive="$output/release/cpython-3.12.7-ohos-aarch64-source.tar.gz"
python3 "$workspace_root/scripts/python_runtime_artifact.py" --lock "$source_lock" pack \
  --runtime-usr "$output/target-build/runtime/usr" --output "$archive"
python3 "$workspace_root/scripts/python_runtime_artifact.py" \
  create-source-receipt \
  --source-lock "$source_lock" \
  --runtime-usr "$output/target-build/runtime/usr" \
  --archive "$archive" \
  --build-dir "$output/target-build/build" \
  --prepared-source "$output/cpython-source" \
  --host-python "$output/host-build/python" \
  --toolchain "$output/toolchain" \
  --sysroot "$native/sysroot" \
  --deps-prefix "$output/deps/prefix" \
  --output "$output/release/PYTHON_SOURCE_BUILD_RECEIPT.json"
python3 -B - "$output/release/PYTHON_SOURCE_BUILD_RECEIPT.json" \
  "$source_lock_sha_before" "$recipe_sha_before" <<'PY'
import json
import sys

receipt = json.load(open(sys.argv[1], encoding="utf-8"))
if receipt.get("python_source_lock_sha256") != sys.argv[2]:
    raise SystemExit("source receipt did not capture the pre-build source lock")
if receipt.get("build_recipe_sha256") != sys.argv[3]:
    raise SystemExit("source receipt did not capture the pre-build recipe")
PY

echo "PYTHON_SOURCE_RELEASE_BUILD_OK"
sha256sum "$archive" "$output/release/PYTHON_SOURCE_BUILD_RECEIPT.json"
