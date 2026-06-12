#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <package-source-dir>" >&2
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_DIR="$(realpath "$1")"

DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS_4.1"
if [[ ! -d "${DEFAULT_OHOS_ROOT}/command-line-tools" && -d "/home/kaihong/M-DDS/command-line-tools" ]]; then
  DEFAULT_OHOS_ROOT="/home/kaihong/M-DDS"
fi

COMMAND_LINE_TOOLS_ROOT="${ROS2_OHOS_COMMAND_LINE_TOOLS_ROOT:-${DEFAULT_OHOS_ROOT}/command-line-tools}"
PYTHON_BIN="${ROS2_OHOS_PYTHON_HOST:-${COMMAND_LINE_TOOLS_ROOT}/sdk/default/openharmony/native/llvm/python3/bin/python3}"
PREFIX="${ROS2_OHOS_ROS2_PREFIX:-${ROOT_DIR}/install/ohos-ros2}"
TARGET_PYTHON_VERSION="${ROS2_OHOS_TARGET_PYTHON_VERSION:-3.12}"
TARGET_PURELIB="${PREFIX}/lib/python${TARGET_PYTHON_VERSION}/site-packages"
TARGET_SCRIPTS="${PREFIX}/bin"
TARGET_PYTHON_BIN="${ROS2_OHOS_TARGET_PYTHON_BIN:-/data/local/release/usr/bin/python3.12}"
FALLBACK_PURELIB="${PREFIX}/lib/python3.11/site-packages"

if [[ ! -x "${PYTHON_BIN}" ]]; then
  echo "python3 not found at ${PYTHON_BIN}" >&2
  exit 1
fi
if [[ ! -f "${SOURCE_DIR}/setup.py" ]]; then
  echo "setup.py not found in ${SOURCE_DIR}" >&2
  exit 1
fi

package_name="$(
  sed -n 's:.*<name>[[:space:]]*\([^<][^<]*\)[[:space:]]*</name>.*:\1:p' "${SOURCE_DIR}/package.xml" | head -n 1
)"
if [[ -z "${package_name}" ]]; then
  echo "Failed to determine package name from ${SOURCE_DIR}/package.xml" >&2
  exit 1
fi

mkdir -p "${TARGET_PURELIB}" "${TARGET_SCRIPTS}"

(
  cd "${SOURCE_DIR}"
  "${PYTHON_BIN}" setup.py install \
    --prefix "${PREFIX}" \
    --install-lib "${TARGET_PURELIB}" \
    --install-scripts "${TARGET_SCRIPTS}" \
    --no-compile \
    --single-version-externally-managed \
    --record /tmp/ros2-python-package-record.txt
)

entry_points_file="$(
  find "${TARGET_PURELIB}" -maxdepth 2 -path "${TARGET_PURELIB}/${package_name}-*.egg-info/entry_points.txt" | head -n 1
)"

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
if [ -n "\${LD_PRELOAD:-}" ]; then
  export LD_PRELOAD="${TARGET_PYTHON_BIN%/bin/python3.12}/lib/libpython3.12.so.1.0 \${LD_PRELOAD}"
else
  export LD_PRELOAD="${TARGET_PYTHON_BIN%/bin/python3.12}/lib/libpython3.12.so.1.0"
fi
export PYTHONHOME="${TARGET_PYTHON_BIN%/bin/python3.12}"
export LD_LIBRARY_PATH="\${PREFIX}/lib\${VENDOR_LIB_PATH:+:\${VENDOR_LIB_PATH}}:/data/local/tmp:/data/local/release/usr/lib\${LD_LIBRARY_PATH:+:\${LD_LIBRARY_PATH}}"
export AMENT_PREFIX_PATH="\${PREFIX}\${AMENT_PREFIX_PATH:+:\${AMENT_PREFIX_PATH}}"
export CMAKE_PREFIX_PATH="\${PREFIX}\${CMAKE_PREFIX_PATH:+:\${CMAKE_PREFIX_PATH}}"
export COLCON_PREFIX_PATH="\${PREFIX}\${COLCON_PREFIX_PATH:+:\${COLCON_PREFIX_PATH}}"
export PYTHONPATH="\${PREFIX}/lib/python${TARGET_PYTHON_VERSION}/site-packages:\${PREFIX}/lib/python3.11/site-packages\${PYTHONPATH:+:\${PYTHONPATH}}"
exec "${TARGET_PYTHON_BIN}" -c 'import sys; sys.argv[0]="${argv0_name}"; from ${module_name} import ${function_name} as _entry; sys.exit(_entry())' "\$@"
EOF
  chmod 755 "${wrapper_path}"
}

if [[ -f "${entry_points_file}" ]]; then
  while IFS= read -r line; do
    script_name="${line%%=*}"
    entry_target="${line#*=}"
    script_name="$(printf '%s' "${script_name}" | xargs)"
    entry_target="$(printf '%s' "${entry_target}" | xargs)"
    module_name="${entry_target%%:*}"
    function_name="${entry_target#*:}"
    script_path="${TARGET_SCRIPTS}/${script_name}"
    alias_script_path="${TARGET_SCRIPTS}/${package_name}__${script_name}"
    write_ohos_wrapper "${script_path}" "${script_name}" "${module_name}" "${function_name}"
    write_ohos_wrapper "${alias_script_path}" "${package_name}__${script_name}" "${module_name}" "${function_name}"
  done < <(
    awk '
      /^\[console_scripts\]$/ {in_section=1; next}
      /^\[/ {if (in_section) exit}
      in_section && NF {print}
    ' "${entry_points_file}"
  )
else
  while IFS= read -r record_path; do
    if [[ "${record_path}" != "${TARGET_SCRIPTS}/"* ]]; then
      continue
    fi
    if [[ -f "${record_path}" ]] && head -n 1 "${record_path}" | grep -q '^#!'; then
      sed -i "1c #!${TARGET_PYTHON_BIN}" "${record_path}"
    fi
  done < /tmp/ros2-python-package-record.txt
fi

echo "ROS 2 Python package installed:"
echo "  source: ${SOURCE_DIR}"
echo "  purelib: ${TARGET_PURELIB}"
