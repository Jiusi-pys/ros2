#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_type_description_probe.sh

Starts a host demo_nodes_cpp talker under rmw_mdds_cpp and verifies the
standard RCL ~/get_type_description service returns std_msgs/msg/String.
EOF
}

if [[ $# -gt 0 ]]; then
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage
      exit 2
      ;;
  esac
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DESCRIPTION_C="${ROOT_DIR}/build/std_msgs/rosidl_generator_c/std_msgs/msg/detail/string__description.c"
LOG_DIR="${TMPDIR:-/tmp}/rmw_mdds_type_description_probe_logs"
TALKER_LOG="${TMPDIR:-/tmp}/rmw_mdds_type_description_talker.log"
DOMAIN_ID="${RMW_MDDS_TYPE_DESCRIPTION_DOMAIN_ID:-$((($$ % 80) + 140))}"

require_path() {
  local path="$1"
  [[ -e "${path}" ]] || {
    echo "missing required path: ${path}" >&2
    exit 1
  }
}

type_hash_from_description() {
  python3 - "${DESCRIPTION_C}" <<'PY'
import re
import sys

path = sys.argv[1]
text = open(path, "r", encoding="utf-8").read()
match = re.search(r"static rosidl_type_hash_t hash = \{1, \{(?P<body>.*?)\}\};", text, re.S)
if not match:
    raise SystemExit("failed to locate std_msgs/msg/String type hash")
values = re.findall(r"0x([0-9a-fA-F]{2})", match.group("body"))
if len(values) != 32:
    raise SystemExit(f"expected 32 hash bytes, found {len(values)}")
print("RIHS01_" + "".join(value.lower() for value in values))
PY
}

require_path "${ROOT_DIR}/install/setup.bash"
require_path "${DESCRIPTION_C}"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u

export RMW_IMPLEMENTATION=rmw_mdds_cpp
export ROS_DOMAIN_ID="${DOMAIN_ID}"
export ROS_LOG_DIR="${LOG_DIR}"
mkdir -p "${ROS_LOG_DIR}"

type_hash="$(type_hash_from_description)"

timeout 30s ros2 run demo_nodes_cpp talker >"${TALKER_LOG}" 2>&1 &
talker_pid=$!

cleanup() {
  kill "${talker_pid}" >/dev/null 2>&1 || true
  wait "${talker_pid}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

service_ready=0
for _ in $(seq 1 60); do
  if ros2 service list 2>/dev/null | grep -qx '/talker/get_type_description'; then
    service_ready=1
    break
  fi
  sleep 0.25
done

if [[ "${service_ready}" != "1" ]]; then
  echo "type description service did not appear" >&2
  sed -n '1,120p' "${TALKER_LOG}" >&2 || true
  exit 1
fi

response="$(
  timeout 15s ros2 service call /talker/get_type_description \
    type_description_interfaces/srv/GetTypeDescription \
    "{type_name: 'std_msgs/msg/String', type_hash: '${type_hash}', include_type_sources: false}" 2>&1
)"

grep -q "successful=True" <<<"${response}" || {
  echo "${response}" >&2
  exit 1
}
grep -q "type_name='std_msgs/msg/String'" <<<"${response}" || {
  echo "${response}" >&2
  exit 1
}

echo "RESULT|rmw_mdds_type_description|PASS|std_msgs/msg/String"
echo "rmw_mdds_type_description_probe_ok"
