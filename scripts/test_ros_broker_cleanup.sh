#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/lib/ros_broker_cleanup.sh
record="$(mktemp)"
trap 'rm -f "$record"' EXIT
mode=normal
MDDS_OWNED_TRACKED=(worker)
MDDS_OWNED_LOCK_BOARDS=(A B)
mdds_owned_stop_all() {
  printf 'stop\n' >> "$record"
  if [[ "$mode" == worker_failure ]]; then return 1; fi
  MDDS_OWNED_TRACKED=()
}
ros_broker_cleanup_daemons() {
  printf 'daemon:%s\n' "$1" >> "$record"
  [[ "$mode" != daemon_failure || "$1" != A ]]
}
mdds_owned_release_locks() { printf 'release\n' >> "$record"; }
ros_broker_finish
[[ "$(cat "$record")" == $'stop\ndaemon:A\ndaemon:B\nrelease' ]]
: > "$record";mode=worker_failure;MDDS_OWNED_TRACKED=(worker)
if ros_broker_finish; then exit 1; fi
[[ "$(cat "$record")" == stop ]]
: > "$record";mode=daemon_failure;MDDS_OWNED_TRACKED=(worker)
if ros_broker_finish; then exit 1; fi
[[ "$(cat "$record")" == $'stop\ndaemon:A\ndaemon:B' ]]
printf 'ROS_BROKER_CLEANUP_ORDER PASS cases=3\n'
