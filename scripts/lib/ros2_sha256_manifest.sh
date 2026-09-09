#!/usr/bin/env bash
# Convert Git-for-Windows sha256sum's binary-mode marker into the portable
# two-space form accepted by both GNU coreutils and KaihongOS/toybox.

ros2_normalize_sha256_manifest() { # <raw-manifest> <new-output>
  local input="$1" output="$2" line path relative reserved
  if [[ ! -f "$input" || -L "$input" || -e "$output" || -L "$output" ]]; then
    echo "ERROR: SHA-256 manifest normalization requires a regular input and a new output" >&2
    return 1
  fi
  # sha256sum's marker is exactly column 66: one space followed by either
  # '*' (binary) or ' ' (text). Change only '*' in that position. A path is
  # accepted only when it is the find-generated ./relative form; escaped or
  # control-character filenames fail closed instead of producing a manifest
  # whose parser semantics differ across hosts.
  if ! (umask 077; set -C; LC_ALL=C sed -E \
      's/^([0-9a-f]{64}) \*/\1  /' "$input" > "$output") 2>/dev/null; then
    echo "ERROR: could not create normalized SHA-256 manifest" >&2
    return 1
  fi
  if [[ ! -s "$output" ]] || LC_ALL=C grep -Env \
      '^[0-9a-f]{64}  \./[^[:cntrl:]]+$' "$output" >/dev/null; then
    echo "ERROR: SHA-256 manifest contains a non-portable record" >&2
    return 1
  fi
  while IFS= read -r line || [[ -n "$line" ]]; do
    # The canonical prefix is 66 ASCII bytes. Validate the path independently
    # instead of treating sha256sum -c as a sandbox: no alternate separator,
    # empty/dot/traversal component, or deployment-control root may survive.
    path="${line:66}"
    relative="${path#./}"
    if [[ "$path" != ./* || -z "$relative" || "$relative" == /* ||
          "$relative" == *\\* ]]; then
      echo "ERROR: SHA-256 manifest contains an unsafe relative path: $path" >&2
      return 1
    fi
    case "/$relative/" in
      *//*|*/./*|*/../*)
        echo "ERROR: SHA-256 manifest contains an unsafe path component: $path" >&2
        return 1
        ;;
    esac
    for reserved in \
      deploy_manifest.sha256 env.sh release_provenance.json \
      .ros2_deploy_complete .ros2-activity-lock \
      .ros2_deploy_complete .ros2-activity-lock; do
      case "$relative" in
        "$reserved"|"$reserved"/*)
          echo "ERROR: SHA-256 manifest contains reserved deployment path: $path" >&2
          return 1
          ;;
      esac
    done
  done < "$output"
}
