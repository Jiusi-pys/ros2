#!/system/bin/sh
# codex-file-meta: begin
# relative_path: "ohos/tools/rmw_mdds_test_rmw_board_runner.sh"
# language: "shell"
# summary: "Runs the current AArch64 test_rmw_implementation programs sequentially on an RK3588A board."
# symbols: ["kill_mdds_brokers", "cleanup_broker_state"]
# generated_by: "codex"
# codex-file-meta: end

set -u

TEST_ROOT="${RMW_MDDS_TEST_ROOT:-/data/local/tmp/rmw_mdds_test_rmw_current}"
PREFIX="${ROS2_OHOS_REMOTE_PREFIX:-/data/local/tmp/ohos-colcon-rk3588a}"
UNDERLAY_PREFIX="${ROS2_OHOS_UNDERLAY_PREFIX:-/data/local/tmp/ohos-prefix}"
FASTDDS_PREFIX="${ROS2_OHOS_FASTDDS_PREFIX:-/data/local/tmp/ohos-fastdds}"
RESULT_DIR="${TEST_ROOT}/results"
TIMEOUT_SECONDS="${RMW_MDDS_TEST_TIMEOUT_SECONDS:-240}"
DOMAIN_BASE="${RMW_MDDS_TEST_DOMAIN_BASE:-201}"
BRIDGE_LIBRARY="${RMW_MDDS_BRIDGE_LIBRARY:-${PREFIX}/lib/libmdds_bridge_shared.z.so}"
BROKER_BIN="${PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker"
SUITE_BROKER_SOCKET="${RESULT_DIR}/suite.sock"
SUITE_BROKER_LOG="${RESULT_DIR}/suite.broker.log"
SUITE_BROKER_PID=""
UPSTREAM_EXPECTED_PROGRAMS=16
UPSTREAM_EXPECTED_ASSERTIONS=129
UPSTREAM_EXPECTED_SKIPS=6
DIRECT_LOAN_FILTER="TestSubscriptionUseLoan.*"
DIRECT_LOAN_LOG="${RESULT_DIR}/test_subscription.direct_loan.log"
DIRECT_LOAN_EXPECTED_PASS=3
DIRECT_LOAN_EXPECTED_SKIP=0
CAPABILITY_FILTER="RmwMddsPubSub.InitializesPublisherAndSubscriptionAllocations:RmwMddsPubSub.AllocationInitRejectsUnsupportedTypeSupport:RmwMddsPubSub.SerializedSizeReports*"
CAPABILITY_LOG="${RESULT_DIR}/test_pubsub_inproc.capability.log"
CAPABILITY_EXPECTED_PASS=5
CAPABILITY_EXPECTED_SKIP=0

TESTS="
test_client
test_create_destroy_node
test_duration_infinite
test_event
test_graph_api
test_init_options
test_init_shutdown
test_publisher
test_publisher_allocator
test_qos_profile_check_compatible
test_serialize_deserialize
test_service
test_subscription
test_subscription_allocator
test_unique_identifiers
test_wait_set
"

