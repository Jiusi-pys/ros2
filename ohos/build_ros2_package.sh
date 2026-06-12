#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/build_ros2_package.sh"
# language: "shell"
# summary: "Shell file: set -euo pipefail."
# symbols: []
# generated_by: "codebase-frontmatter-summary"
# codex-file-meta: end

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

DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS_4.1"
if [[ ! -d "${DEFAULT_OHOS_ROOT}/command-line-tools" && -d "/home/kaihong/M-DDS/command-line-tools" ]]; then
  DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS"
fi

COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-${DEFAULT_OHOS_ROOT}/command-line-tools}"
OPENHARMONY_ROOT="${ROS2_OHOS_OPENHARMONY_ROOT:-${DEFAULT_OHOS_ROOT}/OpenHarmony}"
RELEASE_SITE_PACKAGES_ROOT="${ROS2_OHOS_RELEASE_SITE_PACKAGES:-${OPENHARMONY_ROOT}/out/arm64/khs_3588s_sbc/packages/phone/data/local/release/usr/lib/python3.12/site-packages}"

CMAKE_BIN="${ROS2_OHOS_CMAKE:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/cmake}"
NINJA_BIN="${ROS2_OHOS_NINJA:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/ninja}"
PYTHON_BIN="${ROS2_OHOS_PYTHON_HOST:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/llvm/python3/bin/python3}"

PREFIX="${ROS2_OHOS_ROS2_PREFIX:-${ROOT_DIR}/install/ohos-ros2}"
FASTDDS_PREFIX="${ROS2_OHOS_FASTDDS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-fastdds}"
BUILD_ROOT="${ROS2_OHOS_ROS2_BUILD_ROOT:-${ROOT_DIR}/build/ohos-ros2}"
PYDEPS_ROOT="${ROS2_OHOS_ROS2_PYDEPS_ROOT:-${BUILD_ROOT}/pydeps}"
TARGET_PYTHON_VERSION="${ROS2_OHOS_TARGET_PYTHON_VERSION:-3.12}"
TARGET_PYTHON_RUNTIME_ROOT="${ROS2_OHOS_TARGET_PYTHON_RUNTIME_ROOT:-${ROOT_DIR}/build/ohos-python-runtime/usr}"
TARGET_PYTHON_INCLUDE_DIR="${ROS2_OHOS_TARGET_PYTHON_INCLUDE_DIR:-${TARGET_PYTHON_RUNTIME_ROOT}/include/python${TARGET_PYTHON_VERSION}}"
TARGET_PYTHON_LIBRARY="${ROS2_OHOS_TARGET_PYTHON_LIBRARY:-${TARGET_PYTHON_RUNTIME_ROOT}/lib/libpython${TARGET_PYTHON_VERSION}.so}"
TARGET_NUMPY_INCLUDE_DIR="${ROS2_OHOS_TARGET_NUMPY_INCLUDE_DIR:-${TARGET_PYTHON_RUNTIME_ROOT}/lib/python${TARGET_PYTHON_VERSION}/site-packages/numpy/core/include}"
TARGET_PYTHON_EXTENSION_SUFFIX="${ROS2_OHOS_TARGET_PYTHON_EXTENSION_SUFFIX:-.cpython-312-aarch64-linux-ohos.so}"
EIGEN3_INCLUDE_DIR_HINT="${EIGEN3_INCLUDE_DIR:-/tmp/eigen3-root/usr/include/eigen3}"
EIGEN3_PREFIX_HINT="${EIGEN3_PREFIX:-}"
if [[ -z "${EIGEN3_PREFIX_HINT}" && "${EIGEN3_INCLUDE_DIR_HINT}" == */include/eigen3 ]]; then
  EIGEN3_PREFIX_HINT="$(realpath "${EIGEN3_INCLUDE_DIR_HINT}/../.." 2>/dev/null || true)"
