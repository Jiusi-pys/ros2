#!/bin/sh

set -eu

if [ "$#" -ne 4 ]; then
  echo "Usage: $0 <prefix> <domain_id> <service_log> <client_log>" >&2
  exit 1
fi

PREFIX="$1"
DOMAIN_ID="$2"
SERVICE_LOG="$3"
CLIENT_LOG="$4"

VENDOR_LIB_PATH=
for dir in "${PREFIX}"/opt/*/lib; do
  if [ -d "${dir}" ]; then
    VENDOR_LIB_PATH="${VENDOR_LIB_PATH:+${VENDOR_LIB_PATH}:}${dir}"
  fi
done
export LD_LIBRARY_PATH="${PREFIX}/lib${VENDOR_LIB_PATH:+:${VENDOR_LIB_PATH}}:/data/local/tmp:/data/local/release/usr/lib"
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
export ROS_DOMAIN_ID="${DOMAIN_ID}"

rm -f "${SERVICE_LOG}" "${CLIENT_LOG}"

"${PREFIX}/lib/examples_rclcpp_minimal_service/service_main" >"${SERVICE_LOG}" 2>&1 &
SERVER_PID=$!
sleep 2

set +e
"${PREFIX}/lib/examples_rclcpp_minimal_client/client_main" >"${CLIENT_LOG}" 2>&1
CLIENT_STATUS=$?
set -e

sleep 1
kill "${SERVER_PID}" >/dev/null 2>&1 || true
wait "${SERVER_PID}" >/dev/null 2>&1 || true

echo "---CLIENT---"
cat "${CLIENT_LOG}"
echo "---SERVER---"
cat "${SERVICE_LOG}"

exit "${CLIENT_STATUS}"
