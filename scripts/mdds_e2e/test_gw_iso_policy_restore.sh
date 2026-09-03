#!/usr/bin/env bash
# Deterministic host-only test for the policy reconciliation code embedded in
# run_mdds_gw.sh.  No HDC, network, process, or repository mutation occurs.
set -euo pipefail

source_path="${1:-scripts/run_mdds_gw.sh}"
if [ ! -f "$source_path" ] || [ -L "$source_path" ]; then
  echo "GW_ISO_POLICY_UNIT_RESULT FAIL invalid_source" >&2
  exit 2
fi

policy_lib=$(awk '
  /^[[:space:]]*cat <<'"'"'GW_ISO_POLICY_LIB_EOF'"'"'$/ { capture = 1; next }
  capture && /^GW_ISO_POLICY_LIB_EOF$/ { exit }
  capture { print }
' "$source_path")
if [ -z "$policy_lib" ]; then
  echo "GW_ISO_POLICY_UNIT_RESULT FAIL library_not_found" >&2
  exit 2
fi
eval "$policy_lib"

state_file=$(mktemp)
next_file=$(mktemp)
calls_file=$(mktemp)
trap 'rm -f "$state_file" "$next_file" "$calls_file"' EXIT

ip() {
  if [ "$*" = '-4 rule show' ]; then
    command cat "$state_file"
    return 0
  fi
  if [ "$1 $2 $3" = '-4 rule del' ]; then
    command awk '
      BEGIN { removed = 0 }
      !removed && /^16000:.*fwmark 0\/0xffff iif lo lookup 2006$/ { removed = 1; next }
      { print }
      END { if (!removed) exit 1 }
    ' "$state_file" > "$next_file"
    command mv "$next_file" "$state_file"
    printf 'del\n' >> "$calls_file"
    return 0
  fi
  printf 'unexpected fake ip invocation: %s\n' "$*" >&2
  return 99
}

exact_re='^16000:.*fwmark 0/0xffff iif lo lookup 2006$'
selector_re='^16000:.*fwmark 0/0xffff iif lo lookup [0-9][0-9]*$'
exact_line=$'16000:\tfrom all fwmark 0/0xffff iif lo lookup 2006'
conflict_line=$'16000:\tfrom all fwmark 0/0xffff iif lo lookup 2003'

printf '%s\n%s\n' "$exact_line" "$exact_line" > "$state_file"
: > "$calls_file"
collapse_out=$(gw_iso_ensure_rule "$exact_re" "$selector_re" pref 16000 fwmark 0/0xffff iif lo table 2006)
[ "$collapse_out" = 'GW_ISO_POLICY_RESTORE_COLLAPSED kind=rule removed=1' ]
[ "$(gw_iso_rule_count "$exact_re")" = 1 ]
[ "$(command grep -c '^del$' "$calls_file")" = 1 ]
printf 'GW_ISO_POLICY_UNIT exact_duplicate=PASS remaining=1 removed=1\n'

printf '%s\n%s\n' "$exact_line" "$conflict_line" > "$state_file"
: > "$calls_file"
set +e
conflict_out=$(gw_iso_ensure_rule "$exact_re" "$selector_re" pref 16000 fwmark 0/0xffff iif lo table 2006)
conflict_rc=$?
set -e
[ "$conflict_rc" -ne 0 ]
[ "$conflict_out" = 'GW_ISO_POLICY_RESTORE_CONFLICT kind=rule total=2 exact=1' ]
[ "$(command wc -l < "$state_file" | tr -d ' ')" = 2 ]
[ ! -s "$calls_file" ]
printf 'GW_ISO_POLICY_UNIT mixed_conflict=PASS retained=2 deleted=0\n'
printf 'GW_ISO_POLICY_UNIT_RESULT PASS\n'
