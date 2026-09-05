#!/usr/bin/env bash
# Deterministic regression for locked downloads and clean patch replay.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$ROOT"
SRC_DIR="$ROOT/target_deps_src"
# shellcheck source=../target_deps_src/lib/locked_sources.sh
source "$SRC_DIR/lib/locked_sources.sh"

[ "$(lock_field eigen-3.4.0.tar.gz 3)" = \
  8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72 ] || {
  echo "ERROR: Eigen is not bound to the reviewed source archive" >&2
  exit 1
}

TMP_BASE="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
TEST_ROOT="$(mktemp -d "$TMP_BASE/ohos-target-deps-test.XXXXXX")"
case "$TEST_ROOT" in "$TMP_BASE"/ohos-target-deps-test.*) ;; *) exit 70 ;; esac
cleanup() {
  case "$TEST_ROOT" in "$TMP_BASE"/ohos-target-deps-test.*) rm -rf -- "$TEST_ROOT" ;; esac
}
trap cleanup EXIT

# A corrupt pre-existing cache must fail closed without reaching the network.
printf 'not-the-archive\n' > "$TEST_ROOT/tinyxml2-10.0.0.tar.gz"
if fetch_locked tinyxml2-10.0.0.tar.gz "$TEST_ROOT/tinyxml2-10.0.0.tar.gz" \
    >"$TEST_ROOT/corrupt.stdout" 2>"$TEST_ROOT/corrupt.stderr"; then
  echo "ERROR: corrupt cache was accepted" >&2
  exit 1
fi
grep -q 'checksum mismatch for cached' "$TEST_ROOT/corrupt.stderr" || {
  echo "ERROR: corrupt-cache failure was not the checksum gate" >&2
  exit 1
}

if [ "${1:-}" = --offline ]; then
  for cached in lttng-ust-2.13.8.tar.bz2 qtbase-5.15.8.tar.xz; do
    [ -f "$SRC_DIR/$cached" ] || {
      echo "ERROR: --offline requires cached $SRC_DIR/$cached" >&2
      exit 2
    }
  done
else
  fetch_locked lttng-ust-2.13.8.tar.bz2 "$SRC_DIR/lttng-ust-2.13.8.tar.bz2"
  fetch_locked qtbase-5.15.8.tar.xz "$SRC_DIR/qtbase-5.15.8.tar.xz"
fi

fetch_locked lttng-ust-2.13.8.tar.bz2 "$SRC_DIR/lttng-ust-2.13.8.tar.bz2"
fetch_locked qtbase-5.15.8.tar.xz "$SRC_DIR/qtbase-5.15.8.tar.xz"
tar -xf "$SRC_DIR/lttng-ust-2.13.8.tar.bz2" -C "$TEST_ROOT"
tar -xf "$SRC_DIR/qtbase-5.15.8.tar.xz" -C "$TEST_ROOT"

apply_patch_locked "$TEST_ROOT/lttng-ust-2.13.8" \
  "$SRC_DIR/lttng-ust-2.13.8-ohos.patch"
apply_patch_locked "$TEST_ROOT/qtbase-everywhere-src-5.15.8" \
  "$SRC_DIR/qtbase-5.15.8-ohos.patch"

# Clean mode starts from pristine extraction. Accepting an already-applied
# patch would mean a cached or modified source tree escaped the BEGIN gate.
if OHOS_TARGET_DEPS_CLEAN=1 apply_patch_locked \
    "$TEST_ROOT/qtbase-everywhere-src-5.15.8" \
    "$SRC_DIR/qtbase-5.15.8-ohos.patch" \
    >"$TEST_ROOT/preapplied.stdout" 2>"$TEST_ROOT/preapplied.stderr"; then
  echo "ERROR: clean mode accepted a pre-applied patch" >&2
  exit 1
fi
grep -q 'unexpectedly had a patch pre-applied' "$TEST_ROOT/preapplied.stderr" || {
  echo "ERROR: clean pre-applied rejection did not reach the intended gate" >&2
  exit 1
}

# A checkout at the locked commit is insufficient for a clean build when its
# worktree has local changes. Exercise this without contacting a remote.
GIT_SOURCE="$TEST_ROOT/git-source"
git init -q "$GIT_SOURCE"
git -C "$GIT_SOURCE" config user.name target-deps-test
git -C "$GIT_SOURCE" config user.email target-deps-test.invalid
git -C "$GIT_SOURCE" config commit.gpgsign false
printf 'locked\n' > "$GIT_SOURCE/input.txt"
git -C "$GIT_SOURCE" add input.txt
git -C "$GIT_SOURCE" commit -q -m locked
GIT_REVISION="$(git -C "$GIT_SOURCE" rev-parse HEAD)"
printf 'dirty\n' >> "$GIT_SOURCE/input.txt"
ORIGINAL_SOURCE_LOCK="$SOURCE_LOCK"
SOURCE_LOCK="$TEST_ROOT/dirty-git.lock"
printf 'git dirty-test %s https://example.invalid/dirty-test.git\n' \
  "$GIT_REVISION" > "$SOURCE_LOCK"
if OHOS_TARGET_DEPS_CLEAN=1 ensure_locked_git_checkout dirty-test "$GIT_SOURCE" \
    >"$TEST_ROOT/dirty.stdout" 2>"$TEST_ROOT/dirty.stderr"; then
  echo "ERROR: clean mode accepted a dirty locked git checkout" >&2
  exit 1
fi
SOURCE_LOCK="$ORIGINAL_SOURCE_LOCK"
grep -q 'locked git source is dirty in clean mode' "$TEST_ROOT/dirty.stderr" || {
  echo "ERROR: dirty git rejection did not reach the intended gate" >&2
  exit 1
}

grep -q '__MUSL__' \
  "$TEST_ROOT/lttng-ust-2.13.8/src/lib/lttng-ust-common/ust-cancelstate.c"
grep -q 'Q_OS_OPENHARMONY' \
  "$TEST_ROOT/qtbase-everywhere-src-5.15.8/src/corelib/global/qsystemdetection.h"
grep -q 'isEmpty(OHOS_ARCH): OHOS_ARCH = arm64-v8a' \
  "$TEST_ROOT/qtbase-everywhere-src-5.15.8/mkspecs/common/oh-base-head.conf"

echo "TARGET_DEPS_LOCK_TEST=PASS"
