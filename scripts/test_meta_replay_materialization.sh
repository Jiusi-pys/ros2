#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture="$(mktemp -d)"
trap 'rm -rf -- "$fixture"' EXIT
eval "$(sed -n '/^materialize_meta_candidate() {/,/^}/p' scripts/verify_fresh_lock_replay.sh)"
# Exercise the Windows default even on hosts whose own Git default is LF.
git() { command git -c core.autocrlf=true "$@"; }
mkdir "$fixture/origin" "$fixture/destination"
git -C "$fixture/origin" init --quiet
printf '*.sh text eol=lf\n' > "$fixture/origin/.gitattributes"
printf 'obsolete\n' > "$fixture/origin/obsolete"
git -C "$fixture/origin" add -A
git -C "$fixture/origin" -c user.name=Fixture -c user.email=fixture@example.invalid commit --quiet -m base
base="$(git -C "$fixture/origin" rev-parse HEAD)"
cp .gitattributes "$fixture/origin/.gitattributes"
mkdir -p "$fixture/origin/target_deps_src/ohos-autotools-bin"
cp target_deps_src/ohos-autotools-bin/ohos-cc "$fixture/origin/target_deps_src/ohos-autotools-bin/ohos-cc"
printf 'first\r\nsecond\n' > "$fixture/origin/.gitignore"
printf 'print("fixed bytes")\n' > "$fixture/origin/recipe.py"
rm "$fixture/origin/obsolete"
git -C "$fixture/origin" add -A
git -C "$fixture/origin" diff --cached --binary --full-index --output="$fixture/meta.patch" "$base"
materialize_meta_candidate "$fixture/destination" "$fixture/origin" "$base" "$fixture/meta.patch"
[ ! -e "$fixture/destination/obsolete" ]
cmp "$fixture/origin/recipe.py" "$fixture/destination/recipe.py"
cmp "$fixture/origin/.gitignore" "$fixture/destination/.gitignore"
cmp "$fixture/origin/target_deps_src/ohos-autotools-bin/ohos-cc" "$fixture/destination/target_deps_src/ohos-autotools-bin/ohos-cc"
[ "$(git -C "$fixture/origin" write-tree)" = "$(git -C "$fixture/destination" write-tree)" ]
echo 'META_MATERIALIZATION_CONTRACT result=PASS checks=exact-bytes,compiler-wrapper,mixed-ignore,new-attributes,deleted-file,tree'
