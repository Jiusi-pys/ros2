#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
eval "$(sed -n '/^owned_env() {/,/^}/p' scripts/run_ohos_generic_acceptance.sh)"
DEVICE_DIR=/data/local/tmp/ros2-generic
ROS2_RUN_ID=rmw_contract
DOMAIN=201
declare -A BOARD_PRODUCT=([A]=KaihongOS) BOARD_VERSION=([A]=6.1) BOARD_ARCH=([A]=aarch64)
DEPLOYED_DEFAULT_RMW=rmw_fastrtps_cpp
EXPECTED_RMW=rmw_cyclonedds_cpp
value="$(owned_env A compiled-default)"
[[ "$value" == *"export RMW_IMPLEMENTATION='rmw_cyclonedds_cpp'"* ]]
[[ "$value" == *'REQUEST_MODE=explicit'* ]]
[[ "$value" != *'unset RMW_IMPLEMENTATION'* ]]
EXPECTED_RMW=rmw_fastrtps_cpp
value="$(owned_env A compiled-default)"
[[ "$value" == *'REQUEST_MODE=compiled-default'* ]]
[[ "$value" == *'unset RMW_IMPLEMENTATION FASTDDS_BUILTIN_TRANSPORTS'* ]]
value="$(owned_env A explicit)"
[[ "$value" == *"export RMW_IMPLEMENTATION='rmw_fastrtps_cpp'"* ]]
[[ "$value" != *'unset RMW_IMPLEMENTATION'* ]]
grep -Fq 'RMW_ACTIVE' scripts/run_ohos_generic_acceptance.sh
if grep -Fq "tr -d ' " scripts/run_ohos_generic_acceptance.sh; then
  echo 'ERROR: board identity still requires the absent tr command' >&2
  exit 1
fi
echo 'ACCEPTANCE_RMW_SELECTION=PASS explicit-cyclone,compiled-fastdds,explicit-fastdds'
