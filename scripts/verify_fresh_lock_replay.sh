#!/usr/bin/env bash
# Import the immutable release manifest into an empty directory, replay every
# patch/snapshot, and compare every resulting repository tree with this source
# workspace. The destination is intentionally retained as review evidence.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd -P)"
cd "$ROOT"

if [ "$#" -gt 1 ]; then
  echo "Usage: ./scripts/verify_fresh_lock_replay.sh [empty-destination]" >&2
  exit 2
fi
LOCK="${OHOS_SOURCE_MANIFEST:-$ROOT/ros2.ohos.lock.repos}"
LOCK="$(cd "$(dirname "$LOCK")" && pwd -P)/$(basename "$LOCK")"
[ -f "$LOCK" ] || {
  echo "ERROR: $LOCK is missing; export patches and freeze the manifest first" >&2
  exit 2
}

if [ "$#" -eq 1 ]; then
  DEST="$1"
  mkdir -p "$DEST"
else
  DEST="$(mktemp -d "${TMPDIR:-/tmp}/ros2-lock-replay.XXXXXX")"
fi
DEST="$(cd "$DEST" && pwd -P)"
if [ -n "$(find "$DEST" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
  echo "ERROR: replay destination is not empty: $DEST" >&2
  exit 2
fi

for tool in git find grep sha256sum awk; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "ERROR: missing host tool: $tool" >&2
    exit 2
  }
done
if command -v pixi >/dev/null 2>&1; then
  VCS=(pixi run vcs)
  HOST_PYTHON=(pixi run python)
elif command -v vcs >/dev/null 2>&1; then
  VCS=(vcs)
  HOST_PYTHON=(python3)
else
  echo "ERROR: neither pixi nor vcs is available" >&2
  exit 2
fi

TMP_BASE="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
INDEX_TMP="$(mktemp -d "$TMP_BASE/ros2-tree-compare.XXXXXX")"
case "$INDEX_TMP" in "$TMP_BASE"/ros2-tree-compare.*) ;; *) exit 70 ;; esac
cleanup_indexes() {
  case "$INDEX_TMP" in "$TMP_BASE"/ros2-tree-compare.*) rm -rf -- "$INDEX_TMP" ;; esac
}
trap cleanup_indexes EXIT
worktree_tree() { # <repo> <private-index> [exclude imported src]
  local repo="$1" private_index="$2"
  rm -f "$private_index"
  GIT_INDEX_FILE="$private_index" git -C "$repo" read-tree HEAD
  if [ "${3:-}" = exclude-src ]; then
    GIT_INDEX_FILE="$private_index" git -C "$repo" add -A -- . ':(exclude)src'
  else
    GIT_INDEX_FILE="$private_index" git -C "$repo" add -A
  fi
  GIT_INDEX_FILE="$private_index" git -C "$repo" write-tree
}

# Materialize the complete meta workspace from its fixed public commit plus an
# exact binary patch of this uncommitted review candidate.  This keeps the
# source checkout/index untouched while making DEST directly buildable.
META_URL="${ROS2_META_URL:-https://github.com/Jiusi-pys/ros2.git}"
case "$META_URL" in https://*) ;; *) echo "ERROR: ROS2_META_URL must be public HTTPS" >&2; exit 2 ;; esac
META_LOCAL_HEAD="$(git -C "$ROOT" rev-parse HEAD)"
META_BASE="${ROS2_META_BASE:-$(git -C "$ROOT" rev-parse refs/remotes/origin/jazzy_ohos)}"
if ! git -C "$ROOT" merge-base --is-ancestor "$META_BASE" "$META_LOCAL_HEAD"; then
  echo "ERROR: public meta base is not an ancestor of the local candidate: $META_BASE" >&2
  exit 2
fi
META_PATCH="$INDEX_TMP/meta-worktree.patch"
META_SOURCE_INDEX="$INDEX_TMP/meta-source.index"
rm -f "$META_SOURCE_INDEX"
GIT_INDEX_FILE="$META_SOURCE_INDEX" git -C "$ROOT" read-tree "$META_BASE"
GIT_INDEX_FILE="$META_SOURCE_INDEX" git -C "$ROOT" add -A -- . ':(exclude)src'
GIT_INDEX_FILE="$META_SOURCE_INDEX" git -C "$ROOT" diff --cached --binary --full-index "$META_BASE" > "$META_PATCH"
META_PATCH_SHA="$(sha256sum "$META_PATCH" | awk '{print $1}')"
META_SOURCE_TREE="$(GIT_INDEX_FILE="$META_SOURCE_INDEX" git -C "$ROOT" write-tree)"