stop_pids_gracefully() {
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

kill_mdds_brokers() {
  # Graceful broker shutdown lets the MDDS bridge release DSoftBus sockets.
  pids=$(
    ps -ef 2>/dev/null |
      grep '[r]mw_mdds_broker' |
      sed -E 's/^ *[^ ]+ +([0-9]+).*/\1/'
  )
  stop_pids_gracefully ${pids}
  sleep 1
}

stop_suite_broker() {
  if [ -n "${SUITE_BROKER_PID}" ]; then
    stop_pids_gracefully "${SUITE_BROKER_PID}"
    wait "${SUITE_BROKER_PID}" 2>/dev/null || true
    SUITE_BROKER_PID=""
  fi
  rm -f "${SUITE_BROKER_SOCKET}" "${SUITE_BROKER_SOCKET}.autostart.lock" \
    "${SUITE_BROKER_SOCKET}.listener.lock" 2>/dev/null || true
  rm -rf "${SUITE_BROKER_SOCKET}.lock" "${SUITE_BROKER_SOCKET}.autostart.lockdir" \
    "${SUITE_BROKER_SOCKET}.listener.lockdir" 2>/dev/null || true
}

start_suite_broker() {
  stop_suite_broker
  rm -f "${SUITE_BROKER_LOG}"
  nohup env ROS_DOMAIN_ID="${DOMAIN_BASE}" RMW_MDDS_BRIDGE_LIBRARY="${BRIDGE_LIBRARY}" \
    "${BROKER_BIN}" --socket "${SUITE_BROKER_SOCKET}" >"${SUITE_BROKER_LOG}" 2>&1 &
  SUITE_BROKER_PID=$!
  for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    if ! kill -0 "${SUITE_BROKER_PID}" 2>/dev/null; then
      cat "${SUITE_BROKER_LOG}" 2>/dev/null || true
      return 1
    fi
    if [ -e "${SUITE_BROKER_SOCKET}" ]; then
      return 0
    fi
    sleep 1
  done
  cat "${SUITE_BROKER_LOG}" 2>/dev/null || true
  stop_suite_broker
  return 1
}

cleanup_broker_state() {
  rm -f /data/local/tmp/rmw_mdds_cpp.sock "${RESULT_DIR}"/*.sock 2>/dev/null || true
  rm -rf /data/local/tmp/rmw_mdds_cpp.sock.lock "${RESULT_DIR}"/*.sock.lock 2>/dev/null || true
}

if [ ! -d "${PREFIX}/lib" ] || [ ! -x "${BROKER_BIN}" ]; then
  echo "RESULT|rmw_mdds_test_rmw_board|FAIL|stage=runtime_prefix"
  echo "BOARD_RC=1"
  exit 1
fi
if [ ! -f "${BRIDGE_LIBRARY}" ]; then
  echo "RESULT|rmw_mdds_test_rmw_board|FAIL|stage=bridge_library"
  echo "BOARD_RC=1"
  exit 1
fi

rm -rf "${RESULT_DIR}"
mkdir -p "${RESULT_DIR}" "${TEST_ROOT}/home" "${TEST_ROOT}/roslogs"

VENDOR_LIB_PATH=""
for dir in "${PREFIX}"/opt/*/lib; do
  if [ -d "${dir}" ]; then
    VENDOR_LIB_PATH="${VENDOR_LIB_PATH:+${VENDOR_LIB_PATH}:}${dir}"
  fi
done
UNDERLAY_VENDOR_LIB_PATH=""
for dir in "${UNDERLAY_PREFIX}"/opt/*/lib; do
  if [ -d "${dir}" ]; then
    UNDERLAY_VENDOR_LIB_PATH="${UNDERLAY_VENDOR_LIB_PATH:+${UNDERLAY_VENDOR_LIB_PATH}:}${dir}"
  fi
done

unset LD_PRELOAD
export HOME="${TEST_ROOT}/home"
export ROS_LOG_DIR="${TEST_ROOT}/roslogs"
export ROS_DISTRO=jazzy
export LD_LIBRARY_PATH="${TEST_ROOT}/lib:${PREFIX}/lib:${PREFIX}/lib/rmw_mdds_cpp:${UNDERLAY_PREFIX}/lib:${FASTDDS_PREFIX}/lib${VENDOR_LIB_PATH:+:${VENDOR_LIB_PATH}}${UNDERLAY_VENDOR_LIB_PATH:+:${UNDERLAY_VENDOR_LIB_PATH}}:/data/local/tmp:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64"
export AMENT_PREFIX_PATH="${PREFIX}:${UNDERLAY_PREFIX}"
export CMAKE_PREFIX_PATH="${PREFIX}:${UNDERLAY_PREFIX}:${FASTDDS_PREFIX}"
export COLCON_PREFIX_PATH="${PREFIX}:${UNDERLAY_PREFIX}"
export RMW_IMPLEMENTATION=rmw_mdds_cpp
export RMW_MDDS_BROKER=1
export RMW_MDDS_BROKER_EXECUTABLE="${PREFIX}/lib/rmw_mdds_cpp/rmw_mdds_broker"
export RMW_MDDS_BRIDGE_LIBRARY="${BRIDGE_LIBRARY}"

kill_mdds_brokers
cleanup_broker_state
if ! start_suite_broker; then
  echo "RESULT|rmw_mdds_test_rmw_board|FAIL|stage=suite_broker_start"
  echo "BOARD_RC=1"
  exit 1
fi
trap 'stop_suite_broker; cleanup_broker_state' EXIT

pass=0
fail=0
total=0
skipped_tests=0
passed_tests=0
index=0

for test_name in ${TESTS}; do
  total=$((total + 1))
  index=$((index + 1))
  test_binary="${TEST_ROOT}/bin/${test_name}"
  test_log="${RESULT_DIR}/${test_name}.log"
  export ROS_DOMAIN_ID=$((DOMAIN_BASE + index))
  export RMW_MDDS_BROKER_SOCKET="${SUITE_BROKER_SOCKET}"
  export RMW_MDDS_BROKER_LOG="${SUITE_BROKER_LOG}"

  if ! kill -0 "${SUITE_BROKER_PID}" 2>/dev/null; then
    echo "TEST_RMW_CASE|${test_name}|FAIL|rc=125|reason=suite_broker_stopped"
    fail=$((fail + 1))
    continue
  fi

  if [ ! -x "${test_binary}" ]; then
    echo "TEST_RMW_CASE|${test_name}|FAIL|rc=127|reason=missing_binary"
    fail=$((fail + 1))
    continue
  fi

  timeout "${TIMEOUT_SECONDS}" "${test_binary}" --gtest_color=no >"${test_log}" 2>&1
  test_rc=$?
  passed_count="$(sed -n -E 's/^\[  PASSED  \] ([0-9]+) tests?\..*/\1/p' "${test_log}" | tail -n 1)"
  skipped_count="$(sed -n -E 's/^\[  SKIPPED \] ([0-9]+) tests?.*/\1/p' "${test_log}" | head -n 1)"
  passed_count="${passed_count:-0}"
  skipped_count="${skipped_count:-0}"
  skipped_tests=$((skipped_tests + skipped_count))
  passed_tests=$((passed_tests + passed_count))

  if [ "${test_rc}" -eq 0 ] && [ $((passed_count + skipped_count)) -gt 0 ]; then
    disposition=PASS
    if [ "${passed_count}" -eq 0 ]; then
      disposition=SKIP_ONLY
    elif [ "${skipped_count}" -gt 0 ]; then
      disposition=PASS_WITH_SKIPS
    fi
    echo "TEST_RMW_CASE|${test_name}|${disposition}|rc=0|passed=${passed_count}|skipped=${skipped_count}"
    pass=$((pass + 1))
  else
    echo "TEST_RMW_CASE|${test_name}|FAIL|rc=${test_rc}|passed=${passed_count}|skipped=${skipped_count}"
    tail -n 80 "${test_log}"
    fail=$((fail + 1))
  fi
done

stop_suite_broker
cleanup_broker_state
trap - EXIT

direct_loan_pass=0
direct_loan_skip=0
direct_loan_rc=127
direct_loan_ok=0
test_subscription_binary="${TEST_ROOT}/bin/test_subscription"
if [ -x "${test_subscription_binary}" ]; then
  kill_mdds_brokers
  cleanup_broker_state
  export ROS_DOMAIN_ID=$((DOMAIN_BASE + 17))
  export RMW_MDDS_BROKER=0
  unset RMW_MDDS_BROKER_SOCKET
  unset RMW_MDDS_BROKER_LOG
  timeout "${TIMEOUT_SECONDS}" "${test_subscription_binary}" \
    --gtest_color=no --gtest_filter="${DIRECT_LOAN_FILTER}" \
    >"${DIRECT_LOAN_LOG}" 2>&1
  direct_loan_rc=$?
  direct_loan_pass="$(
    sed -n -E 's/^\[  PASSED  \] ([0-9]+) tests?\..*/\1/p' \
      "${DIRECT_LOAN_LOG}" | tail -n 1
  )"
  direct_loan_skip="$(
    sed -n -E 's/^\[  SKIPPED \] ([0-9]+) tests?.*/\1/p' \
      "${DIRECT_LOAN_LOG}" | head -n 1
  )"
  direct_loan_pass="${direct_loan_pass:-0}"
  direct_loan_skip="${direct_loan_skip:-0}"
  if [ "${direct_loan_rc}" -eq 0 ] && \
      [ "${direct_loan_pass}" -eq "${DIRECT_LOAN_EXPECTED_PASS}" ] && \
      [ "${direct_loan_skip}" -eq "${DIRECT_LOAN_EXPECTED_SKIP}" ]; then
    direct_loan_ok=1
  else
    tail -n 80 "${DIRECT_LOAN_LOG}" 2>/dev/null || true
  fi
