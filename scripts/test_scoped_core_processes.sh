#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
DEVICE_DIR=/data/local/tmp/ros2-generic
. scripts/lib/ros2_owned_processes.sh
fixture="$(mktemp -d)"
trap 'rm -rf -- "$fixture"' EXIT
mkdir -p "$fixture/self" "$fixture/101" "$fixture/202" "$fixture/303"
printf 'system shell\n' > "$fixture/self/maps"
printf '0000-1000 r-xp 0 0:0 1 /data/local/tmp/ros2-generic/Lib/librcl.so\n' > "$fixture/101/maps"
printf '0000-1000 r-xp 0 0:0 1 /data/local/tmp/ros2/Lib/libmdds.so\n' > "$fixture/202/maps"
printf '0000-1000 r-xp 0 0:0 1 /system/lib64/libc.so\n' > "$fixture/303/maps"
result="$(sh -c "$(ros2_scoped_process_command "$DEVICE_DIR" "$fixture")")"
test "$result" = 'ROS2_SCOPED_PID=101'
if ros2_scoped_process_command '/data/local/tmp/ros2-generic;bad' "$fixture"; then exit 1; fi
echo 'SCOPED_CORE_PROCESS_CONTRACT=PASS core-only,preserve-mdds,reject-injection'
