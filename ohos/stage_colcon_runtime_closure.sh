#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNDERLAY_PREFIX="${ROS2_OHOS_ROS2_PREFIX:-${ROOT_DIR}/install/ohos-ros2}"
OVERLAY_PREFIX="${ROS2_OHOS_COLCON_INSTALL_BASE:-${ROOT_DIR}/install/ohos-colcon-rk3588s}"
TARGET_PYTHON_VERSION="${ROS2_OHOS_TARGET_PYTHON_VERSION:-3.12}"
OVERLAY_SITE="${OVERLAY_PREFIX}/lib/python${TARGET_PYTHON_VERSION}/site-packages"
UNDERLAY_SITE="${UNDERLAY_PREFIX}/lib/python${TARGET_PYTHON_VERSION}/site-packages"

runtime_packages=(
  builtin_interfaces
  unique_identifier_msgs
  service_msgs
  action_msgs
  rcl_interfaces
  rosgraph_msgs
  statistics_msgs
  lifecycle_msgs
  composition_interfaces
  type_description_interfaces
  rosidl_runtime_py
  rosidl_parser
  rosidl_adapter
)

mkdir -p \
  "${OVERLAY_PREFIX}/lib" \
  "${OVERLAY_PREFIX}/share/ament_index/resource_index/packages" \
  "${OVERLAY_SITE}"

copy_tree_clean() {
  local src="$1"
  local dst_parent="$2"
  local base
  base="$(basename "${src}")"
  if [[ -e "${dst_parent}/${base}" || -L "${dst_parent}/${base}" ]]; then
    rm -rf "${dst_parent:?}/${base}"
  fi
  rsync -rlt --no-owner --no-group \
    --exclude '__pycache__/' \
    --exclude '*.pyc' \
    --exclude '*.pyo' \
    "${src}" "${dst_parent}/"
}

for pkg in "${runtime_packages[@]}"; do
  if [[ -d "${UNDERLAY_PREFIX}/share/${pkg}" ]]; then
    rsync -a "${UNDERLAY_PREFIX}/share/${pkg}" "${OVERLAY_PREFIX}/share/"
  fi
  if [[ -f "${UNDERLAY_PREFIX}/share/ament_index/resource_index/packages/${pkg}" ]]; then
    rsync -a \
      "${UNDERLAY_PREFIX}/share/ament_index/resource_index/packages/${pkg}" \
      "${OVERLAY_PREFIX}/share/ament_index/resource_index/packages/"
  fi
  if [[ -d "${UNDERLAY_SITE}/${pkg}" || -L "${UNDERLAY_SITE}/${pkg}" ]]; then
    copy_tree_clean "${UNDERLAY_SITE}/${pkg}" "${OVERLAY_SITE}"
  fi
  for egg in "${UNDERLAY_SITE}/${pkg}"-*.egg-info; do
    [[ -e "${egg}" || -L "${egg}" ]] || continue
    copy_tree_clean "${egg}" "${OVERLAY_SITE}"
  done
  for lib in "${UNDERLAY_PREFIX}/lib/lib${pkg}__"*.so; do
    [[ -e "${lib}" || -L "${lib}" ]] || continue
    rsync -a "${lib}" "${OVERLAY_PREFIX}/lib/"
  done
done

for lib in "${UNDERLAY_PREFIX}/lib/"*.so*; do
  [[ -e "${lib}" || -L "${lib}" ]] || continue
  rsync -a "${lib}" "${OVERLAY_PREFIX}/lib/"
done

if [[ -d "${UNDERLAY_PREFIX}/opt" ]]; then
  while IFS= read -r lib; do
    rsync -a "${lib}" "${OVERLAY_PREFIX}/lib/"
  done < <(find "${UNDERLAY_PREFIX}/opt" \( -type f -o -type l \) -name 'lib*.so*' | sort)
fi

copy_optional_pydep() {
  local src="$1"
  if [[ -e "${src}" || -L "${src}" ]]; then
    copy_tree_clean "${src}" "${OVERLAY_SITE}"
  fi
}

copy_optional_pydep /usr/lib/python3/dist-packages/packaging
copy_optional_pydep /usr/lib/python3/dist-packages/packaging-20.3.egg-info
copy_optional_pydep /usr/lib/python3/dist-packages/pyparsing.py
copy_optional_pydep /usr/lib/python3/dist-packages/pyparsing-2.4.6.egg-info
copy_optional_pydep /usr/lib/python3/dist-packages/catkin_pkg
copy_optional_pydep /usr/lib/python3/dist-packages/catkin_pkg-1.1.0.egg-info
copy_optional_pydep /usr/lib/python3/dist-packages/rospkg
copy_optional_pydep /usr/lib/python3/dist-packages/rosdistro
copy_optional_pydep /usr/lib/python3/dist-packages/rosdistro-1.0.1.egg-info
copy_optional_pydep /usr/lib/python3.8/distutils
copy_optional_pydep /usr/lib/python3/dist-packages/em.py
copy_optional_pydep /usr/lib/python3/dist-packages/empy-3.3.2.egg-info
copy_optional_pydep /usr/lib/python3/dist-packages/argcomplete
copy_optional_pydep /usr/lib/python3/dist-packages/argcomplete-1.8.1.egg-info
copy_optional_pydep /usr/lib/python3/dist-packages/yaml
# ament_copyright is required by `ros2 pkg create` (pure-python, stdlib-only);
# stage it from the workspace source into both the overlay and underlay so the
# create verb's entry point loads on-device.
copy_optional_pydep "${ROOT_DIR}/src/ament/ament_lint/ament_copyright/ament_copyright"
if [[ -d "${OVERLAY_SITE}/ament_copyright" ]]; then
  rm -f "${OVERLAY_SITE}/ament_copyright/SUMMARY.md"
  rsync -a --exclude SUMMARY.md "${OVERLAY_SITE}/ament_copyright" "${UNDERLAY_SITE}/"
