#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <package-source-dir> [build-dir]" >&2
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_DIR="$(realpath "$1")"
PACKAGE_NAME="$(basename "${SOURCE_DIR}")"
shift

BUILD_DIR="${ROOT_DIR}/build/ohos-ros2/${PACKAGE_NAME}"
if [[ $# -gt 0 && "$1" != "--" ]]; then
  BUILD_DIR="$1"
  shift
fi

EXTRA_CMAKE_ARGS=()
if [[ $# -gt 0 && "$1" == "--" ]]; then
  shift
  EXTRA_CMAKE_ARGS=("$@")
fi

COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-/home/kaihong/M-DDS_4.1/command-line-tools}"
OPENHARMONY_ROOT="${ROS2_OHOS_OPENHARMONY_ROOT:-/home/kaihong/M-DDS_4.1/OpenHarmony}"
RELEASE_SITE_PACKAGES_ROOT="${ROS2_OHOS_RELEASE_SITE_PACKAGES:-${OPENHARMONY_ROOT}/out/arm64/khs_3588s_sbc/packages/phone/data/local/release/usr/lib/python3.12/site-packages}"

CMAKE_BIN="${ROS2_OHOS_CMAKE:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/cmake}"
NINJA_BIN="${ROS2_OHOS_NINJA:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/ninja}"
PYTHON_BIN="${ROS2_OHOS_PYTHON_HOST:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/llvm/python3/bin/python3}"

PREFIX="${ROS2_OHOS_ROS2_PREFIX:-${ROOT_DIR}/install/ohos-ros2}"
FASTDDS_PREFIX="${ROS2_OHOS_FASTDDS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-fastdds}"
BUILD_ROOT="${ROS2_OHOS_ROS2_BUILD_ROOT:-${ROOT_DIR}/build/ohos-ros2}"
PYDEPS_ROOT="${ROS2_OHOS_ROS2_PYDEPS_ROOT:-${BUILD_ROOT}/pydeps}"
OHOS_ARCH="${ROS2_OHOS_ARCH:-arm64-v8a}"
OHOS_STL="${ROS2_OHOS_STL:-c++_static}"
BUILD_TYPE="${ROS2_OHOS_BUILD_TYPE:-Release}"
TOOLCHAIN_FILE="${ROOT_DIR}/ohos/cmake/kaihongos.toolchain.cmake"

PURELIB="$("${PYTHON_BIN}" - <<'PY' "${PREFIX}"
import sys
import sysconfig
prefix = sys.argv[1]
print(sysconfig.get_path("purelib", vars={"base": prefix, "platbase": prefix}))
PY
)"

mkdir -p "${PYDEPS_ROOT}"
for dep in catkin_pkg packaging pyparsing yaml lark; do
  if [[ -e "${RELEASE_SITE_PACKAGES_ROOT}/${dep}" && ! -e "${PYDEPS_ROOT}/${dep}" ]]; then
    ln -s "${RELEASE_SITE_PACKAGES_ROOT}/${dep}" "${PYDEPS_ROOT}/${dep}"
  fi
done
if [[ -e "${RELEASE_SITE_PACKAGES_ROOT}/em.py" && ! -e "${PYDEPS_ROOT}/em.py" ]]; then
  ln -s "${RELEASE_SITE_PACKAGES_ROOT}/em.py" "${PYDEPS_ROOT}/em.py"
fi

BASE_PYTHONPATH="${PURELIB}:${PYDEPS_ROOT}"

package_dir_args=()
prefix_path_entries=()
for base_prefix in "${PREFIX}" "${FASTDDS_PREFIX}"; do
  search_roots=("${base_prefix}")
  if [[ -d "${base_prefix}/opt" ]]; then
    while IFS= read -r vendor_root; do
      search_roots+=("${vendor_root}")
    done < <(find "${base_prefix}/opt" -mindepth 1 -maxdepth 1 -type d | sort)
  fi

  prefix_path_entries+=("${search_roots[@]}")

  for root in "${search_roots[@]}"; do
    for dir in "${root}"/share/*/cmake "${root}"/lib/cmake/* "${root}"/lib/*/cmake "${root}"/cmake; do
      if [[ -d "${dir}" ]]; then
        package_names=()
        while IFS= read -r config_file; do
          config_name="$(basename "${config_file}")"
          config_name="${config_name%-config.cmake}"
          config_name="${config_name%Config.cmake}"
          package_names+=("${config_name}")
        done < <(find "${dir}" -maxdepth 1 -type f \( -name '*Config.cmake' -o -name '*-config.cmake' \) | sort)

        if [[ ${#package_names[@]} -eq 0 ]]; then
          package_name="$(basename "$(dirname "${dir}")")"
          if [[ "${package_name}" == "cmake" ]]; then
            package_name="$(basename "${dir}")"
          fi
          package_names=("${package_name}")
        fi

        for package_name in "${package_names[@]}"; do
          package_dir_args+=("-D${package_name}_DIR=${dir}")
        done
      fi
    done
  done
done

prefix_path_joined=""
for prefix_entry in "${prefix_path_entries[@]}"; do
  if [[ -z "${prefix_path_joined}" ]]; then
    prefix_path_joined="${prefix_entry}"
  else
    prefix_path_joined="${prefix_path_joined};${prefix_entry}"
  fi
done

if [[ -d "${FASTDDS_PREFIX}/include" ]]; then
  package_dir_args+=("-DFastRTPS_INCLUDE_DIR=${FASTDDS_PREFIX}/include")
fi
if [[ -f "${FASTDDS_PREFIX}/lib/libfastrtps.so" ]]; then
  package_dir_args+=("-DFastRTPS_LIBRARY_RELEASE=${FASTDDS_PREFIX}/lib/libfastrtps.so")
fi
if [[ -f "${FASTDDS_PREFIX}/lib/libfastcdr.so" ]]; then
  package_dir_args+=("-DFastCDR_LIBRARY_RELEASE=${FASTDDS_PREFIX}/lib/libfastcdr.so")
fi

PYTHONPATH="${BASE_PYTHONPATH}${PYTHONPATH:+:${PYTHONPATH}}" \
  "${CMAKE_BIN}" -S "${SOURCE_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="${NINJA_BIN}" \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
  -DROS2_OHOS_COMMAND_LINE_TOOLS_ROOT="${COMMAND_LINE_TOOLS_ROOT}" \
  -DOHOS_ARCH="${OHOS_ARCH}" \
  -DOHOS_STL="${OHOS_STL}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
  "-DCMAKE_PREFIX_PATH=${prefix_path_joined}" \
  -DPython3_EXECUTABLE="${PYTHON_BIN}" \
  -DBUILD_TESTING=OFF \
  "${package_dir_args[@]}" \
  "${EXTRA_CMAKE_ARGS[@]}"

PYTHONPATH="${BASE_PYTHONPATH}${PYTHONPATH:+:${PYTHONPATH}}" \
  "${CMAKE_BIN}" --build "${BUILD_DIR}" --target install -- -j"$(nproc)"