fi
echo "DIRECT_LOAN_SUMMARY DIRECT_LOAN_PASS=${direct_loan_pass} DIRECT_LOAN_SKIP=${direct_loan_skip} DIRECT_LOAN_RC=${direct_loan_rc} RMW_MDDS_BROKER=0"

capability_pass=0
capability_skip=0
capability_rc=127
capability_ok=0
capability_binary="${TEST_ROOT}/bin/test_pubsub_inproc_capability"
if [ -x "${capability_binary}" ]; then
  export ROS_DOMAIN_ID=$((DOMAIN_BASE + 18))
  timeout "${TIMEOUT_SECONDS}" "${capability_binary}" \
    --gtest_color=no --gtest_filter="${CAPABILITY_FILTER}" \
    >"${CAPABILITY_LOG}" 2>&1
  capability_rc=$?
  capability_pass="$(
    sed -n -E 's/^\[  PASSED  \] ([0-9]+) tests?\..*/\1/p' \
      "${CAPABILITY_LOG}" | tail -n 1
  )"
  capability_skip="$(
    sed -n -E 's/^\[  SKIPPED \] ([0-9]+) tests?.*/\1/p' \
      "${CAPABILITY_LOG}" | head -n 1
  )"
  capability_pass="${capability_pass:-0}"
  capability_skip="${capability_skip:-0}"
  if [ "${capability_rc}" -eq 0 ] && \
      [ "${capability_pass}" -eq "${CAPABILITY_EXPECTED_PASS}" ] && \
      [ "${capability_skip}" -eq "${CAPABILITY_EXPECTED_SKIP}" ]; then
    capability_ok=1
  else
    tail -n 80 "${CAPABILITY_LOG}" 2>/dev/null || true
  fi
