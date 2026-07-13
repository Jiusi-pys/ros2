#!/system/bin/sh

set -u

MODE=${1:-}
DOMAIN=${2:-}
WORK=${3:-}
PREFIX=${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}
UNDERLAY=${ROS2_OHOS_UNDERLAY_PREFIX:-/data/local/tmp/ohos-prefix}
FASTDDS=${ROS2_OHOS_FASTDDS_PREFIX:-/data/local/tmp/ohos-fastdds}
BRIDGE=${RMW_MDDS_BRIDGE_LIBRARY:-/data/local/tmp/libmdds_bridge_shared.z.so}
PROBE=${PREFIX}/lib/rmw_mdds_dynamic_loan_probe/rmw_mdds_broker_dynamic_loan_probe
BROKER=${PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker

case "${MODE}" in
  --self-test|--remote-publisher|--remote-subscriber) ;;
  *)
    echo "usage: $0 <--self-test|--remote-publisher|--remote-subscriber> <domain> <work-dir>" >&2
    exit 2
    ;;
esac
if [ -z "${DOMAIN}" ] || [ -z "${WORK}" ]; then
  echo "usage: $0 <--self-test|--remote-publisher|--remote-subscriber> <domain> <work-dir>" >&2
  exit 2
fi
if [ ! -x "${PROBE}" ] || [ ! -x "${BROKER}" ]; then
  echo "RESULT|rmw_mdds_broker_dynamic_loan_board_runner|FAIL|stage=artifacts"
  exit 1
fi
if [ "${MODE}" != "--self-test" ] && [ ! -f "${BRIDGE}" ]; then
  echo "RESULT|rmw_mdds_broker_dynamic_loan_board_runner|FAIL|stage=bridge"
  exit 1
fi

SOCKET=${WORK}/broker.sock
LOG=${WORK}/probe.log
RESULT=${WORK}/result.txt
DONE=${WORK}/done
RC_FILE=${WORK}/rc

stop_pids_gracefully()
{
  pids="$*"
  [ -n "${pids}" ] || return 0
  for pid in ${pids}; do
    kill "${pid}" 2>/dev/null || true
  done
  for attempt in 1 2 3 4 5; do
    alive=0
    for pid in ${pids}; do
      if kill -0 "${pid}" 2>/dev/null; then
        alive=1
      fi
    done
    [ "${alive}" -eq 0 ] && break
    sleep 1
  done
  for pid in ${pids}; do
    if kill -0 "${pid}" 2>/dev/null; then
      kill -9 "${pid}" 2>/dev/null || true
    fi
  done
}

kill_broker()
{
  pids=$(
    ps -ef 2>/dev/null |
      grep "[r]mw_mdds_broker --socket ${SOCKET}" |
      sed -E 's/^ *[^ ]+ +([0-9]+).*/\1/'
  )
  # Graceful broker shutdown releases bridge-owned DSoftBus sockets first.
  stop_pids_gracefully ${pids}
}

kill_all_brokers()
{
  pids=$(
    ps -ef 2>/dev/null |
      grep '[r]mw_mdds_broker --socket' |
      sed -E 's/^ *[^ ]+ +([0-9]+).*/\1/'
  )
  stop_pids_gracefully ${pids}
}

kill_all_brokers
kill_broker
rm -rf "${WORK}"
mkdir -p "${WORK}" "${WORK}/home" "${WORK}/roslogs"
rm -f "${SOCKET}"
rm -rf "${SOCKET}.lock"

unset LD_PRELOAD
export HOME=${WORK}/home
export ROS_LOG_DIR=${WORK}/roslogs
export ROS_DISTRO=jazzy
export LD_LIBRARY_PATH=${PREFIX}/lib:${PREFIX}/lib/rmw_mdds_cpp:${UNDERLAY}/lib:${FASTDDS}/lib:/data/local/tmp:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64
export AMENT_PREFIX_PATH=${PREFIX}:${UNDERLAY}
export CMAKE_PREFIX_PATH=${PREFIX}:${UNDERLAY}:${FASTDDS}
export COLCON_PREFIX_PATH=${PREFIX}:${UNDERLAY}
export RMW_IMPLEMENTATION=rmw_mdds_cpp
export RMW_MDDS_BROKER=1
export RMW_MDDS_BROKER_EXECUTABLE=${BROKER}
export RMW_MDDS_BROKER_SOCKET=${SOCKET}
export RMW_MDDS_BROKER_LOG=${WORK}/broker.log
export RMW_MDDS_NODE_SYNC_TOPIC=/rmw_mdds_dynamic_graph_${DOMAIN}
export ROS_DOMAIN_ID=${DOMAIN}
if [ "${MODE}" = "--self-test" ]; then
  export RMW_MDDS_BRIDGE=0
  unset RMW_MDDS_BRIDGE_LIBRARY
else
  export RMW_MDDS_BRIDGE=1
  export RMW_MDDS_BRIDGE_LIBRARY=${BRIDGE}
fi

timeout 180 "${PROBE}" "${MODE}" >"${LOG}" 2>&1
rc=$?
kill_broker
pool_count=$(find "${WORK}" -name 'rmw_mdds_loan_*' 2>/dev/null | wc -l)
if [ "${pool_count}" -ne 0 ]; then
  rc=1
fi

rmw_sha=$(sha256sum "${PREFIX}/lib/librmw_mdds_cpp.so" | cut -d ' ' -f 1)
broker_sha=$(sha256sum "${BROKER}" | cut -d ' ' -f 1)
probe_sha=$(sha256sum "${PROBE}" | cut -d ' ' -f 1)
status=FAIL
if [ "${rc}" -eq 0 ]; then
  status=PASS
fi
{
  cat "${LOG}"
  echo "RESULT|rmw_mdds_broker_dynamic_loan_board_runner|${status}|mode=${MODE}|domain=${DOMAIN}|rc=${rc}|pool_count=${pool_count}|rmw_sha=${rmw_sha}|broker_sha=${broker_sha}|probe_sha=${probe_sha}"
  echo "BOARD_RC=${rc}"
} >"${RESULT}"
echo "${rc}" >"${RC_FILE}"
touch "${DONE}"
cat "${RESULT}"
exit "${rc}"
