#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS_4.1"
if [[ ! -d "${DEFAULT_OHOS_ROOT}/command-line-tools" && -d "/home/kaihong/M-DDS/command-line-tools" ]]; then
  DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS"
fi

COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-${DEFAULT_OHOS_ROOT}/command-line-tools}"
OPENHARMONY_ROOT="${ROS2_OHOS_OPENHARMONY_ROOT:-${DEFAULT_OHOS_ROOT}/OpenHarmony}"
if [[ -z "${ROS2_OHOS_OPENHARMONY_ROOT:-}" && ! -d "${OPENHARMONY_ROOT}" ]]; then
  for openharmony_candidate in \
    "${DEFAULT_OHOS_ROOT}/OpenHarmony_lyl" \
    "/home/kaihong/M-DDS/OpenHarmony_lyl"; do
    if [[ -d "${openharmony_candidate}" ]]; then
      OPENHARMONY_ROOT="${openharmony_candidate}"
      break
    fi
  done
fi
CMAKE_BIN="${ROS2_OHOS_CMAKE:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/cmake}"
NINJA_BIN="${ROS2_OHOS_NINJA:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/build-tools/cmake/bin/ninja}"
PYTHON_BIN="${ROS2_OHOS_PYTHON_HOST:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/llvm/python3/bin/python3}"

UNDERLAY_PREFIX="${ROS2_OHOS_ROS2_PREFIX:-${ROOT_DIR}/install/ohos-ros2}"
FASTDDS_PREFIX="${ROS2_OHOS_FASTDDS_INSTALL_DIR:-${ROOT_DIR}/install/ohos-fastdds}"
BUILD_BASE="${ROS2_OHOS_COLCON_BUILD_BASE:-${ROOT_DIR}/build/ohos-colcon-rk3588a}"
INSTALL_BASE="${ROS2_OHOS_COLCON_INSTALL_BASE:-${ROOT_DIR}/install/ohos-colcon-rk3588a}"
PYDEPS_ROOT="${ROS2_OHOS_ROS2_PYDEPS_ROOT:-${ROOT_DIR}/build/ohos-ros2/pydeps}"
BUILD_TYPE="${ROS2_OHOS_BUILD_TYPE:-Release}"
OHOS_ARCH="${ROS2_OHOS_ARCH:-arm64-v8a}"
OHOS_OUT_ARCH="${ROS2_OHOS_OUT_ARCH:-arm64}"
OHOS_STL="${ROS2_OHOS_STL:-c++_static}"
TOOLCHAIN_FILE="${ROOT_DIR}/ohos/cmake/kaihongos.toolchain.cmake"
PYTHON_TARGETS_PRELUDE="${ROOT_DIR}/ohos/cmake/ensure_python_targets.cmake"
TARGET_PYTHON_VERSION="${ROS2_OHOS_TARGET_PYTHON_VERSION:-3.12}"
TARGET_PYTHON_RUNTIME_ROOT="${ROS2_OHOS_TARGET_PYTHON_RUNTIME_ROOT:-${ROOT_DIR}/build/ohos-python-runtime/usr}"
TARGET_PYTHON_INCLUDE_DIR="${ROS2_OHOS_TARGET_PYTHON_INCLUDE_DIR:-${TARGET_PYTHON_RUNTIME_ROOT}/include/python${TARGET_PYTHON_VERSION}}"
TARGET_PYTHON_LIBRARY="${ROS2_OHOS_TARGET_PYTHON_LIBRARY:-${TARGET_PYTHON_RUNTIME_ROOT}/lib/libpython${TARGET_PYTHON_VERSION}.so}"
TARGET_NUMPY_INCLUDE_DIR="${ROS2_OHOS_TARGET_NUMPY_INCLUDE_DIR:-${TARGET_PYTHON_RUNTIME_ROOT}/lib/python${TARGET_PYTHON_VERSION}/site-packages/numpy/core/include}"
TARGET_PYTHON_EXTENSION_SUFFIX="${ROS2_OHOS_TARGET_PYTHON_EXTENSION_SUFFIX:-.cpython-312-aarch64-linux-ohos.so}"
TARGET_PURELIB="${INSTALL_BASE}/lib/python${TARGET_PYTHON_VERSION}/site-packages"
UNDERLAY_TARGET_PURELIB="${UNDERLAY_PREFIX}/lib/python${TARGET_PYTHON_VERSION}/site-packages"
TARGET_SCRIPTS="${INSTALL_BASE}/bin"
TARGET_PYTHON_BIN="${ROS2_OHOS_TARGET_PYTHON_BIN:-/data/local/release/usr/bin/python3.12}"
REMOTE_UNDERLAY_PREFIX="${ROS2_OHOS_REMOTE_UNDERLAY_PREFIX:-/data/local/tmp/ohos-prefix}"
REMOTE_FASTDDS_PREFIX="${ROS2_OHOS_REMOTE_FASTDDS_PREFIX:-/data/local/tmp/ohos-fastdds}"
REMOTE_PYDEPS_PREFIX="${ROS2_OHOS_REMOTE_PYDEPS_PREFIX:-}"
TINYXML2_INCLUDE_DIR_HINT="${TINYXML2_INCLUDE_DIR:-${UNDERLAY_PREFIX}/include}"
TINYXML2_LIBRARY_HINT="${TINYXML2_LIBRARY:-${UNDERLAY_PREFIX}/lib/libtinyxml2.so}"
TARGET_OPENSSL_INCLUDE_DIR_HINT="${ROS2_OHOS_OPENSSL_INCLUDE_DIR:-}"
TARGET_OPENSSL_CRYPTO_LIBRARY_HINT="${ROS2_OHOS_OPENSSL_CRYPTO_LIBRARY:-}"
COLCON_EVENT_HANDLERS="${ROS2_OHOS_COLCON_EVENT_HANDLERS:-console_direct+}"
DIRECT_CMAKE_BUILD="${ROS2_OHOS_COLCON_DIRECT_CMAKE:-1}"
CLEAN_INSTALL="${ROS2_OHOS_COLCON_CLEAN_INSTALL:-0}"