fi
EIGEN3_CMAKE_DIR_HINT="${ROS2_OHOS_EIGEN3_DIR:-${Eigen3_DIR:-}}"
if [[ -z "${EIGEN3_CMAKE_DIR_HINT}" && -d "${EIGEN3_INCLUDE_DIR_HINT}" ]]; then
  EIGEN3_CMAKE_DIR_HINT="${BUILD_DIR}/cmake/eigen3"
  mkdir -p "${EIGEN3_CMAKE_DIR_HINT}"
  cat > "${EIGEN3_CMAKE_DIR_HINT}/Eigen3Config.cmake" <<EOF
if(NOT TARGET Eigen3::Eigen)
  add_library(Eigen3::Eigen INTERFACE IMPORTED)
  set_target_properties(Eigen3::Eigen PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_INCLUDE_DIR_HINT}")
endif()

set(Eigen3_FOUND TRUE)
set(EIGEN3_FOUND TRUE)
set(Eigen3_INCLUDE_DIRS "${EIGEN3_INCLUDE_DIR_HINT}")
set(EIGEN3_INCLUDE_DIR "${EIGEN3_INCLUDE_DIR_HINT}")
set(EIGEN3_INCLUDE_DIRS "${EIGEN3_INCLUDE_DIR_HINT}")
set(EIGEN3_ROOT_DIR "${EIGEN3_PREFIX_HINT}")
set(EIGEN3_VERSION_STRING "3.3.7")
set(Eigen3_VERSION "3.3.7")
EOF
fi
if [[ -z "${EIGEN3_CMAKE_DIR_HINT}" && -n "${EIGEN3_PREFIX_HINT}" ]]; then
  for eigen3_cmake_dir in "${EIGEN3_PREFIX_HINT}/lib/cmake/eigen3" "${EIGEN3_PREFIX_HINT}/share/eigen3/cmake"; do
    if [[ -f "${eigen3_cmake_dir}/Eigen3Config.cmake" ]]; then
      EIGEN3_CMAKE_DIR_HINT="${eigen3_cmake_dir}"
      break
    fi
  done
fi
TINYXML2_INCLUDE_DIR_HINT="${TINYXML2_INCLUDE_DIR:-${PREFIX}/include}"
TINYXML2_LIBRARY_HINT="${TINYXML2_LIBRARY:-${PREFIX}/lib/libtinyxml2.so}"
OHOS_ARCH="${ROS2_OHOS_ARCH:-arm64-v8a}"
OHOS_STL="${ROS2_OHOS_STL:-c++_static}"
BUILD_TYPE="${ROS2_OHOS_BUILD_TYPE:-Release}"
TOOLCHAIN_FILE="${ROOT_DIR}/ohos/cmake/kaihongos.toolchain.cmake"
PYTHON_TARGETS_PRELUDE="${ROOT_DIR}/ohos/cmake/ensure_python_targets.cmake"

PURELIB="$("${PYTHON_BIN}" - <<'PY' "${PREFIX}"
import sys
import sysconfig
prefix = sys.argv[1]
print(sysconfig.get_path("purelib", vars={"base": prefix, "platbase": prefix}))
PY
)"
TARGET_PURELIB="${PREFIX}/lib/python${TARGET_PYTHON_VERSION}/site-packages"

mkdir -p "${PYDEPS_ROOT}"
for dep in catkin_pkg packaging pyparsing yaml lark; do
  if [[ -L "${PYDEPS_ROOT}/${dep}" && ! -e "${PYDEPS_ROOT}/${dep}" ]]; then
    rm -f "${PYDEPS_ROOT}/${dep}"
  fi
  if [[ -e "${RELEASE_SITE_PACKAGES_ROOT}/${dep}" && ! -e "${PYDEPS_ROOT}/${dep}" ]]; then
    ln -s "${RELEASE_SITE_PACKAGES_ROOT}/${dep}" "${PYDEPS_ROOT}/${dep}"
  elif [[ -e "${RELEASE_SITE_PACKAGES_ROOT}/${dep}.py" && ! -e "${PYDEPS_ROOT}/${dep}.py" ]]; then
    ln -s "${RELEASE_SITE_PACKAGES_ROOT}/${dep}.py" "${PYDEPS_ROOT}/${dep}.py"
  fi
done
if [[ -L "${PYDEPS_ROOT}/em.py" && ! -e "${PYDEPS_ROOT}/em.py" ]]; then
  rm -f "${PYDEPS_ROOT}/em.py"
