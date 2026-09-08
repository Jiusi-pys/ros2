#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture="$(mktemp -d)"
trap 'rm -rf -- "$fixture"' EXIT
line="$(grep '^  out="$(remote.*wc -c' scripts/deploy_ohos_generic.sh)"
test -n "$line"
[[ "$line" != *'tr -d'* ]]
remote() { printf '%s' "$2"; }
archive_remote="$fixture/archive"
# Evaluate the actual deployment line with a transport spy, then exercise its
# actual size predicate. No tar extraction or board command is executed here.
for bytes in 0 185; do
  head -c "$bytes" /dev/zero > "$archive_remote"
  for expected in "$bytes" "$((bytes + 1))"; do
    archive_bytes="$expected"
    set +u
    eval "$line"
    set -u
    predicate="${out%%; then*}"
    rc=0
    sh -c "$predicate; then exit 1; else exit 0; fi" || rc=$?
    if [[ "$expected" == "$bytes" ]]; then test "$rc" = 0; else test "$rc" = 1; fi
  done
done
echo 'GENERIC_DEPLOY_SIZE_CONTRACT=PASS empty,nonempty,mismatch,no-remote-tr'