DEFAULT_PACKAGES=(
  rcutils
  ament_index_python
  ament_index_cpp
  ros2cli
  ros2pkg
  rcl
  rcl_action
  rcl_lifecycle
  rclcpp
  rclcpp_action
  rclcpp_components
  rclcpp_lifecycle
  rpyutils
  rclpy
  rosidl_generator_py
  action_tutorials_interfaces
  ros2action
  ros2doctor
  ros2interface
  ros2node
  ros2param
  ros2run
  ros2service
  ros2topic
  ros2lifecycle
  ros2component
  ros2bag
  ros2cli_common_extensions
  std_msgs
  example_interfaces
  geometry_msgs
  sensor_msgs
  nav_msgs
  tf2
  tf2_py
  tf2_ros
  osrf_pycommon
  launch
  launch_xml
  launch_yaml
  launch_ros
  ros2launch
  class_loader
  pluginlib
  composition
)

if [[ $# -gt 0 ]]; then
  PACKAGES=("$@")
else
  PACKAGES=("${DEFAULT_PACKAGES[@]}")
fi

if [[ ! -x "${CMAKE_BIN}" ]]; then
  echo "cmake not found at ${CMAKE_BIN}" >&2
  exit 1
fi
if [[ ! -x "${NINJA_BIN}" ]]; then
  echo "ninja not found at ${NINJA_BIN}" >&2
  exit 1
fi
if [[ ! -x "${PYTHON_BIN}" ]]; then
  echo "python3 not found at ${PYTHON_BIN}" >&2
  exit 1
fi
if [[ ! -d "${UNDERLAY_PREFIX}" ]]; then
  echo "OHOS ROS 2 underlay not found at ${UNDERLAY_PREFIX}" >&2
  echo "Run ./ohos/build_ros2_bootstrap.sh and package builds first." >&2
  exit 1
fi
if [[ ! -d "${FASTDDS_PREFIX}" ]]; then
  echo "OHOS FastDDS prefix not found at ${FASTDDS_PREFIX}" >&2
  echo "Run ./ohos/build_fastdds_stack.sh first." >&2
  exit 1
fi

if [[ "${CLEAN_INSTALL}" != "0" ]]; then
  case "${BUILD_BASE}" in
    "${ROOT_DIR}/build/"*|/tmp/*) rm -rf "${BUILD_BASE:?}" ;;
    *) echo "Refusing to clean unexpected build base: ${BUILD_BASE}" >&2; exit 1 ;;
  esac
  case "${INSTALL_BASE}" in
    "${ROOT_DIR}/install/"*|/tmp/*) rm -rf "${INSTALL_BASE:?}" ;;
    *) echo "Refusing to clean unexpected install base: ${INSTALL_BASE}" >&2; exit 1 ;;
  esac
fi

PURELIB="$("${PYTHON_BIN}" - <<'PY' "${UNDERLAY_PREFIX}"
import sys
import sysconfig
prefix = sys.argv[1]
print(sysconfig.get_path("purelib", vars={"base": prefix, "platbase": prefix}))
PY
)"

PREFIX_PATH_ENTRIES=(
  "${UNDERLAY_PREFIX}"
  "${FASTDDS_PREFIX}"
)
if [[ -d "${UNDERLAY_PREFIX}/opt" ]]; then
  while IFS= read -r vendor_root; do
    PREFIX_PATH_ENTRIES+=("${vendor_root}")
  done < <(find "${UNDERLAY_PREFIX}/opt" -mindepth 1 -maxdepth 1 -type d | sort)
fi

CMAKE_PREFIX_PATH_CMAKE=""
CMAKE_PREFIX_PATH_ENV=""
PACKAGE_DIR_ARGS=()
declare -A PACKAGE_DIR_SEEN=()
for prefix_entry in "${PREFIX_PATH_ENTRIES[@]}"; do
  if [[ -d "${prefix_entry}" ]]; then
    if [[ -z "${CMAKE_PREFIX_PATH_CMAKE}" ]]; then
      CMAKE_PREFIX_PATH_CMAKE="${prefix_entry}"
      CMAKE_PREFIX_PATH_ENV="${prefix_entry}"
    else
      CMAKE_PREFIX_PATH_CMAKE="${CMAKE_PREFIX_PATH_CMAKE};${prefix_entry}"
      CMAKE_PREFIX_PATH_ENV="${CMAKE_PREFIX_PATH_ENV}:${prefix_entry}"
    fi

    for dir in "${prefix_entry}"/share/*/cmake "${prefix_entry}"/lib/cmake/* "${prefix_entry}"/lib/*/cmake "${prefix_entry}"/cmake; do
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
          if [[ -z "${PACKAGE_DIR_SEEN["${package_name}"]+x}" ]]; then
            PACKAGE_DIR_ARGS+=("-D${package_name}_DIR=${dir}")
            PACKAGE_DIR_SEEN["${package_name}"]=1
          fi
        done
      fi
    done
  fi
done
if [[ -d "${FASTDDS_PREFIX}/include" ]]; then
  PACKAGE_DIR_ARGS+=("-DFastRTPS_INCLUDE_DIR=${FASTDDS_PREFIX}/include")
fi
if [[ -f "${FASTDDS_PREFIX}/lib/libfastrtps.so" ]]; then
  PACKAGE_DIR_ARGS+=("-DFastRTPS_LIBRARY_RELEASE=${FASTDDS_PREFIX}/lib/libfastrtps.so")
fi
if [[ -f "${FASTDDS_PREFIX}/lib/libfastcdr.so" ]]; then
  PACKAGE_DIR_ARGS+=("-DFastCDR_LIBRARY_RELEASE=${FASTDDS_PREFIX}/lib/libfastcdr.so")
fi
if [[ -d "${TARGET_PYTHON_INCLUDE_DIR}" && -f "${TARGET_PYTHON_LIBRARY}" ]]; then
  PACKAGE_DIR_ARGS+=(
    "-DROSIDL_GENERATOR_PY_OHOS_TARGET_PYTHON_INCLUDE_DIR=${TARGET_PYTHON_INCLUDE_DIR}"
    "-DROSIDL_GENERATOR_PY_OHOS_TARGET_PYTHON_LIBRARY=${TARGET_PYTHON_LIBRARY}"
    "-DROSIDL_GENERATOR_PY_OHOS_TARGET_EXTENSION_SUFFIX=${TARGET_PYTHON_EXTENSION_SUFFIX}"
    "-DRCLPY_OHOS_TARGET_PYTHON_INCLUDE_DIR=${TARGET_PYTHON_INCLUDE_DIR}"
    "-DRCLPY_OHOS_TARGET_PYTHON_LIBRARY=${TARGET_PYTHON_LIBRARY}"
    "-DRCLPY_OHOS_TARGET_PYTHON_EXTENSION_SUFFIX=${TARGET_PYTHON_EXTENSION_SUFFIX}"
    "-DTF2_PY_OHOS_TARGET_PYTHON_INCLUDE_DIR=${TARGET_PYTHON_INCLUDE_DIR}"
    "-DTF2_PY_OHOS_TARGET_PYTHON_LIBRARY=${TARGET_PYTHON_LIBRARY}"
    "-DTF2_PY_OHOS_TARGET_EXTENSION_SUFFIX=${TARGET_PYTHON_EXTENSION_SUFFIX}"
    "-DTF2_GEOMETRY_MSGS_OHOS_TARGET_PYTHON_INCLUDE_DIR=${TARGET_PYTHON_INCLUDE_DIR}"
    "-DTF2_GEOMETRY_MSGS_OHOS_TARGET_PYTHON_LIBRARY=${TARGET_PYTHON_LIBRARY}"
  )
  if [[ -d "${TARGET_NUMPY_INCLUDE_DIR}" ]]; then
    PACKAGE_DIR_ARGS+=("-DROSIDL_GENERATOR_PY_OHOS_TARGET_NUMPY_INCLUDE_DIR=${TARGET_NUMPY_INCLUDE_DIR}")
  fi
  if [[ -d "${UNDERLAY_PREFIX}/include/pybind11" ]]; then
    PACKAGE_DIR_ARGS+=("-DRCLPY_OHOS_TARGET_PYBIND11_INCLUDE_DIR=${UNDERLAY_PREFIX}/include")
  fi
fi
if [[ -d "${TINYXML2_INCLUDE_DIR_HINT}" && -f "${TINYXML2_LIBRARY_HINT}" ]]; then
  PACKAGE_DIR_ARGS+=(
    "-DTINYXML2_INCLUDE_DIR=${TINYXML2_INCLUDE_DIR_HINT}"
    "-DTINYXML2_LIBRARY=${TINYXML2_LIBRARY_HINT}"
  )
fi
if [[ -z "${TARGET_OPENSSL_INCLUDE_DIR_HINT}" ]]; then
  for openssl_include_candidate in \
    "${OPENHARMONY_ROOT}/extension/communication/kh_iotsdk/include" \
    "${OPENHARMONY_ROOT}/third_party/openssl/include" \
    "${OPENHARMONY_ROOT}/extension/security/tee/optee_sdk/export-ta_arm64/host_include" \
    "${OPENHARMONY_ROOT}/extension/security/tee/optee_sdk/export-ta_arm64/include"; do
    if [[ -f "${openssl_include_candidate}/openssl/evp.h" &&
      -f "${openssl_include_candidate}/openssl/err.h" ]]; then
      TARGET_OPENSSL_INCLUDE_DIR_HINT="${openssl_include_candidate}"
      break
    fi
  done
fi
if [[ -z "${TARGET_OPENSSL_CRYPTO_LIBRARY_HINT}" ]]; then
  for openssl_crypto_candidate in \
    "${OPENHARMONY_ROOT}/out/${OHOS_OUT_ARCH}/targets/thirdparty/openssl/libcrypto_openssl.z.so" \
    "${OPENHARMONY_ROOT}/out/${OHOS_OUT_ARCH}/targets/innerkits/ohos-arm64/openssl/libcrypto_shared/libcrypto_openssl.z.so"; do
    if [[ -f "${openssl_crypto_candidate}" ]]; then
      TARGET_OPENSSL_CRYPTO_LIBRARY_HINT="${openssl_crypto_candidate}"
      break
    fi
  done
fi
if [[ -d "${TARGET_OPENSSL_INCLUDE_DIR_HINT}" && -f "${TARGET_OPENSSL_CRYPTO_LIBRARY_HINT}" ]]; then
  PACKAGE_DIR_ARGS+=(
    "-DRMW_MDDS_TARGET_OPENSSL_INCLUDE_DIR=${TARGET_OPENSSL_INCLUDE_DIR_HINT}"
    "-DRMW_MDDS_TARGET_OPENSSL_CRYPTO_LIBRARY=${TARGET_OPENSSL_CRYPTO_LIBRARY_HINT}"
  )
fi
OVERLAY_PACKAGE_DIR_OVERRIDES=(
  rmw_implementation
  rcl
  rcl_action
  rcl_lifecycle
  rclcpp
  rclcpp_action
  rclcpp_components
  rclcpp_lifecycle
  rmw_mdds_cpp
  demo_nodes_cpp
  action_tutorials_cpp
)
for package_name in "${OVERLAY_PACKAGE_DIR_OVERRIDES[@]}"; do
  package_cmake_dir="${INSTALL_BASE}/share/${package_name}/cmake"
  if [[ -d "${package_cmake_dir}" ]]; then
    PACKAGE_DIR_ARGS+=("-D${package_name}_DIR=${package_cmake_dir}")
  fi
done

export CMAKE_COMMAND="${CMAKE_BIN}"
export CMAKE_PREFIX_PATH="${INSTALL_BASE}:${CMAKE_PREFIX_PATH_ENV}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
export AMENT_PREFIX_PATH="${INSTALL_BASE}:${UNDERLAY_PREFIX}${AMENT_PREFIX_PATH:+:${AMENT_PREFIX_PATH}}"
PYTHONPATH_PREFIXES=()
for pythonpath_entry in "${PURELIB}" "${UNDERLAY_TARGET_PURELIB}" "${PYDEPS_ROOT}"; do
  if [[ -d "${pythonpath_entry}" ]]; then
    PYTHONPATH_PREFIXES+=("${pythonpath_entry}")
  fi
done
PYTHONPATH_PREFIX=""
for pythonpath_entry in "${PYTHONPATH_PREFIXES[@]}"; do
  if [[ -z "${PYTHONPATH_PREFIX}" ]]; then
    PYTHONPATH_PREFIX="${pythonpath_entry}"
  else
    PYTHONPATH_PREFIX="${PYTHONPATH_PREFIX}:${pythonpath_entry}"
  fi
done
export PYTHONPATH="${PYTHONPATH_PREFIX}${PYTHONPATH_PREFIX:+${PYTHONPATH:+:}}${PYTHONPATH:-}"

write_ohos_wrapper() {
  local wrapper_path="$1"
  local argv0_name="$2"
  local module_name="$3"
  local function_name="$4"

  cat > "${wrapper_path}" <<EOF
#!/bin/sh
SCRIPT_DIR=\$(CDPATH= cd -- "\$(dirname "\$0")" && pwd)
PREFIX=\$(CDPATH= cd -- "\${SCRIPT_DIR}/.." && pwd)
VENDOR_LIB_PATH=
for dir in "\${PREFIX}"/opt/*/lib; do
  if [ -d "\${dir}" ]; then
    VENDOR_LIB_PATH="\${VENDOR_LIB_PATH:+\${VENDOR_LIB_PATH}:}\${dir}"
  fi
done
UNDERLAY_PREFIX="${REMOTE_UNDERLAY_PREFIX}"
FASTDDS_PREFIX="${REMOTE_FASTDDS_PREFIX}"
UNDERLAY_VENDOR_LIB_PATH=
for dir in "\${UNDERLAY_PREFIX}"/opt/*/lib; do
  if [ -d "\${dir}" ]; then
    UNDERLAY_VENDOR_LIB_PATH="\${UNDERLAY_VENDOR_LIB_PATH:+\${UNDERLAY_VENDOR_LIB_PATH}:}\${dir}"
  fi
done
if [ -n "\${LD_PRELOAD:-}" ]; then
  export LD_PRELOAD="${TARGET_PYTHON_BIN%/bin/python3.12}/lib/libpython3.12.so.1.0 \${LD_PRELOAD}"
else
  export LD_PRELOAD="${TARGET_PYTHON_BIN%/bin/python3.12}/lib/libpython3.12.so.1.0"
fi
export PYTHONHOME="${TARGET_PYTHON_BIN%/bin/python3.12}"
if [ -z "\${HOME:-}" ] || [ ! -w "\${HOME}" ]; then
  export HOME="/data/local/tmp"
fi
if [ -z "\${ROS_LOG_DIR:-}" ]; then
  export ROS_LOG_DIR="/data/local/tmp/roslogs"
fi
export LD_LIBRARY_PATH="\${PREFIX}/lib:\${UNDERLAY_PREFIX}/lib:\${FASTDDS_PREFIX}/lib\${VENDOR_LIB_PATH:+:\${VENDOR_LIB_PATH}}\${UNDERLAY_VENDOR_LIB_PATH:+:\${UNDERLAY_VENDOR_LIB_PATH}}:/data/local/tmp:/data/local/release/usr/lib:/system/lib64/platformsdk:/system/lib64/chipset-pub-sdk:/system/lib64\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}"
export AMENT_PREFIX_PATH="\${PREFIX}:\${UNDERLAY_PREFIX}\${AMENT_PREFIX_PATH:+:\${AMENT_PREFIX_PATH}}"
export CMAKE_PREFIX_PATH="\${PREFIX}:\${UNDERLAY_PREFIX}:\${FASTDDS_PREFIX}\${CMAKE_PREFIX_PATH:+:\${CMAKE_PREFIX_PATH}}"
export COLCON_PREFIX_PATH="\${PREFIX}:\${UNDERLAY_PREFIX}\${COLCON_PREFIX_PATH:+:\${COLCON_PREFIX_PATH}}"
export PYTHONPATH="\${PREFIX}/lib/python${TARGET_PYTHON_VERSION}/site-packages:\${UNDERLAY_PREFIX}/lib/python${TARGET_PYTHON_VERSION}/site-packages:\${UNDERLAY_PREFIX}/lib/python3.11/site-packages${REMOTE_PYDEPS_PREFIX:+:${REMOTE_PYDEPS_PREFIX}}\${PYTHONPATH:+:\${PYTHONPATH}}"
exec "${TARGET_PYTHON_BIN}" -c 'import sys; sys.argv[0]="${argv0_name}"; from ${module_name} import ${function_name} as _entry; sys.exit(_entry())' "\$@"
EOF
  chmod 755 "${wrapper_path}"
}

clean_python_package_install_artifacts() {
  local package_name="$1"

  if [[ -d "${TARGET_SCRIPTS}" ]]; then
    rm -f "${TARGET_SCRIPTS}/${package_name}__"*
  fi

  if [[ ! -d "${INSTALL_BASE}/lib" ]]; then
    return 0
  fi

  local entry_points_file pkg_info distribution_name line script_name
  while IFS= read -r entry_points_file; do
    pkg_info="${entry_points_file%/entry_points.txt}/PKG-INFO"
    if [[ -f "${pkg_info}" ]]; then
      distribution_name="$(sed -n 's/^Name: //p' "${pkg_info}" | head -n 1)"
      if [[ -n "${distribution_name}" ]]; then
        rm -f "${TARGET_SCRIPTS}/${distribution_name}__"*
      fi
    fi

    while IFS= read -r line; do
      script_name="${line%%=*}"
      script_name="$(printf '%s' "${script_name}" | xargs)"
      if [[ -n "${script_name}" ]]; then
        rm -f "${TARGET_SCRIPTS}/${script_name}"
      fi
    done < <(
      awk '
        /^\[console_scripts\]$/ {in_section=1; next}
        /^\[/ {if (in_section) exit}
        in_section && NF {print}
      ' "${entry_points_file}"
    )
  done < <(find "${INSTALL_BASE}/lib" -mindepth 3 -maxdepth 4 -path "*/python*/site-packages/${package_name}-*.egg-info/entry_points.txt" -type f | sort)

  find "${INSTALL_BASE}/lib" -mindepth 3 -maxdepth 3 -type d -path "*/python*/site-packages/${package_name}" -prune -exec rm -rf {} +
  find "${INSTALL_BASE}/lib" -mindepth 3 -maxdepth 3 -type d -path "*/python*/site-packages/${package_name}-*.egg-info" -prune -exec rm -rf {} +
}

write_pythonpath_hook_set() {
  local package_name="$1"
  local hook_dir="${INSTALL_BASE}/share/${package_name}/hook"
  local environment_dir="${INSTALL_BASE}/share/${package_name}/environment"
  local target_pythonpath="lib/python${TARGET_PYTHON_VERSION}/site-packages"

  mkdir -p "${hook_dir}" "${environment_dir}"
  cat > "${hook_dir}/pythonpath.sh" <<EOF
# generated by colcon_rk3588a.sh for OHOS target Python

_colcon_prepend_unique_value PYTHONPATH "\$COLCON_CURRENT_PREFIX/${target_pythonpath}"
EOF
  printf 'prepend-non-duplicate;PYTHONPATH;%s\n' "${target_pythonpath}" > "${hook_dir}/pythonpath.dsv"
  cat > "${hook_dir}/pythonpath.ps1" <<EOF
# generated by colcon_rk3588a.sh for OHOS target Python

colcon_prepend_unique_value PYTHONPATH "\$env:COLCON_CURRENT_PREFIX/${target_pythonpath}"
EOF

  cat > "${environment_dir}/pythonpath.sh" <<EOF
# generated by colcon_rk3588a.sh for OHOS target Python

ament_prepend_unique_value PYTHONPATH "\$AMENT_CURRENT_PREFIX/${target_pythonpath}"
EOF
  printf 'prepend-non-duplicate;PYTHONPATH;%s\n' "${target_pythonpath}" > "${environment_dir}/pythonpath.dsv"
  cat > "${environment_dir}/pythonpath.ps1" <<EOF
# generated by colcon_rk3588a.sh for OHOS target Python

ament_prepend_unique_value PYTHONPATH "\$env:AMENT_CURRENT_PREFIX/${target_pythonpath}"
EOF
}

install_ament_python_package() {
  local package_name="$1"
  local package_path="$2"
  local package_build_base="${BUILD_BASE}/${package_name}"

  rm -rf "${package_build_base}"
  clean_python_package_install_artifacts "${package_name}"
  mkdir -p "${package_build_base}" "${TARGET_PURELIB}" "${TARGET_SCRIPTS}"

  (
    cd "${package_path}"
    "${PYTHON_BIN}" setup.py install \
      --prefix "${INSTALL_BASE}" \
      --install-lib "${TARGET_PURELIB}" \
      --install-scripts "${TARGET_SCRIPTS}" \
      --no-compile \
      --single-version-externally-managed \
      --record "${package_build_base}/install.log"
  )

  write_pythonpath_hook_set "${package_name}"
}

sync_python_artifacts_to_target_purelib() {
  local source_site_packages=()
  if [[ ! -d "${INSTALL_BASE}/lib" ]]; then
    return 0
  fi

  while IFS= read -r site_packages_dir; do
    if [[ "${site_packages_dir}" == "${TARGET_PURELIB}" ]]; then
      continue
    fi
    source_site_packages+=("${site_packages_dir}")
  done < <(find "${INSTALL_BASE}/lib" -mindepth 2 -maxdepth 2 -type d -path "*/python*/site-packages" | sort)

  if [[ ${#source_site_packages[@]} -eq 0 ]]; then
    return 0
  fi

  mkdir -p "${TARGET_PURELIB}"
  for site_packages_dir in "${source_site_packages[@]}"; do
    while IFS= read -r entry; do
      cp -a "${entry}" "${TARGET_PURELIB}/"
    done < <(
      find "${site_packages_dir}" -mindepth 1 -maxdepth 1 \
        ! -name '__pycache__' \
        ! -name '*.pyc' \
        ! -name '*.pyo' \
        | sort
    )
  done

  find "${TARGET_PURELIB}" -type d -name __pycache__ -prune -exec rm -rf {} +
  find "${TARGET_PURELIB}" -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete

  for site_packages_dir in "${source_site_packages[@]}"; do
    rm -rf "${site_packages_dir}"
  done

  find "${INSTALL_BASE}/lib" -mindepth 1 -maxdepth 1 -type d \
    -name 'python*' ! -name "python${TARGET_PYTHON_VERSION}" -empty -delete
}

rewrite_pythonpath_hooks_for_target() {
  if [[ ! -d "${INSTALL_BASE}/share" ]]; then
    return 0
  fi

  local target_pythonpath="lib/python${TARGET_PYTHON_VERSION}/site-packages"
  local hook_file
  while IFS= read -r hook_file; do
    cat > "${hook_file}" <<EOF
# generated by colcon_rk3588a.sh for OHOS target Python

_colcon_prepend_unique_value PYTHONPATH "\$COLCON_CURRENT_PREFIX/${target_pythonpath}"
EOF
  done < <(find "${INSTALL_BASE}/share" -path '*/hook/pythonpath.sh' -type f | sort)

  while IFS= read -r hook_file; do
    cat > "${hook_file}" <<EOF
# generated by colcon_rk3588a.sh for OHOS target Python

ament_prepend_unique_value PYTHONPATH "\$AMENT_CURRENT_PREFIX/${target_pythonpath}"
EOF
  done < <(find "${INSTALL_BASE}/share" -path '*/environment/pythonpath.sh' -type f | sort)

  while IFS= read -r hook_file; do
    printf 'prepend-non-duplicate;PYTHONPATH;%s\n' "${target_pythonpath}" > "${hook_file}"
  done < <(find "${INSTALL_BASE}/share" \( -path '*/hook/pythonpath.dsv' -o -path '*/environment/pythonpath.dsv' \) -type f | sort)

  while IFS= read -r hook_file; do
    cat > "${hook_file}" <<EOF
# generated by colcon_rk3588a.sh for OHOS target Python

colcon_prepend_unique_value PYTHONPATH "\$env:COLCON_CURRENT_PREFIX/${target_pythonpath}"
EOF
  done < <(find "${INSTALL_BASE}/share" -path '*/hook/pythonpath.ps1' -type f | sort)

  while IFS= read -r hook_file; do
    cat > "${hook_file}" <<EOF
# generated by colcon_rk3588a.sh for OHOS target Python

ament_prepend_unique_value PYTHONPATH "\$env:AMENT_CURRENT_PREFIX/${target_pythonpath}"
EOF
  done < <(find "${INSTALL_BASE}/share" -path '*/environment/pythonpath.ps1' -type f | sort)
}

rewrite_generated_cmake_python_paths_for_target() {
  if [[ ! -d "${INSTALL_BASE}/share" ]]; then
    return 0
  fi

  local target_pythonpath="lib/python${TARGET_PYTHON_VERSION}/site-packages"
  local cmake_file
  while IFS= read -r cmake_file; do
    sed -i -E \
      "s#lib/python[0-9]+\\.[0-9]+/site-packages#${target_pythonpath}#g" \
      "${cmake_file}"
  done < <(
    find "${INSTALL_BASE}/share" -path '*/cmake/*.cmake' -type f -print0 | \
      xargs -0 -r grep -IlE 'lib/python[0-9]+\.[0-9]+/site-packages' | sort
  )
}

rewrite_console_scripts_for_ohos() {
  if [[ ! -d "${TARGET_PURELIB}" ]]; then
    return 0
  fi

  mkdir -p "${TARGET_SCRIPTS}"

  local entry_points_file
  while IFS= read -r entry_points_file; do
    local package_name=""
    local pkg_info="${entry_points_file%/entry_points.txt}/PKG-INFO"
    if [[ -f "${pkg_info}" ]]; then
      package_name="$(sed -n 's/^Name: //p' "${pkg_info}" | head -n 1)"
    fi

    local line script_name entry_target module_name function_name
    while IFS= read -r line; do
      script_name="${line%%=*}"
      entry_target="${line#*=}"
      script_name="$(printf '%s' "${script_name}" | xargs)"
      entry_target="$(printf '%s' "${entry_target}" | xargs)"
      if [[ -z "${script_name}" || "${entry_target}" != *:* ]]; then
        continue
      fi
      module_name="${entry_target%%:*}"
      function_name="${entry_target#*:}"
      write_ohos_wrapper "${TARGET_SCRIPTS}/${script_name}" "${script_name}" "${module_name}" "${function_name}"
      if [[ -n "${package_name}" ]]; then
        write_ohos_wrapper "${TARGET_SCRIPTS}/${package_name}__${script_name}" "${package_name}__${script_name}" "${module_name}" "${function_name}"
      fi
    done < <(
      awk '
        /^\[console_scripts\]$/ {in_section=1; next}
        /^\[/ {if (in_section) exit}
        in_section && NF {print}
      ' "${entry_points_file}"
    )
  done < <(find "${TARGET_PURELIB}" -maxdepth 2 -path '*/entry_points.txt' -type f | sort)
}

validate_target_python_hooks() {
  if [[ ! -d "${INSTALL_BASE}/share" ]]; then
    return 0
  fi

  local bad_hooks
  bad_hooks="$(
    find "${INSTALL_BASE}/share" \( -path '*/hook/pythonpath.*' -o -path '*/environment/pythonpath.*' \) -type f -print0 | \
      xargs -0 -r grep -Il 'python3\.[0-9]' | \
      xargs -r grep -L "python${TARGET_PYTHON_VERSION}" || true
  )"

  if [[ -n "${bad_hooks}" ]]; then
    echo "Found non-target Python hooks under ${INSTALL_BASE}/share:" >&2
    printf '%s\n' "${bad_hooks}" >&2
    return 1
  fi

  local bad_cmake_paths
  bad_cmake_paths="$(
    find "${INSTALL_BASE}/share" -path '*/cmake/*.cmake' -type f -print0 | \
      xargs -0 -r grep -nE 'lib/python[0-9]+\.[0-9]+/site-packages' | \
      grep -v "lib/python${TARGET_PYTHON_VERSION}/site-packages" || true
  )"

  if [[ -n "${bad_cmake_paths}" ]]; then
    echo "Found non-target Python CMake paths under ${INSTALL_BASE}/share:" >&2
    printf '%s\n' "${bad_cmake_paths}" >&2
    return 1
  fi

  local bad_python_dirs
  bad_python_dirs="$(
    find "${INSTALL_BASE}/lib" -mindepth 1 -maxdepth 1 -type d \
      -name 'python*' ! -name "python${TARGET_PYTHON_VERSION}" -print 2>/dev/null || true
  )"

  if [[ -n "${bad_python_dirs}" ]]; then
    echo "Found non-target Python directories under ${INSTALL_BASE}/lib:" >&2
    printf '%s\n' "${bad_python_dirs}" >&2
    return 1
  fi
}

postprocess_install_tree() {
  sync_python_artifacts_to_target_purelib
  rewrite_pythonpath_hooks_for_target
  rewrite_generated_cmake_python_paths_for_target
  rewrite_console_scripts_for_ohos
}

build_colcon_package() {
  local package_name="$1"
  local package_path="$2"

  rm -f "${BUILD_BASE}/${package_name}"/colcon_command_prefix_*.sh.env
  rm -f "${BUILD_BASE}/${package_name}/install.log"

  colcon build \
    --paths "${package_path}" \
    --build-base "${BUILD_BASE}" \
    --install-base "${INSTALL_BASE}" \
    --merge-install \
    --executor sequential \
    --event-handlers "${COLCON_EVENT_HANDLERS}" \
    --allow-overriding "${package_name}" \
    --cmake-clean-cache \
    --cmake-args \
      "-G" "Ninja" \
      "-DCMAKE_MAKE_PROGRAM=${NINJA_BIN}" \
      "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}" \
      "-DCMAKE_PROJECT_INCLUDE_BEFORE=${PYTHON_TARGETS_PRELUDE}" \
      "-DROS2_OHOS_COMMAND_LINE_TOOLS_ROOT=${COMMAND_LINE_TOOLS_ROOT}" \
      "-DROS2_OHOS_TARGET_PYTHON_INCLUDE_DIR=${TARGET_PYTHON_INCLUDE_DIR}" \
      "-DROS2_OHOS_TARGET_PYTHON_LIBRARY=${TARGET_PYTHON_LIBRARY}" \
      "-DROS2_OHOS_TARGET_NUMPY_INCLUDE_DIR=${TARGET_NUMPY_INCLUDE_DIR}" \
      "-DOHOS_ARCH=${OHOS_ARCH}" \
      "-DOHOS_STL=${OHOS_STL}" \
      "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" \
      "-DPython3_EXECUTABLE=${PYTHON_BIN}" \
      "-DBUILD_TESTING=OFF" \
      "${PACKAGE_DIR_ARGS[@]}"
}

build_direct_cmake_package() {
  local package_name="$1"
  local package_path="$2"
  local package_build_base="${BUILD_BASE}/${package_name}"
  local direct_cmake_prefix_path="${INSTALL_BASE}"
  local direct_pythonpath="${PYTHONPATH:-}"

  if [[ -n "${CMAKE_PREFIX_PATH_CMAKE}" ]]; then
    direct_cmake_prefix_path="${direct_cmake_prefix_path};${CMAKE_PREFIX_PATH_CMAKE}"
  fi
  if [[ -d "${TARGET_PURELIB}" ]]; then
    direct_pythonpath="${direct_pythonpath:+${direct_pythonpath}:}${TARGET_PURELIB}"
  fi

  rm -rf "${package_build_base}"
  mkdir -p "${package_build_base}" "${TARGET_PURELIB}"

  PYTHONPATH="${direct_pythonpath}" \
    "${CMAKE_BIN}" -S "${package_path}" -B "${package_build_base}" -G Ninja \
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
      -DCMAKE_INSTALL_PREFIX="${INSTALL_BASE}" \
      "-DCMAKE_PREFIX_PATH=${direct_cmake_prefix_path}" \
      -DPython3_EXECUTABLE="${PYTHON_BIN}" \
      -DBUILD_TESTING=OFF \
      "${PACKAGE_DIR_ARGS[@]}"

  PYTHONPATH="${direct_pythonpath}" \
    "${CMAKE_BIN}" --build "${package_build_base}" --target install -- -j"$(nproc)"
}

REQUESTED_PACKAGE_PATHS=()
for package_name in "${PACKAGES[@]}"; do
  package_path="$(
    colcon list \
      --base-paths "${ROOT_DIR}/src" \
      --packages-select "${package_name}" \
      --paths-only | head -n 1
  )"
  if [[ -z "${package_path}" ]]; then
    echo "package not found under ${ROOT_DIR}/src: ${package_name}" >&2
    exit 1
  fi
  REQUESTED_PACKAGE_PATHS+=("${package_path}")
done

while IFS= read -r package_path; do
  package_name="$(
    sed -n 's:.*<name>[[:space:]]*\([^<][^<]*\)[[:space:]]*</name>.*:\1:p' "${package_path}/package.xml" | head -n 1
  )"
  if [[ -z "${package_name}" ]]; then
    echo "failed to determine package name from ${package_path}/package.xml" >&2
    exit 1
  fi

  if grep -q '<build_type>ament_python</build_type>' "${package_path}/package.xml"; then
    install_ament_python_package "${package_name}" "${package_path}"
  elif [[ "${DIRECT_CMAKE_BUILD}" != "0" ]] && grep -q '<build_type>ament_cmake</build_type>' "${package_path}/package.xml"; then
    build_direct_cmake_package "${package_name}" "${package_path}"
  else
    build_colcon_package "${package_name}" "${package_path}"
  fi
  postprocess_install_tree
done < <(
  colcon list \
    --base-paths "${ROOT_DIR}/src" \
    --topological-order \
    --packages-select "${PACKAGES[@]}" \
    --paths-only
)

validate_target_python_hooks

echo "colcon RK3588A build complete:"
echo "  packages: ${PACKAGES[*]}"
echo "  install: ${INSTALL_BASE}"
echo "  target purelib: ${TARGET_PURELIB}"
