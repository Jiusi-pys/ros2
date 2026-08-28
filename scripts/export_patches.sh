#!/usr/bin/env bash
# Export every src/ subrepo's local (OHOS port) commits as a patch series into
# patches/, so the port survives without push access to the upstream repos.
#
# For each git repo under src/ with commits ahead of origin/<branch>, writes
#   patches/<repo-path-with-__>.patch   (git format-patch --stdout series)
#   patches/<repo-path-with-__>.base    (upstream base commit the series sits on)
#
# Re-run after changing/amending any subrepo commit. The generated files are
# tracked in THIS repository; apply them on a fresh checkout with
# scripts/apply_patches.sh.
set -euo pipefail
cd "$(dirname "$0")/.."

mkdir -p patches
found=0
while IFS= read -r gitdir; do
  repo="${gitdir%/.git}"
  rel="${repo#src/}"
  key="${rel//\//__}"
  branch="$(git -C "$repo" symbolic-ref --short -q HEAD || true)"
  [ -n "$branch" ] || continue                       # detached HEAD: skip (warn below)
  git -C "$repo" rev-parse --verify -q "origin/$branch" >/dev/null || continue
  ahead="$(git -C "$repo" rev-list --count "origin/$branch..HEAD")"
  if [ "$ahead" -eq 0 ]; then
    # no local commits left: remove a stale exported series if present
    rm -f "patches/$key.patch" "patches/$key.base"
    continue
  fi
  # true base = parent of the first local commit (origin/<branch> may have
  # moved forward since the local branch was created, e.g. after a vcs pull)
  first="$(git -C "$repo" rev-list "origin/$branch..HEAD" | tail -1)"
  base="$(git -C "$repo" rev-parse "$first^")"
  git -C "$repo" format-patch --stdout "$base..HEAD" > "patches/$key.patch"
  echo "$base" > "patches/$key.base"
  echo "== $rel: $ahead commit(s) on $branch -> patches/$key.patch"
  found=$((found+1))
done < <(find src -maxdepth 3 -name .git -type d | sort)

echo "exported $found repo series into patches/"
