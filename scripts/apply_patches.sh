#!/usr/bin/env bash
# Apply the exported OHOS port patch series (patches/) onto a fresh src/
# checkout populated by `vcs import --input ros2.repos src/`.
#
# Idempotent: a series whose diff is already present in the working tree
# (reverse-apply check) is skipped. Uses `git am --3way` so the patches can
# still apply after `vcs pull` moved the upstream base forward; on a 3-way
# conflict, resolve in the listed repo and `git am --continue`, then re-run
# scripts/export_patches.sh.
set -uo pipefail
cd "$(dirname "$0")/.."

fail=0
for p in patches/*.patch; do
  [ -f "$p" ] || continue
  key="$(basename "$p" .patch)"
  repo="src/${key//__//}"
  if [ ! -d "$repo/.git" ]; then
    echo "== $repo: not checked out, skipping (run vcs import first)"
    continue
  fi
  if [ -d "$repo/.git/rebase-apply" ]; then
    echo "== $repo: git am in progress, resolve it first (git am --continue/--abort)"
    fail=1
    continue
  fi
  if git -C "$repo" apply --reverse --check "$PWD/$p" >/dev/null 2>&1; then
    echo "== $repo: already applied, skipping"
    continue
  fi
  base="$(cat "patches/$key.base" 2>/dev/null || true)"
  head="$(git -C "$repo" rev-parse HEAD)"
  echo "== $repo: applying $(grep -c '^From ' "$p") commit(s)"
  if [ -n "$base" ] && [ "$head" != "$base" ]; then
    echo "   note: upstream moved (${base:0:9} -> ${head:0:9}), using 3-way apply"
  fi
  if git -C "$repo" am --3way "$PWD/$p"; then
    :
  else
    echo "   FAILED: resolve conflicts in $repo, then 'git -C $repo am --continue'"
    fail=1
  fi
done

[ "$fail" -eq 0 ] && echo "== all port patches applied" || exit 1
