#!/usr/bin/env bash
# Execute the real runner's terminal sentinel and EXIT cleanup in isolation.
set -euo pipefail
cd "$(dirname "$0")/.."
RUNNER=target_deps_src/build_all_clean_ohos.sh
fixture="$(mktemp -d)"
trap 'rm -rf -- "$fixture"' EXIT
PREFIX="$fixture/prefix"
mkdir "$PREFIX"
sentinels="$(sed -n '/^REQUIRED_OUTPUTS=(/,/^MANIFEST=/p' "$RUNNER" | sed '$d')"
eval "$(printf '%s\n' "$sentinels" | sed '/^for relative/,$d')"
for relative in "${REQUIRED_OUTPUTS[@]}"; do
  mkdir -p "$(dirname "$PREFIX/$relative")"
  : > "$PREFIX/$relative"
done
# Qt5Gui_QSvgPlugin.cmake installed by oh-clang names this exact file.
[ -f "$PREFIX/plugins/imageformats/libplugins_imageformats_qsvg.so" ]
(set -e; eval "$sentinels")
mv "$PREFIX/plugins/imageformats/libplugins_imageformats_qsvg.so" \
   "$PREFIX/plugins/imageformats/libqsvg.so"
set +e
(set -e; eval "$sentinels") > "$fixture/missing.log" 2>&1
missing_status=$?
set -e
[ "$missing_status" = 1 ]
grep -Fq 'prefix is incomplete: plugins/imageformats/libplugins_imageformats_qsvg.so' "$fixture/missing.log"

cleanup_function="$(sed -n '/^cleanup() {/,/^}/p' "$RUNNER")"
for expected_status in 0 17; do
  RUN_DIR="$fixture/evidence-$expected_status"
  LOCK_DIR="$fixture/lock-$expected_status"
  BEGIN_RECORD="$RUN_DIR/begin.json"
  JOURNAL="$RUN_DIR/sources.jsonl"
  LOCK_OWNED=1
  mkdir "$RUN_DIR" "$LOCK_DIR"
  printf 'begin evidence\n' > "$BEGIN_RECORD"
  printf 'source evidence\n' > "$JOURNAL"
  set +e
  (eval "$cleanup_function"; trap cleanup EXIT; exit "$expected_status")
  actual_status=$?
  set -e
  [ "$actual_status" = "$expected_status" ]
  [ -f "$BEGIN_RECORD" ] && [ -f "$JOURNAL" ]
  [ ! -e "$LOCK_DIR" ]
  [ ! -e "$PREFIX/.ohos-target-deps.clean-receipt.json" ]
done
echo 'CLEAN_DEPS_RUNNER_CONTRACT result=PASS cases=sentinel-positive,sentinel-negative,success-evidence,failure-evidence'
