#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SOURCE="${ROOT_DIR}/ohos/tools/tsan_ohos_runtime_compat.cpp"
OUTPUT="${1:-${ROOT_DIR}/build/ohos-tsan-runtime-compat/tsan_ohos_runtime_compat.o}"

DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS_4.1"
if [[ ! -d "${DEFAULT_OHOS_ROOT}/command-line-tools" && -d "/home/kaihong/M-DDS/command-line-tools" ]]; then
  DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS"
fi

COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-${DEFAULT_OHOS_ROOT}/command-line-tools}"
NATIVE_ROOT="${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native"
CXX="${ROS2_OHOS_CXX:-${NATIVE_ROOT}/llvm/bin/clang++}"
NM="${ROS2_OHOS_NM:-${NATIVE_ROOT}/llvm/bin/llvm-nm}"
SYSROOT="${ROS2_OHOS_SYSROOT:-${NATIVE_ROOT}/sysroot}"

if [[ ! -x "${CXX}" || ! -x "${NM}" || ! -d "${SYSROOT}" ]]; then
  printf 'OpenHarmony native toolchain is incomplete under %s\n' "${NATIVE_ROOT}" >&2
  exit 2
fi

mkdir -p "$(dirname "${OUTPUT}")"
"${CXX}" \
  --target=aarch64-linux-ohos \
  --sysroot="${SYSROOT}" \
  -std=c++17 \
  -O2 \
  -fPIC \
  -fno-sanitize=thread \
  -Wall \
  -Wextra \
  -Werror \
  -c "${SOURCE}" \
  -o "${OUTPUT}"

symbols="$(${NM} -C "${OUTPUT}")"
for symbol in \
  '__tsan::OnReport(__tsan::ReportDesc const*, bool)' \
  '__tsan::OnFinalize(bool)' \
  '__tsan_on_finalize'; do
  if ! grep -Fq " T ${symbol}" <<<"${symbols}"; then
    printf 'Missing strong TSAN compatibility symbol: %s\n' "${symbol}" >&2
    exit 3
  fi
done

sha256sum "${OUTPUT}"
printf 'TSAN_COMPAT_OBJECT=%s\n' "$(cd "$(dirname "${OUTPUT}")" && pwd)/$(basename "${OUTPUT}")"