fi
if [[ -d "${ROOT_DIR}/build/ohos-ros2/pydeps/lark" || -L "${ROOT_DIR}/build/ohos-ros2/pydeps/lark" ]]; then
  copy_tree_clean "${ROOT_DIR}/build/ohos-ros2/pydeps/lark" "${OVERLAY_SITE}"
fi

repair_overlay_package_manifests() {
  local index_dir="${OVERLAY_PREFIX}/share/ament_index/resource_index/packages"
  local resource
  local pkg
  [[ -d "${index_dir}" ]] || return 0
  for resource in "${index_dir}"/*; do
    [[ -f "${resource}" ]] || continue
    pkg="$(basename "${resource}")"
    if [[ ! -f "${OVERLAY_PREFIX}/share/${pkg}/package.xml" && -f "${UNDERLAY_PREFIX}/share/${pkg}/package.xml" ]]; then
      mkdir -p "${OVERLAY_PREFIX}/share/${pkg}"
      rsync -a "${UNDERLAY_PREFIX}/share/${pkg}/package.xml" "${OVERLAY_PREFIX}/share/${pkg}/package.xml"
    fi
  done
}

stage_local_rosdistro_index() {
  local rosdistro_dir="${OVERLAY_PREFIX}/share/rosdistro"
  mkdir -p "${rosdistro_dir}/jazzy"
  python3 - "${rosdistro_dir}" "${UNDERLAY_PREFIX}" "${OVERLAY_PREFIX}" <<'PY'
import os
import sys
import xml.etree.ElementTree as ET

rosdistro_dir = sys.argv[1]
prefixes = sys.argv[2:]


def quote(value):
    return "'" + str(value).replace("'", "''") + "'"


packages = {}
for prefix in prefixes:
    index_dir = os.path.join(prefix, 'share', 'ament_index', 'resource_index', 'packages')
    if not os.path.isdir(index_dir):
        continue
    for pkg in sorted(os.listdir(index_dir)):
        package_xml = os.path.join(prefix, 'share', pkg, 'package.xml')
        if not os.path.isfile(package_xml):
            continue
        try:
            root = ET.parse(package_xml).getroot()
            version = (root.findtext('version') or '0.0.0').strip()
        except ET.ParseError:
            version = '0.0.0'
        packages[pkg] = version or '0.0.0'

index_path = os.path.join(rosdistro_dir, 'index-v4.yaml')
distribution_path = os.path.join(rosdistro_dir, 'jazzy', 'distribution.yaml')

with open(index_path, 'w', encoding='utf-8') as index_file:
    index_file.write('type: index\n')
    index_file.write('version: 4\n')
    index_file.write('distributions:\n')
    index_file.write('  jazzy:\n')
    index_file.write('    distribution:\n')
    index_file.write('    - jazzy/distribution.yaml\n')
    index_file.write('    distribution_status: active\n')
    index_file.write('    distribution_type: ros2\n')
    index_file.write('    python_version: 3\n')

with open(distribution_path, 'w', encoding='utf-8') as distribution_file:
    distribution_file.write('type: distribution\n')
    distribution_file.write('version: 2\n')
    distribution_file.write('release_platforms:\n')
    distribution_file.write('  ohos:\n')
    distribution_file.write('  - rk3588a\n')
    distribution_file.write('repositories:\n')
    for pkg in sorted(packages):
        distribution_file.write(f'  {pkg}:\n')
        distribution_file.write('    release:\n')
        distribution_file.write('      type: git\n')
        distribution_file.write(f'      url: https://example.invalid/ros2/{pkg}.git\n')
        distribution_file.write(f'      version: {quote(packages[pkg])}\n')
        distribution_file.write('      tags:\n')
        distribution_file.write("        release: release/{package}/{version}\n")
PY
}

repair_overlay_package_manifests
stage_local_rosdistro_index

rsync -a "${ROOT_DIR}/ohos/python_stubs/psutil.py" "${OVERLAY_SITE}/psutil.py"
# Prefer the real target numpy staged in the underlay (Alpine musl aarch64 build,
# extensions renamed to the -ohos suffix). The pure-python stub crashes the
# generated *_s.c typesupport conversion for fixed-size arrays (e.g. action
# goal UUIDs) because PyArray_DATA is called on a non-ndarray in Release builds.
if [[ -f "${UNDERLAY_PREFIX}/lib/python3.12/site-packages/numpy/core/__init__.py" ]]; then
  rm -rf "${OVERLAY_SITE}/numpy"
else
  mkdir -p "${OVERLAY_SITE}/numpy"
  rsync -a "${ROOT_DIR}/ohos/python_stubs/numpy/__init__.py" "${OVERLAY_SITE}/numpy/__init__.py"
fi

find "${OVERLAY_SITE}" -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete
find "${OVERLAY_SITE}" -type d -name __pycache__ -empty -delete

echo "Colcon overlay runtime closure staged:"
echo "  underlay: ${UNDERLAY_PREFIX}"
echo "  overlay: ${OVERLAY_PREFIX}"
echo "  packages: $(find "${OVERLAY_PREFIX}/share/ament_index/resource_index/packages" -mindepth 1 -maxdepth 1 -type f | wc -l)"