materialize_meta_candidate() {
  local destination="$1" url="$2" base="$3" patch="$4"
  git -C "$destination" init --quiet
  git -C "$destination" remote add origin "$url"
  git -C "$destination" fetch --depth 1 origin "$base"
  # Populate the candidate index before writing any working files. Otherwise
  # git apply can use the base's attributes and create CRLF Python inputs even
  # though the candidate adds eol=lf. A matching Git tree conceals that drift.
  git -C "$destination" update-ref --no-deref HEAD "$base"
  git -C "$destination" read-tree "$base"
  # Once the meta candidate itself is published, the local delta is empty.
  # Applying an empty patch fails with "No valid patches in input" even
  # though the checked-out base already is the requested candidate.
  if [ -s "$patch" ]; then
    git -C "$destination" apply --cached --binary --whitespace=nowarn "$patch"
  fi
  git -C "$destination" checkout-index --all
}
materialize_meta_candidate "$DEST" "$META_URL" "$META_BASE" "$META_PATCH"
[ -f "$DEST/ros2.ohos.lock.repos" ] && [ -f "$DEST/scripts/apply_patches.sh" ] || {
  echo "ERROR: meta snapshot did not materialize the release inputs" >&2
  exit 1
}
mkdir -p "$DEST/src"

# A matching normalized Git tree alone can conceal CRLF-corrupted compiler
# wrappers or drift in the raw recipe bytes sealed by a build receipt.
"${HOST_PYTHON[@]}" - "$ROOT" "$DEST" <<'PY'
from pathlib import Path
import runpy
import sys

source, destination = (Path(value).resolve() for value in sys.argv[1:])
helper = runpy.run_path(str(source / "target_deps_src/lib/clean_deps_receipt.py"))
def byte_inventory(root):
    return {entry["path"]: (entry["size"], entry["sha256"])
            for entry in helper["recipe_file_inventory"](root)}
expected, actual = byte_inventory(source), byte_inventory(destination)
if expected != actual:
    mismatch = sorted(path for path in expected.keys() | actual.keys()
                      if expected.get(path) != actual.get(path))
    raise SystemExit("ERROR: replay recipe bytes differ: " + ", ".join(mismatch))
print(f"FRESH_REPLAY_RECIPE_BYTES files={len(expected)} result=EXACT")
PY

echo "FRESH_REPLAY_DEST=$DEST"
printf 'FRESH_REPLAY_META public_base=%s local_head=%s patch_sha256=%s source_tree=%s result=EXACT\n' \
  "$META_BASE" "$META_LOCAL_HEAD" "$META_PATCH_SHA" "$META_SOURCE_TREE"
# A transient remote clone failure must be retried inside this one auditable
# empty-directory run; `set -e` still rejects the import if any repository is
# unavailable after the bounded retry budget.
"${VCS[@]}" import --retry 3 --input "$LOCK" "$DEST/src"
expected_count="$(grep -Ec '^  [^[:space:]][^:]*:$' "$LOCK")"
actual_count="$(find "$DEST/src" -maxdepth 3 -name .git -type d | wc -l | tr -d ' ')"
if [ "$actual_count" != "$expected_count" ]; then
  echo "ERROR: locked import count mismatch: expected=$expected_count actual=$actual_count" >&2
  exit 1
fi
printf 'FRESH_REPLAY_IMPORT repositories=%s result=EXACT\n' "$actual_count"

