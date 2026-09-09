#!/usr/bin/env bash
# Verify that a rejected deployment environment cannot fall through to an
# inherited ROS overlay or any subsequent board payload.
set -euo pipefail
cd "$(dirname "$0")/.."

TMP_BASE="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
TMP_ROOT="$(mktemp -d "$TMP_BASE/ros2-env-source-test.XXXXXX")"
case "$TMP_ROOT" in "$TMP_BASE"/ros2-env-source-test.*) ;; *) exit 70 ;; esac
cleanup() {
  case "$TMP_ROOT" in
    "$TMP_BASE"/ros2-env-source-test.*) rm -rf -- "$TMP_ROOT" ;;
  esac
}
trap cleanup EXIT

mkdir -p "$TMP_ROOT/mixed-overlay"
sentinel="$TMP_ROOT/stale-overlay-ran"
template="$PWD/scripts/env_ohos.template.sh"
output="$TMP_ROOT/rejected.out"
rc=0
ROS2_SENTINEL="$sentinel" sh -c '
  ros2() { printf stale-overlay > "$ROS2_SENTINEL"; }
  export ROS2_HOME="$1"
  export ROS2_DEPLOY_EXPECTED_MARKER=expected-marker
  . "$2" || exit 70
  ros2 --help
' sh "$TMP_ROOT/mixed-overlay" "$template" >"$output" 2>&1 || rc=$?
if [ "$rc" -ne 70 ]; then
  echo "ERROR: rejected env source returned rc=$rc instead of 70" >&2
  cat "$output" >&2
  exit 1
fi
if [ -e "$sentinel" ]; then
  echo "ERROR: stale overlay ran after rejected env source" >&2
  exit 1
fi
grep -Fq 'ERROR: incomplete or mixed ROS 2 deployment' "$output"

printf 'export ROS2_VALID_ENV=accepted\n' > "$TMP_ROOT/valid-env.sh"
ROS2_SENTINEL="$TMP_ROOT/valid-ran" sh -c '
  . "$1" || exit 70
  printf "%s" "$ROS2_VALID_ENV" > "$ROS2_SENTINEL"
' sh "$TMP_ROOT/valid-env.sh"
if [ "$(cat "$TMP_ROOT/valid-ran")" != accepted ]; then
  echo "ERROR: valid env did not execute the guarded payload" >&2
  exit 1
fi

# Inventory every shell runner, including quoted command prefixes. A direct
# `env.sh;` or profile `.env;` source is unguarded; checked sources contain an
# operator before that record's terminating semicolon.
unsafe="$(grep -RInE --include='*.sh' \
  '\.[[:space:]]+[^;&|]*(env\.sh|\.env);' scripts \
  --exclude='test_env_source_contract.sh' || true)"
if [ -n "$unsafe" ]; then
  echo "ERROR: automated source site can continue after source failure:" >&2
  printf '%s\n' "$unsafe" >&2
  exit 1
fi

echo "env source fail-closed contract: PASS"
