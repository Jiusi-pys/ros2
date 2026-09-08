#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
fixture="$(mktemp -d)"
trap 'rm -rf -- "$fixture"' EXIT
test -f scripts/ros2_generic_global_launcher.sh
eval "$(sed -n '/^run_generic_ros2() {/,/^}/p' scripts/ros2_generic_global_launcher.sh)"
mkdir -p "$fixture/valid"
printf 'ros2() { printf "ENV=%%s:%%s:%%s RMW=%%s ARG=%%s\\n" "$ROS_DISTRO" "$ROS_VERSION" "$ROS_PYTHON_VERSION" "$RMW_IMPLEMENTATION" "$1"; }\nexport RMW_IMPLEMENTATION=rmw_fastrtps_cpp\n' > "$fixture/valid/env.sh"
actual="$(unset ROS_DISTRO ROS_VERSION ROS_PYTHON_VERSION RMW_IMPLEMENTATION; run_generic_ros2 "$fixture/valid" --help)"
test "$actual" = 'ENV=jazzy:2:3 RMW=rmw_fastrtps_cpp ARG=--help'
actual="$(RMW_IMPLEMENTATION=rmw_cyclonedds_cpp run_generic_ros2 "$fixture/valid" --help)"
test "$actual" = 'ENV=jazzy:2:3 RMW=rmw_cyclonedds_cpp ARG=--help'
if RMW_IMPLEMENTATION=rmw_mdds run_generic_ros2 "$fixture/valid" --help; then exit 1; fi
mkdir "$fixture/bad"
printf 'return 70\n' > "$fixture/bad/env.sh"
if run_generic_ros2 "$fixture/bad" --help; then exit 1; fi
if run_generic_ros2 "$fixture/missing" --help; then exit 1; fi
if run_generic_ros2 "$fixture/valid" doctor; then exit 1; fi
echo 'GENERIC_GLOBAL_LAUNCHER_CONTRACT=PASS defaults,cyclone,reject-mdds,fail-closed'
