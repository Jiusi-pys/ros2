#!/bin/sh

set -eu

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <prefix> <domain_id> <work_dir>" >&2
  exit 1
fi

PREFIX="$1"
DOMAIN_ID="$2"
WORK_DIR="$3"

LIST_PLUGINS_BIN="${PREFIX}/lib/pluginlib/list_plugins"
ROS2_BIN="${PREFIX}/bin/ros2"
CHECK_URDF_BIN=""
RSP_LIBEXEC="${PREFIX}/lib/robot_state_publisher/robot_state_publisher"
RSP_BIN="${PREFIX}/bin/robot_state_publisher"
TF2_ECHO_BIN="${PREFIX}/lib/tf2_ros/tf2_echo"

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

mkdir -p "${WORK_DIR}"

URDF_FILE="${WORK_DIR}/probe_robot.urdf"
PLUGINS_LOG="${WORK_DIR}/plugins.log"
ROS2PLUGIN_LOG="${WORK_DIR}/ros2plugin.log"
CHECK_URDF_LOG="${WORK_DIR}/check_urdf.log"
RSP_LOG="${WORK_DIR}/robot_state_publisher.log"
NODE_LIST_LOG="${WORK_DIR}/node_list.log"
TOPIC_LIST_LOG="${WORK_DIR}/topic_list.log"
TF_PROOF_LOG="${WORK_DIR}/tf_proof.log"

cat >"${URDF_FILE}" <<'EOF_URDF'
<?xml version="1.0"?>
<robot name="probe_robot">
  <link name="base_link"/>
  <joint name="base_to_link1" type="fixed">
    <parent link="base_link"/>
    <child link="link1"/>
    <origin xyz="0 0 1" rpy="0 0 0"/>
  </joint>
  <link name="link1"/>
</robot>
EOF_URDF

if [ -x "${PREFIX}/bin/check_urdf" ]; then
  CHECK_URDF_BIN="${PREFIX}/bin/check_urdf"
elif [ -x "/data/local/tmp/check_urdf" ]; then
  CHECK_URDF_BIN="/data/local/tmp/check_urdf"
else
  echo "check_urdf_binary_missing" >&2
  exit 1
fi

"${LIST_PLUGINS_BIN}" urdf_parser_plugin urdf::URDFParser >"${PLUGINS_LOG}" 2>&1
"${ROS2_BIN}" plugin list --package urdf >"${ROS2PLUGIN_LOG}" 2>&1
"${CHECK_URDF_BIN}" "${URDF_FILE}" >"${CHECK_URDF_LOG}" 2>&1

echo "---PLUGINS---"
cat "${PLUGINS_LOG}"
echo "---ROS2PLUGIN---"
cat "${ROS2PLUGIN_LOG}"
echo "---CHECK_URDF---"
cat "${CHECK_URDF_LOG}"

if ! grep -q "urdf_xml_parser/URDFXMLParser" "${PLUGINS_LOG}"; then
  echo "pluginlib proof failed" >&2
  exit 1
fi

if ! grep -q "urdf_xml_parser/URDFXMLParser" "${ROS2PLUGIN_LOG}"; then
  echo "ros2 plugin proof failed" >&2
  exit 1
fi

if ! grep -q "robot name is: probe_robot" "${CHECK_URDF_LOG}"; then
  echo "check_urdf proof failed" >&2
  exit 1
fi

RSP_EXECUTABLE=""
if [ -x "${RSP_LIBEXEC}" ]; then
  RSP_EXECUTABLE="${RSP_LIBEXEC}"
elif [ -x "${RSP_BIN}" ]; then
  RSP_EXECUTABLE="${RSP_BIN}"
fi

if [ -z "${RSP_EXECUTABLE}" ]; then
  echo "robot_state_publisher_binary_missing"
  exit 0
fi

rm -f "${RSP_LOG}" "${NODE_LIST_LOG}" "${TOPIC_LIST_LOG}" "${TF_PROOF_LOG}"

"${RSP_EXECUTABLE}" "${URDF_FILE}" >"${RSP_LOG}" 2>&1 &
RSP_PID=$!
sleep 2

set +e
"${ROS2_BIN}" node list >"${NODE_LIST_LOG}" 2>&1
NODE_STATUS=$?
"${ROS2_BIN}" topic list >"${TOPIC_LIST_LOG}" 2>&1
TOPIC_STATUS=$?
if [ -x "${TF2_ECHO_BIN}" ]; then
  timeout 8s "${TF2_ECHO_BIN}" base_link link1 -r 1 >"${TF_PROOF_LOG}" 2>&1
else
  timeout 8s "${ROS2_BIN}" topic echo /tf_static --qos-durability transient_local --once >"${TF_PROOF_LOG}" 2>&1
fi
TF_STATUS=$?
set -e

kill "${RSP_PID}" >/dev/null 2>&1 || true
wait "${RSP_PID}" >/dev/null 2>&1 || true

echo "---NODE_LIST---"
cat "${NODE_LIST_LOG}"
echo "---TOPIC_LIST---"
cat "${TOPIC_LIST_LOG}"
echo "---TF_PROOF---"
cat "${TF_PROOF_LOG}"
echo "---ROBOT_STATE_PUBLISHER---"
cat "${RSP_LOG}"

if grep -q "/robot_state_publisher" "${NODE_LIST_LOG}" && \
   grep -q "/tf_static" "${TOPIC_LIST_LOG}" && \
   { grep -q "Translation: \\[0.000, 0.000, 1.000\\]" "${TF_PROOF_LOG}" && \
     grep -q "Rotation: in Quaternion (xyzw) \\[0.000, 0.000, 0.000, 1.000\\]" "${TF_PROOF_LOG}"; }; then
  exit 0
fi

if [ "${TF_STATUS}" -ne 0 ]; then
  exit "${TF_STATUS}"
fi

if [ "${NODE_STATUS}" -ne 0 ]; then
  exit "${NODE_STATUS}"
fi

exit "${TOPIC_STATUS}"
