#!/usr/bin/env bash
# Apply the exported OHOS port patch series (patches/) onto a fresh src/
# checkout populated by `vcs import --input ros2.ohos.lock.repos src/`.
#
# Idempotent: a series whose diff is already present in the working tree
# (tree/reverse-apply check) is skipped. `git am --3way` provides conflict
# context, but an applied series must still reproduce the exported exact tree;
# rebasing onto a newer upstream requires a deliberate re-export/re-freeze.
set -uo pipefail
cd "$(dirname "$0")/.."

SOURCE_MANIFEST="${OHOS_SOURCE_MANIFEST:-ros2.ohos.lock.repos}"
SELECTED_ROOT_TEXT="$(pixi run python scripts/manifest_source_roots.py --manifest "$SOURCE_MANIFEST" --source-root src)" || exit 2
SOURCE_BASE="$(pixi run python -c 'from pathlib import Path; print(Path("src").resolve().as_posix())' | tr -d '\r')"
declare -A SELECTED_REPOSITORIES=()
while IFS= read -r selected; do
  SELECTED_REPOSITORIES["${selected#"$SOURCE_BASE/"}"]=1
done < <(printf '%s\n' "$SELECTED_ROOT_TEXT" | tr -d '\r' | sed 's|\\|/|g')
fail=0
for p in patches/*.patch; do
  [ -f "$p" ] || continue
  [[ "$p" == *.snapshot.patch ]] && continue
  key="$(basename "$p" .patch)"
  relative="${key//__//}"
  [ -n "${SELECTED_REPOSITORIES[$relative]+present}" ] || continue
  repo="src/$relative"
  if [ ! -d "$repo/.git" ]; then
    echo "== $repo: not checked out (run vcs import with ros2.ohos.lock.repos first)"
    fail=1
    continue
  fi
  if [ -d "$repo/.git/rebase-apply" ]; then
    echo "== $repo: git am in progress, resolve it first (git am --continue/--abort)"
    fail=1
    continue
  fi
  expected_tree="$(cat "patches/$key.tree" 2>/dev/null || true)"
  if [ -n "$expected_tree" ] && [ "$(git -C "$repo" rev-parse 'HEAD^{tree}')" = "$expected_tree" ]; then
    echo "== $repo: commit series tree already present, skipping"
    continue
  fi
  if git -C "$repo" apply --reverse --check "$PWD/$p" >/dev/null 2>&1; then
    echo "== $repo: already applied, skipping"
    continue
  fi
  base="$(cat "patches/$key.base" 2>/dev/null || true)"
  head="$(git -C "$repo" rev-parse HEAD)"
  if [ -z "$base" ] || ! git -C "$repo" cat-file -e "$base^{commit}" 2>/dev/null; then
    echo "== $repo: missing/unreachable recorded patch base"
    fail=1
    continue
  fi
  echo "== $repo: applying $(grep -c '^From ' "$p") commit(s)"
  if [ -n "$base" ] && [ "$head" != "$base" ]; then
    echo "   note: upstream moved (${base:0:9} -> ${head:0:9}), using 3-way apply"
  fi
  if git -C "$repo" am --3way "$PWD/$p"; then
    if [ -n "$expected_tree" ] && [ "$(git -C "$repo" rev-parse 'HEAD^{tree}')" != "$expected_tree" ]; then
      echo "   FAILED: applied series does not reproduce the exported HEAD tree"
      fail=1
    fi
  else
    echo "   FAILED: resolve conflicts in $repo, then 'git -C $repo am --continue'"
    fail=1
  fi
done

# Apply dirty-worktree snapshots only after their commit series.  Validation
# uses another temporary index, so replay does not stage files or alter a
# caller's real index metadata.
TMP_BASE="$(cd "${TMPDIR:-/tmp}" && pwd -P)" || exit 1
SNAPSHOT_TMP="$(mktemp -d "$TMP_BASE/ros2-patch-apply.XXXXXX")" || exit 1
case "$SNAPSHOT_TMP" in "$TMP_BASE"/ros2-patch-apply.*) ;; *) exit 70 ;; esac
cleanup_snapshot_tmp() {
  case "$SNAPSHOT_TMP" in "$TMP_BASE"/ros2-patch-apply.*) rm -rf -- "$SNAPSHOT_TMP" ;; esac
}
trap cleanup_snapshot_tmp EXIT
worktree_tree() { # <repo> <private-index>
  local repo="$1" private_index="$2"
  rm -f "$private_index"
  GIT_INDEX_FILE="$private_index" git -C "$repo" read-tree HEAD || return 1
  GIT_INDEX_FILE="$private_index" git -C "$repo" add -A || return 1
  GIT_INDEX_FILE="$private_index" git -C "$repo" write-tree
}

for snapshot in patches/*.snapshot.patch; do
  [ -f "$snapshot" ] || continue
  key="$(basename "$snapshot" .snapshot.patch)"
  relative="${key//__//}"
  [ -n "${SELECTED_REPOSITORIES[$relative]+present}" ] || continue
  repo="src/$relative"
  if [ ! -d "$repo/.git" ]; then
    echo "== $repo: not checked out for worktree snapshot"
    fail=1
    continue
  fi
  base="$(cat "patches/$key.snapshot.base" 2>/dev/null || true)"
  expected_tree="$(cat "patches/$key.snapshot.tree" 2>/dev/null || true)"
  if ! [[ "$base" =~ ^[0-9a-f]{40}$ && "$expected_tree" =~ ^[0-9a-f]{40}$ ]]; then
    echo "== $repo: snapshot tree metadata is missing or malformed"
    fail=1
    continue
  fi
  actual_tree="$(worktree_tree "$repo" "$SNAPSHOT_TMP/$key.index")" || { fail=1; continue; }
  if [ "$actual_tree" = "$expected_tree" ]; then
    echo "== $repo: worktree snapshot already present, skipping"
    continue
  fi
  if [ -n "$(git -C "$repo" status --porcelain --untracked-files=all)" ]; then
    echo "== $repo: refusing to overlay a snapshot onto a different dirty worktree"
    fail=1
    continue
  fi
  if [ "$(git -C "$repo" rev-parse 'HEAD^{tree}')" != "$base" ]; then
    echo "== $repo: snapshot base tree does not match the applied commit series"
    fail=1
    continue
  fi
  echo "== $repo: applying exact dirty-worktree snapshot"
  if ! git -C "$repo" apply --check --binary "$PWD/$snapshot" || \
     ! git -C "$repo" apply --binary "$PWD/$snapshot"; then
    echo "   FAILED: worktree snapshot did not apply"
    fail=1
    continue
  fi
  actual_tree="$(worktree_tree "$repo" "$SNAPSHOT_TMP/$key.verify.index")" || { fail=1; continue; }
  if [ "$actual_tree" != "$expected_tree" ]; then
    echo "   FAILED: applied snapshot tree does not match exported tree"
    fail=1
  fi
done

[ "$fail" -eq 0 ] && echo "== all port patches applied" || exit 1