# Fast-DDS consumes its bundled Asio and TinyXML2 with network updates disabled
# during the release build.  A vcs import materializes only the parent gitlinks,
# so populate exactly those locked submodules here and prove that neither is at
# a different commit.  Initializing every optional Fast-DDS submodule would add
# unrelated network inputs to this release profile.
FASTDDS="$DEST/src/eProsima/Fast-DDS"
[ -d "$FASTDDS/.git" ] || {
  echo "ERROR: locked Fast-DDS repository is missing: $FASTDDS" >&2
  exit 1
}
git -C "$FASTDDS" submodule update --init --depth 1 -- thirdparty/asio thirdparty/tinyxml2
for submodule in thirdparty/asio thirdparty/tinyxml2; do
  expected_submodule="$(git -C "$FASTDDS" ls-tree HEAD -- "$submodule" | awk '{print $3}')"
  actual_submodule="$(git -C "$FASTDDS/$submodule" rev-parse HEAD)"
  if [ -z "$expected_submodule" ] || [ "$actual_submodule" != "$expected_submodule" ]; then
    echo "ERROR: Fast-DDS submodule mismatch path=$submodule expected=$expected_submodule actual=$actual_submodule" >&2
    exit 1
  fi
  printf 'FRESH_REPLAY_SUBMODULE path=%s commit=%s result=EXACT\n' \
    "$submodule" "$actual_submodule"
done

# `git am` needs a committer identity but the resulting commit SHA is not the
# reproducibility contract; exact trees are. Do not mutate global/local config.
export GIT_COMMITTER_NAME="${GIT_COMMITTER_NAME:-ROS 2 patch replay}"
export GIT_COMMITTER_EMAIL="${GIT_COMMITTER_EMAIL:-ros2-replay@example.invalid}"
(cd "$DEST" && OHOS_SOURCE_MANIFEST="$LOCK" bash scripts/apply_patches.sh)

source_count="$("${HOST_PYTHON[@]}" - "$LOCK" "$ROOT/src" <<'PYCOUNT'
import sys, yaml
from pathlib import Path
manifest, source = map(Path, sys.argv[1:])
repositories = yaml.safe_load(manifest.read_text())["repositories"]
print(sum((source / name / ".git").exists() for name in repositories))
PYCOUNT
)"
if [ "$source_count" != 0 ] && [ "$source_count" != "$expected_count" ]; then
  echo "ERROR: source workspace is partial: expected=0-or-$expected_count actual=$source_count" >&2
  exit 1
fi

compared=0
while IFS= read -r replay_gitdir; do
  replay_repo="${replay_gitdir%/.git}"
  relative="${replay_repo#"$DEST/src/"}"
  replay_repo="$DEST/src/$relative"
  key="${relative//\//__}"
  replay_tree="$(worktree_tree "$replay_repo" "$INDEX_TMP/$key.replay.index")"
  if [ "$source_count" = "$expected_count" ]; then
    source_tree="$(worktree_tree "$ROOT/src/$relative" "$INDEX_TMP/$key.source.index")"
    expected_tree="$source_tree"
    evidence=source-worktree
  elif [ -f "$ROOT/patches/$key.snapshot.tree" ]; then
    expected_tree="$(tr -d '\r\n' < "$ROOT/patches/$key.snapshot.tree")"
    evidence=snapshot-metadata
  elif [ -f "$ROOT/patches/$key.tree" ]; then
    expected_tree="$(tr -d '\r\n' < "$ROOT/patches/$key.tree")"
    evidence=series-metadata
  else
    expected_tree="$(git -C "$replay_repo" rev-parse 'HEAD^{tree}')"
    evidence=locked-commit
  fi
  if [ "$expected_tree" != "$replay_tree" ]; then
    echo "ERROR: replay tree differs for $relative: expected=$expected_tree replay=$replay_tree evidence=$evidence" >&2
    exit 1
  fi
  compared=$((compared + 1))
done < <(find "$DEST/src" -maxdepth 3 -name .git -type d | LC_ALL=C sort)

if [ "$compared" != "$expected_count" ]; then
  echo "ERROR: source tree comparison count mismatch: expected=$expected_count actual=$compared" >&2
  exit 1
fi
META_REPLAY_TREE="$(worktree_tree "$DEST" "$INDEX_TMP/meta-replay.index" exclude-src)"
if [ "$META_REPLAY_TREE" != "$META_SOURCE_TREE" ]; then
  echo "ERROR: replay meta tree differs: expected=$META_SOURCE_TREE replay=$META_REPLAY_TREE" >&2
  exit 1
fi
printf 'FRESH_REPLAY_TREES repositories=%s result=EXACT evidence=%s destination=%s\n' \
  "$compared" "${evidence:-none}" "$DEST"