fi
echo "CAPABILITY_SUMMARY CAPABILITY_PASS=${capability_pass} CAPABILITY_SKIP=${capability_skip} CAPABILITY_RC=${capability_rc} RMW_MDDS_BROKER=0"
cleanup_broker_state

summary="TEST_RMW_GREEN_SUMMARY PASS=${pass} FAIL=${fail} TOTAL=${total} TEST_ASSERTIONS_PASSED=${passed_tests} SKIP_TESTS=${skipped_tests} DIRECT_LOAN_PASS=${direct_loan_pass} DIRECT_LOAN_SKIP=${direct_loan_skip} CAPABILITY_PASS=${capability_pass} CAPABILITY_SKIP=${capability_skip} RMW=rmw_mdds_cpp"
printf '%s\n' "${summary}" | tee "${RESULT_DIR}/summary.txt"
if [ "${fail}" -eq 0 ] && \
    [ "${pass}" -eq "${UPSTREAM_EXPECTED_PROGRAMS}" ] && \
    [ "${total}" -eq "${UPSTREAM_EXPECTED_PROGRAMS}" ] && \
    [ "${passed_tests}" -eq "${UPSTREAM_EXPECTED_ASSERTIONS}" ] && \
    [ "${skipped_tests}" -eq "${UPSTREAM_EXPECTED_SKIPS}" ] && \
    [ "${direct_loan_ok}" -eq 1 ] && [ "${capability_ok}" -eq 1 ]; then
  echo "RESULT|rmw_mdds_test_rmw_board|PASS|programs=${total}|failed=0|assertions=${passed_tests}|skipped_tests=${skipped_tests}|direct_loan_pass=${direct_loan_pass}|direct_loan_skip=${direct_loan_skip}|capability_pass=${capability_pass}|capability_skip=${capability_skip}|rmw=rmw_mdds_cpp"
  echo "BOARD_RC=0"
  exit 0
fi

echo "RESULT|rmw_mdds_test_rmw_board|FAIL|programs=${total}|passed=${pass}|failed=${fail}|assertions=${passed_tests}|skipped_tests=${skipped_tests}|direct_loan_pass=${direct_loan_pass}|direct_loan_skip=${direct_loan_skip}|direct_loan_rc=${direct_loan_rc}|capability_pass=${capability_pass}|capability_skip=${capability_skip}|capability_rc=${capability_rc}|rmw=rmw_mdds_cpp"
echo "BOARD_RC=1"
exit 1
