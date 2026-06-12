#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <prefix> <domain_id> <output_log>" >&2
  exit 1
fi

PREFIX="$1"
DOMAIN_ID="$2"
OUTPUT_LOG="$3"

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

rm -f "${OUTPUT_LOG}"

set +e
timeout 8s "${PREFIX}/lib/composition/dlopen_composition" \
  "${PREFIX}/lib/libtalker_component.so" \
  "${PREFIX}/lib/liblistener_component.so" \
  >"${OUTPUT_LOG}" 2>&1
STATUS=$?
set -e

cat "${OUTPUT_LOG}"

if grep -q "Load library" "${OUTPUT_LOG}" && \
   grep -q "Instantiate class" "${OUTPUT_LOG}" && \
   grep -q "Publishing:" "${OUTPUT_LOG}" && \
   grep -q "I heard:" "${OUTPUT_LOG}"; then
  exit 0
fi

exit "${STATUS}"
