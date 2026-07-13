#!/bin/sh
# codex-file-meta: begin
# relative_path: "ohos/tools/run_rmw_mdds_fullstack_tsan_board.sh"
# language: "shell"
# summary: "Run the RK3588A rmw_mdds full-stack TSAN conformance gate."
# symbols: ["emit"]
# generated_by: "codex"
# codex-file-meta: end

ROOT=${1:-/data/local/tmp/rmw_mdds_tsan_current}
EXPECTED_BRIDGE_SHA=${2:-}
ROS_ROOT=${ROS_ROOT:-/data/local/tmp/ohos-colcon-rk3588a}
TEST_SUPPORT=${TEST_SUPPORT:-/data/local/tmp/rmw_mdds_test_rmw/lib}
PYTHON_RUNTIME=${PYTHON_RUNTIME:-/data/local/release/usr/lib}
TEST_DIR=${ROOT}/bin
OUT=${ROOT}/results_fullstack
SOCKET=${ROOT}/broker.sock
SUMMARY=${OUT}/summary.txt
BROKER_LOG=${RMW_MDDS_TSAN_BROKER_LOG:-${ROOT}/broker_full.log}
BROKER_REPORT_GLOB=${RMW_MDDS_TSAN_BROKER_REPORT_GLOB:-broker_full_tsan*}

if [ -z "${EXPECTED_BRIDGE_SHA}" ]; then
    echo "usage: $0 [root] <expected-bridge-sha256>" >&2
    exit 2
fi

mkdir -p "${OUT}"
rm -f "${OUT}"/*
: > "${SUMMARY}"

total=0
functional_pass=0
functional_fail=0
test_tsan_fail=0
index=0

emit()
{
    echo "$1"
    echo "$1" >> "${SUMMARY}"
}

set -- $(sha256sum "${ROOT}/libmdds_bridge_tsan.z.so")
bridge_sha=$1

for test_bin in "${TEST_DIR}"/test_*; do
    [ -f "${test_bin}" ] || continue
    name=${test_bin##*/}
    index=$((index + 1))
    domain=$((1540 + index))
    log=${OUT}/${name}.log
    rm -f "${OUT}/tsan_${name}"*

    env \
        LD_LIBRARY_PATH=${ROOT}/overlay/lib:${ROOT}/lib:${TEST_SUPPORT}:${PYTHON_RUNTIME}:${ROS_ROOT}/lib:/data/local/tmp/ohos-prefix/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64 \
        LD_PRELOAD=${ROS_ROOT}/lib/librmw_implementation.so \
        RMW_IMPLEMENTATION=rmw_mdds_cpp \
        RMW_MDDS_BROKER=1 \
        RMW_MDDS_BROKER_SOCKET=${SOCKET} \
        ROS_DOMAIN_ID=${domain} \
        TSAN_OPTIONS=log_path=${OUT}/tsan_${name}:verbosity=0 \
        timeout 240 "${test_bin}" > "${log}" 2>&1
    rc=$?

    tsan_files=$(find "${OUT}" -maxdepth 1 -type f -name "tsan_${name}*" | wc -l)
    tsan_text=0
    if grep -E "RMW_MDDS_TSAN_REPORT|WARNING: ThreadSanitizer|ThreadSanitizer: data race" "${log}" >/dev/null 2>&1; then
        tsan_text=1
    fi

    total=$((total + 1))
    if [ "${rc}" -eq 0 ]; then
        functional_pass=$((functional_pass + 1))
    else
        functional_fail=$((functional_fail + 1))
    fi
    if [ "${tsan_files}" -ne 0 ] || [ "${tsan_text}" -ne 0 ]; then
        test_tsan_fail=$((test_tsan_fail + 1))
    fi

    if [ "${rc}" -eq 0 ] && [ "${tsan_files}" -eq 0 ] && [ "${tsan_text}" -eq 0 ]; then
        status=PASS
    else
        status=FAIL
    fi
    emit "RESULT|tsan_fullstack|${status}|name=${name}|domain=${domain}|rc=${rc}|tsan_files=${tsan_files}|tsan_text=${tsan_text}"
done

broker_tsan_files=$(find "${ROOT}" -maxdepth 1 -type f -name "${BROKER_REPORT_GLOB}" | wc -l)
broker_tsan_text=0
if grep -E "RMW_MDDS_TSAN_REPORT|WARNING: ThreadSanitizer|ThreadSanitizer: data race" "${BROKER_LOG}" >/dev/null 2>&1; then
    broker_tsan_text=1
fi
broker_alive=0
if ps -ef | grep "rmw_mdds_broker --socket ${SOCKET}" | grep -v grep >/dev/null 2>&1; then
    broker_alive=1
fi

emit "SUMMARY|tsan_fullstack|total=${total}|functional_pass=${functional_pass}|functional_fail=${functional_fail}|test_tsan_fail=${test_tsan_fail}|broker_tsan_files=${broker_tsan_files}|broker_tsan_text=${broker_tsan_text}|broker_alive=${broker_alive}|bridge_sha=${bridge_sha}"

if [ "${total}" -eq 16 ] && [ "${functional_fail}" -eq 0 ] && \
    [ "${test_tsan_fail}" -eq 0 ] && [ "${broker_tsan_files}" -eq 0 ] && \
    [ "${broker_tsan_text}" -eq 0 ] && [ "${broker_alive}" -eq 1 ] && \
    [ "${bridge_sha}" = "${EXPECTED_BRIDGE_SHA}" ]; then
    echo "BOARD_RC=0" >> "${SUMMARY}"
    echo "BOARD_RC=0"
    exit 0
fi

echo "BOARD_RC=1" >> "${SUMMARY}"
echo "BOARD_RC=1"
exit 1
