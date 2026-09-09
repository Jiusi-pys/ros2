#!/usr/bin/env bash
# Export unpublished commits plus an exact dirty-worktree snapshot for every
# src/ repository.  A temporary index captures tracked and non-ignored
# untracked files without touching the real index or working tree.
set -euo pipefail
cd "$(dirname "$0")/.."

SOURCE_MANIFEST="${OHOS_SOURCE_MANIFEST:-ros2.repos}"
SOURCE_ROOT_TEXT="$(pixi run python scripts/manifest_source_roots.py --manifest "$SOURCE_MANIFEST" --source-root src)" || exit 2
SOURCE_BASE="$(pixi run python -c 'from pathlib import Path; print(Path("src").resolve().as_posix())' | tr -d '\r')"
mapfile -t EXPORT_ROOTS < <(printf '%s\n' "$SOURCE_ROOT_TEXT" | tr -d '\r' | sed 's|\\|/|g')
mkdir -p patches
TMP_BASE="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
TMP_ROOT="$(mktemp -d "$TMP_BASE/ros2-patch-export.XXXXXX")"
case "$TMP_ROOT" in "$TMP_BASE"/ros2-patch-export.*) ;; *) exit 70 ;; esac
cleanup() {
  case "$TMP_ROOT" in "$TMP_BASE"/ros2-patch-export.*) rm -rf -- "$TMP_ROOT" ;; esac
}
trap cleanup EXIT

series_count=0
snapshot_count=0
failed=0

manifest_version_for() { # <repository-key>
  # ros2.repos is generated in the conventional vcstool shape.  Read only
  # the exact entry's scalar version so a locally renamed branch (for example
  # jiusi on top of origin/jazzy) still has a fetchable provenance boundary.
  awk -v target="$1" '
    $0 == "  " target ":" { in_target = 1; next }
    in_target && /^  [^ ]/ { exit }
    in_target && /^    version: / {
      sub(/^    version: /, "")
      gsub(/^['\''"]|['\''"]$/, "")
      print
      exit
    }
  ' "$SOURCE_MANIFEST"
}

provenance_ref_for() { # <repo> <relative-path> <local-branch>
  local repo="$1" rel="$2" branch="$3" candidate manifest_version
  candidate="$(git -C "$repo" rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>/dev/null || true)"
  if [ -n "$candidate" ] && git -C "$repo" cat-file -e "$candidate^{commit}" 2>/dev/null; then
    printf '%s\n' "$candidate"
    return 0
  fi
  candidate="origin/$branch"
  if git -C "$repo" cat-file -e "$candidate^{commit}" 2>/dev/null; then
    printf '%s\n' "$candidate"
    return 0
  fi
  manifest_version="$(manifest_version_for "$rel")"
  [ -n "$manifest_version" ] || return 1
  for candidate in "origin/$manifest_version" "$manifest_version"; do
    if git -C "$repo" cat-file -e "$candidate^{commit}" 2>/dev/null; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

for repo in "${EXPORT_ROOTS[@]}"; do
  rel="${repo#"$SOURCE_BASE/"}"
  key="${rel//\//__}"
  branch="$(git -C "$repo" symbolic-ref --short -q HEAD || true)"
  if [ -z "$branch" ]; then
    echo "ERROR: $rel is detached; attach the intended release branch" >&2
    failed=1
    continue
  fi
  provenance_ref="$(provenance_ref_for "$repo" "$rel" "$branch" || true)"
  if [ -z "$provenance_ref" ]; then
    echo "ERROR: $rel has no fetchable branch/manifest provenance boundary" >&2
    failed=1
    continue
  fi

  if git -C "$repo" merge-base --is-ancestor HEAD "$provenance_ref"; then
    rm -f "patches/$key.patch" "patches/$key.base" "patches/$key.tree"
  else
    base="$(git -C "$repo" merge-base "$provenance_ref" HEAD)"
    if [ -z "$base" ]; then
      echo "ERROR: $rel has no merge base with $provenance_ref" >&2
      failed=1
      continue
    fi
    ahead="$(git -C "$repo" rev-list --count "$base..HEAD")"
    if [ "$ahead" -le 0 ]; then
      echo "ERROR: empty unpublished range for $rel" >&2
      failed=1
      continue
    fi
    git -C "$repo" format-patch --stdout "$base..HEAD" > "patches/$key.patch"
    printf '%s\n' "$base" > "patches/$key.base"
    git -C "$repo" rev-parse 'HEAD^{tree}' > "patches/$key.tree"
    exported="$(grep -Ec '^From [0-9a-f]{40} ' "patches/$key.patch" || true)"
    if [ "$exported" -ne "$ahead" ]; then
      echo "ERROR: patch count mismatch for $rel: range=$ahead patch=$exported" >&2
      failed=1
      continue
    fi
    echo "== $rel: $ahead unpublished commit(s) -> patches/$key.patch"
    series_count=$((series_count + 1))
  fi

  # Snapshot the index+worktree through a private index. `git add -A` includes
  # tracked changes and non-ignored untracked files, while the user's actual
  # index and staging state remain byte-for-byte untouched.
  private_index="$TMP_ROOT/$key.index"
  GIT_INDEX_FILE="$private_index" git -C "$repo" read-tree HEAD
  GIT_INDEX_FILE="$private_index" git -C "$repo" add -A
  snapshot_tree="$(GIT_INDEX_FILE="$private_index" git -C "$repo" write-tree)"
  head_tree="$(git -C "$repo" rev-parse 'HEAD^{tree}')"
  if [ "$snapshot_tree" = "$head_tree" ]; then
    rm -f "patches/$key.snapshot.patch" "patches/$key.snapshot.base" "patches/$key.snapshot.tree"
  else
    GIT_INDEX_FILE="$private_index" git -C "$repo" diff --cached --binary --full-index HEAD -- > \
      "patches/$key.snapshot.patch"
    # Record tree IDs as values, not as objects that a fresh clone must
    # already possess. git-am may recreate an equivalent commit with a new
    # committer timestamp/SHA; its HEAD tree is the stable snapshot base.
    printf '%s\n' "$head_tree" > "patches/$key.snapshot.base"
    printf '%s\n' "$snapshot_tree" > "patches/$key.snapshot.tree"
    [ -s "patches/$key.snapshot.patch" ] || {
      echo "ERROR: non-empty snapshot tree produced an empty patch for $rel" >&2
      failed=1
      continue
    }
    echo "== $rel: dirty tracked/untracked snapshot -> patches/$key.snapshot.patch"
    snapshot_count=$((snapshot_count + 1))
  fi
done

echo "exported $series_count commit series and $snapshot_count worktree snapshots"
[ "$failed" -eq 0 ] || exit 1
