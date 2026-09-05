#!/usr/bin/env bash
# Shared fail-closed source acquisition helpers for KaihongOS target deps.
# Callers must set SRC_DIR before sourcing this file.

if [ -z "${SRC_DIR:-}" ]; then
  echo "ERROR: SRC_DIR must be set before sourcing locked_sources.sh" >&2
  return 2 2>/dev/null || exit 2
fi

SOURCE_LOCK="${SRC_DIR}/sources.lock"
[ -f "$SOURCE_LOCK" ] || {
  echo "ERROR: target dependency lock is missing: $SOURCE_LOCK" >&2
  return 2 2>/dev/null || exit 2
}

sha256_file() {
  sha256sum "$1" | awk '{print $1}'
}

target_deps_clean_mode() {
  [ "${OHOS_TARGET_DEPS_CLEAN:-0}" = 1 ]
}

target_deps_journal() {
  target_deps_clean_mode || return 0
  [ -n "${OHOS_TARGET_DEPS_JOURNAL:-}" ] || {
    echo "ERROR: clean target-dependency build has no source journal" >&2
    return 1
  }
  [ -n "${OHOS_TARGET_DEPS_PYTHON:-}" ] && [ -x "$OHOS_TARGET_DEPS_PYTHON" ] || {
    echo "ERROR: clean target-dependency build has no executable receipt Python" >&2
    return 1
  }
  local receipt_tool="${OHOS_TARGET_DEPS_RECEIPT_TOOL:-${SRC_DIR}/lib/clean_deps_receipt.py}"
  [ -f "$receipt_tool" ] || {
    echo "ERROR: clean target-dependency receipt helper is missing: $receipt_tool" >&2
    return 1
  }
  "$OHOS_TARGET_DEPS_PYTHON" "$receipt_tool" journal \
    --journal "$OHOS_TARGET_DEPS_JOURNAL" "$@"
}

lock_record() { # <name>
  local name="$1" record count
  record="$(awk -v wanted="$name" '
    $1 !~ /^#/ && $2 == wanted { print $1 "\t" $2 "\t" $3 "\t" $4 }
  ' "$SOURCE_LOCK")"
  count="$(printf '%s\n' "$record" | awk 'NF { n++ } END { print n + 0 }')"
  if [ "$count" != 1 ]; then
    echo "ERROR: source lock must contain exactly one entry for $name (found $count)" >&2
    return 1
  fi
  printf '%s\n' "$record"
}

lock_field() { # <name> <field-number>
  lock_record "$1" | cut -f"$2"
}

fetch_locked() { # <lock-name> <output-path>
  local name="$1" output="$2" kind digest url actual partial
  kind="$(lock_field "$name" 1)"
  digest="$(lock_field "$name" 3)"
  url="$(lock_field "$name" 4)"
  [ "$kind" = archive ] || {
    echo "ERROR: $name is not an archive entry" >&2
    return 1
  }
  case "$url" in
    https://*) ;;
    *) echo "ERROR: refusing non-HTTPS source URL for $name: $url" >&2; return 1 ;;
  esac

  if [ ! -f "$output" ]; then
    partial="${output}.part.$$"
    echo "== downloading $name"
    if ! curl -fSL --proto '=https' --tlsv1.2 \
        --connect-timeout 30 --speed-time 60 --speed-limit 1024 --max-time 900 \
        --retry 3 --retry-all-errors \
        -o "$partial" "$url"; then
      rm -f -- "$partial"
      return 1
    fi
    actual="$(sha256_file "$partial")"
    if [ "$actual" != "$digest" ]; then
      echo "ERROR: checksum mismatch for downloaded $name: expected=$digest actual=$actual" >&2
      rm -f -- "$partial"
      return 1
    fi
    mv -f -- "$partial" "$output"
  fi

  actual="$(sha256_file "$output")"
  if [ "$actual" != "$digest" ]; then
    echo "ERROR: checksum mismatch for cached $name: expected=$digest actual=$actual" >&2
    return 1
  fi
  target_deps_journal --event source_verified --name "$name" \
    --kind "$kind" --digest "$digest" --url "$url" --path "$output"
  printf 'SOURCE_VERIFIED name=%s sha256=%s\n' "$name" "$digest"
}

