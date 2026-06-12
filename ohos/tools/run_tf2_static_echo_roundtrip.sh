#!/bin/sh

set -eu

if [ "$#" -ne 4 ]; then
  echo "Usage: $0 <prefix> <domain_id> <publisher_log> <echo_log>" >&2
  exit 1
fi

PREFIX="$1"
DOMAIN_ID="$2"
PUBLISHER_LOG="$3"
ECHO_LOG="$4"

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

rm -f "${PUBLISHER_LOG}" "${ECHO_LOG}"

"${PREFIX}/lib/tf2_ros/static_transform_publisher" \
  --x 1 --y 2 --z 3 --yaw 0.5 \
  --frame-id map --child-frame-id laser \
  >"${PUBLISHER_LOG}" 2>&1 &
PUBLISHER_PID=$!
sleep 2

set +e
timeout 8s "${PREFIX}/lib/tf2_ros/tf2_echo" map laser -r 2 >"${ECHO_LOG}" 2>&1
ECHO_STATUS=$?
set -e

sleep 1
kill "${PUBLISHER_PID}" >/dev/null 2>&1 || true
wait "${PUBLISHER_PID}" >/dev/null 2>&1 || true

echo "---ECHO---"
cat "${ECHO_LOG}"
echo "---PUBLISHER---"
cat "${PUBLISHER_LOG}"

if grep -q "At time" "${ECHO_LOG}" && grep -q "Translation:" "${ECHO_LOG}"; then
  exit 0
fi

exit "${ECHO_STATUS}"
