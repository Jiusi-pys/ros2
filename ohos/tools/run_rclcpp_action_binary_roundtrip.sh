#!/bin/sh

set -eu

if [ "$#" -ne 4 ]; then
  echo "Usage: $0 <prefix> <domain_id> <server_log> <client_log>" >&2
  exit 1
fi

PREFIX="$1"
DOMAIN_ID="$2"
SERVER_LOG="$3"
CLIENT_LOG="$4"

VENDOR_LIB_PATH=
for dir in "${PREFIX}"/opt/*/lib; do
  if [ -d "${dir}" ]; then
    VENDOR_LIB_PATH="${VENDOR_LIB_PATH:+${VENDOR_LIB_PATH}:}${dir}"
  fi
done
export LD_LIBRARY_PATH="${PREFIX}/lib${VENDOR_LIB_PATH:+:${VENDOR_LIB_PATH}}:/data/local/tmp:/data/local/release/usr/lib"
export AMENT_PREFIX_PATH="${PREFIX}"
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
export ROS_DOMAIN_ID="${DOMAIN_ID}"

rm -f "${SERVER_LOG}" "${CLIENT_LOG}"

"${PREFIX}/lib/examples_rclcpp_minimal_action_server/action_server_not_composable" >"${SERVER_LOG}" 2>&1 &
SERVER_PID=$!
sleep 2

set +e
"${PREFIX}/lib/examples_rclcpp_minimal_action_client/action_client_not_composable" >"${CLIENT_LOG}" 2>&1
CLIENT_STATUS=$?
set -e

sleep 1
kill "${SERVER_PID}" >/dev/null 2>&1 || true
wait "${SERVER_PID}" >/dev/null 2>&1 || true

echo "---CLIENT---"
cat "${CLIENT_LOG}"
echo "---SERVER---"
cat "${SERVER_LOG}"

exit "${CLIENT_STATUS}"