fi
if [[ -e "${RELEASE_SITE_PACKAGES_ROOT}/em.py" && ! -e "${PYDEPS_ROOT}/em.py" ]]; then
  ln -s "${RELEASE_SITE_PACKAGES_ROOT}/em.py" "${PYDEPS_ROOT}/em.py"
fi

BASE_PYTHONPATH="${PURELIB}:${PYDEPS_ROOT}"
if [[ -d "${TARGET_PURELIB}" && "${TARGET_PURELIB}" != "${PURELIB}" ]]; then
  BASE_PYTHONPATH="${TARGET_PURELIB}:${BASE_PYTHONPATH}"
fi

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

if [[ -n "${EIGEN3_PREFIX_HINT}" && -d "${EIGEN3_PREFIX_HINT}" ]]; then
  prefix_path_entries+=("${EIGEN3_PREFIX_HINT}")
fi

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
if [[ -d "${TARGET_PYTHON_INCLUDE_DIR}" && -f "${TARGET_PYTHON_LIBRARY}" && -d "${TARGET_NUMPY_INCLUDE_DIR}" ]]; then
  package_dir_args+=(
    "-DROSIDL_GENERATOR_PY_OHOS_TARGET_PYTHON_INCLUDE_DIR=${TARGET_PYTHON_INCLUDE_DIR}"
    "-DROSIDL_GENERATOR_PY_OHOS_TARGET_PYTHON_LIBRARY=${TARGET_PYTHON_LIBRARY}"
    "-DROSIDL_GENERATOR_PY_OHOS_TARGET_NUMPY_INCLUDE_DIR=${TARGET_NUMPY_INCLUDE_DIR}"
    "-DROSIDL_GENERATOR_PY_OHOS_TARGET_EXTENSION_SUFFIX=${TARGET_PYTHON_EXTENSION_SUFFIX}"
  )
fi
if [[ -d "${EIGEN3_INCLUDE_DIR_HINT}" ]]; then
  package_dir_args+=("-DEIGEN3_INCLUDE_DIR=${EIGEN3_INCLUDE_DIR_HINT}")
fi
if [[ -n "${EIGEN3_CMAKE_DIR_HINT}" && -f "${EIGEN3_CMAKE_DIR_HINT}/Eigen3Config.cmake" ]]; then
  package_dir_args+=("-DEigen3_DIR=${EIGEN3_CMAKE_DIR_HINT}")
fi
if [[ -d "${TINYXML2_INCLUDE_DIR_HINT}" && -f "${TINYXML2_LIBRARY_HINT}" ]]; then
  package_dir_args+=(
    "-DTINYXML2_INCLUDE_DIR=${TINYXML2_INCLUDE_DIR_HINT}"
    "-DTINYXML2_LIBRARY=${TINYXML2_LIBRARY_HINT}"
  )
fi

PYTHONPATH="${BASE_PYTHONPATH}${PYTHONPATH:+:${PYTHONPATH}}" \
  "${CMAKE_BIN}" -S "${SOURCE_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="${NINJA_BIN}" \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
  -DCMAKE_PROJECT_INCLUDE_BEFORE="${PYTHON_TARGETS_PRELUDE}" \
  -DROS2_OHOS_COMMAND_LINE_TOOLS_ROOT="${COMMAND_LINE_TOOLS_ROOT}" \
  -DROS2_OHOS_TARGET_PYTHON_INCLUDE_DIR="${TARGET_PYTHON_INCLUDE_DIR}" \
  -DROS2_OHOS_TARGET_PYTHON_LIBRARY="${TARGET_PYTHON_LIBRARY}" \
  -DROS2_OHOS_TARGET_NUMPY_INCLUDE_DIR="${TARGET_NUMPY_INCLUDE_DIR}" \
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

if [[ "${TARGET_PURELIB}" != "${PURELIB}" && -d "${PURELIB}" ]]; then
  mkdir -p "${TARGET_PURELIB}"
  cp -a "${PURELIB}/." "${TARGET_PURELIB}/"
fi