ensure_locked_git_checkout() { # <lock-name> <destination>
  local name="$1" destination="$2" kind revision url actual
  kind="$(lock_field "$name" 1)"
  revision="$(lock_field "$name" 3)"
  url="$(lock_field "$name" 4)"
  [ "$kind" = git ] || { echo "ERROR: $name is not a git entry" >&2; return 1; }
  case "$url" in
    https://*) ;;
    *) echo "ERROR: refusing non-HTTPS git URL for $name: $url" >&2; return 1 ;;
  esac
  if [ ! -d "$destination/.git" ]; then
    [ ! -e "$destination" ] || {
      echo "ERROR: refusing to replace non-git path: $destination" >&2
      return 1
    }
    git clone --filter=blob:none --no-checkout "$url" "$destination"
    # The destination has no worktree yet, so pin line-ending policy before
    # checkout instead of inheriting a workstation's core.autocrlf setting.
    git -C "$destination" config core.autocrlf false
    git -C "$destination" config core.eol lf
    git -C "$destination" fetch --depth 1 origin "$revision"
    git -C "$destination" checkout --detach "$revision"
  fi
  actual="$(git -C "$destination" rev-parse HEAD)"
  if [ "$actual" != "$revision" ]; then
    echo "ERROR: git source $name is at $actual, expected $revision" >&2
    return 1
  fi
  if target_deps_clean_mode && [ -n "$(git -C "$destination" status --porcelain=v1 --untracked-files=all)" ]; then
    echo "ERROR: locked git source is dirty in clean mode: $destination" >&2
    return 1
  fi
  target_deps_journal --event source_verified --name "$name" \
    --kind "$kind" --digest "$revision" --url "$url" --path "$destination"
  printf 'SOURCE_VERIFIED name=%s commit=%s\n' "$name" "$revision"
}

apply_patch_locked() { # <source-root> <patch-file>
  local root="$1" patch_file="$2"
  [ -s "$patch_file" ] || { echo "ERROR: missing patch: $patch_file" >&2; return 1; }
  if patch -d "$root" -p1 -R --dry-run -f -s < "$patch_file" >/dev/null 2>&1; then
    if target_deps_clean_mode; then
      echo "ERROR: clean build source unexpectedly had a patch pre-applied: $patch_file" >&2
      return 1
    fi
    printf 'PATCH_VERIFIED file=%s state=already-applied\n' "$(basename "$patch_file")"
    return 0
  fi
  if ! patch -d "$root" -p1 --dry-run -f -s < "$patch_file" >/dev/null 2>&1; then
    echo "ERROR: patch is neither cleanly applicable nor already applied: $patch_file" >&2
    return 1
  fi
  patch -d "$root" -p1 -f -s < "$patch_file"
  patch -d "$root" -p1 -R --dry-run -f -s < "$patch_file" >/dev/null 2>&1 || {
    echo "ERROR: post-apply verification failed: $patch_file" >&2
    return 1
  }
  target_deps_journal --event patch_applied --name "$(basename "$patch_file")" \
    --digest "$(sha256_file "$patch_file")" --path "$patch_file" --target "$root"
  printf 'PATCH_VERIFIED file=%s state=applied\n' "$(basename "$patch_file")"
}

recipe_fingerprint() {
  printf '%s\n' "$@" | sha256sum | awk '{print $1}'
}

sdk_fingerprint() { # <native-sdk-directory>
  local native="$1" metadata="$1/oh-uni-package.json" clang="$1/llvm/bin/clang.exe"
  [ -f "$metadata" ] || { echo "ERROR: SDK metadata missing: $metadata" >&2; return 1; }
  [ -f "$clang" ] || { echo "ERROR: SDK clang missing: $clang" >&2; return 1; }
  recipe_fingerprint "$(sha256_file "$metadata")" "$(sha256_file "$clang")"
}

marker_matches() { # <marker> <fingerprint>
  [ -f "$1" ] && [ "$(tr -d '\r\n' < "$1")" = "$2" ]
}

write_marker() { # <marker> <fingerprint>
  local marker="$1" fingerprint="$2" temporary="${1}.tmp.$$"
  printf '%s\n' "$fingerprint" > "$temporary"
  mv -f -- "$temporary" "$marker"
}
