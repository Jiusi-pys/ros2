#!/bin/sh

set -eu

if [ "$#" -ne 4 ]; then
  echo "Usage: $0 <prefix> <domain_id> <container_log> <cli_log>" >&2
  exit 1
fi

PREFIX="$1"
DOMAIN_ID="$2"
CONTAINER_LOG="$3"
CLI_LOG="$4"

VENDOR_LIB_PATH=
for dir in "${PREFIX}"/opt/*/lib; do
  if [ -d "${dir}" ]; then
    VENDOR_LIB_PATH="${VENDOR_LIB_PATH:+${VENDOR_LIB_PATH}:}${dir}"
  fi
done
export LD_LIBRARY_PATH="${PREFIX}/lib${VENDOR_LIB_PATH:+:${VENDOR_LIB_PATH}}:/data/local/tmp:/data/local/release/usr/lib"
export PYTHONPATH="${PREFIX}/lib/python3.12/site-packages:${PREFIX}/lib/python3.11/site-packages"
export AMENT_PREFIX_PATH="${PREFIX}"
export LD_PRELOAD="/data/local/release/usr/lib/libpython3.12.so.1.0"
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET
export ROS_DOMAIN_ID="${DOMAIN_ID}"

rm -f "${CONTAINER_LOG}" "${CLI_LOG}"

"${PREFIX}/lib/rclcpp_components/component_container" >"${CONTAINER_LOG}" 2>&1 &
CONTAINER_PID=$!
sleep 2

set +e
{
  "${PREFIX}/bin/ros2" component load /ComponentManager composition composition::Talker
  "${PREFIX}/bin/ros2" component load /ComponentManager composition composition::Listener
  sleep 3
  "${PREFIX}/bin/ros2" component list /ComponentManager
} >"${CLI_LOG}" 2>&1
CLI_STATUS=$?
set -e

sleep 1
kill "${CONTAINER_PID}" >/dev/null 2>&1 || true
wait "${CONTAINER_PID}" >/dev/null 2>&1 || true

echo "---CLI---"
cat "${CLI_LOG}"
echo "---CONTAINER---"
cat "${CONTAINER_LOG}"

if grep -q "Loaded component" "${CLI_LOG}" && \
   grep -q "composition::Talker" "${CLI_LOG}" && \
   grep -q "composition::Listener" "${CLI_LOG}" && \
   grep -q "Publishing:" "${CONTAINER_LOG}" && \
   grep -q "I heard:" "${CONTAINER_LOG}"; then
  exit 0
fi

exit "${CLI_STATUS}"
