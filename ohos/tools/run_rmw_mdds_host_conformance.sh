#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
Usage: run_rmw_mdds_host_conformance.sh [--package-only|--upstream-only]

Runs host-side rmw_mdds_cpp conformance tests with the ROS 2 install
environment sourced so ament_cmake_test is importable by CTest wrappers.
EOF
}

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="all"

if [[ $# -gt 1 ]]; then
  usage
  exit 2
fi
if [[ $# -eq 1 ]]; then
  case "$1" in
    --package-only) MODE="package" ;;
    --upstream-only) MODE="upstream" ;;
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

require_path() {
  local path="$1"
  [[ -e "${path}" ]] || {
    echo "missing required path: ${path}" >&2
    exit 1
  }
}

require_path "${ROOT_DIR}/install/setup.bash"
require_path "${ROOT_DIR}/build/rmw_mdds_cpp"
require_path "${ROOT_DIR}/build/test_rmw_implementation"

set +u
source "${ROOT_DIR}/install/setup.bash"
set -u
export LD_LIBRARY_PATH="${ROOT_DIR}/build/rmw_mdds_cpp:${LD_LIBRARY_PATH:-}"
export RMW_IMPLEMENTATION=rmw_mdds_cpp

if [[ "${MODE}" == "all" || "${MODE}" == "package" ]]; then
  RMW_IMPLEMENTATION=rmw_mdds_cpp ctest --test-dir "${ROOT_DIR}/build/rmw_mdds_cpp" --output-on-failure
  echo "RESULT|rmw_mdds_host_conformance|PASS|package"
fi

if [[ "${MODE}" == "all" || "${MODE}" == "upstream" ]]; then
  RMW_IMPLEMENTATION=rmw_mdds_cpp ctest --test-dir "${ROOT_DIR}/build/test_rmw_implementation" \
    -R __rmw_mdds_cpp$ --output-on-failure
  echo "RESULT|rmw_mdds_host_conformance|PASS|upstream"
fi

echo "rmw_mdds_host_conformance_ok"
