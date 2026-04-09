#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH_DIR="${ROOT_DIR}/ohos/patches"

apply_patch_file() {
  local repo_dir="$1"
  local patch_file="$2"

  if git -C "${repo_dir}" apply --check "${patch_file}" >/dev/null 2>&1; then
    git -C "${repo_dir}" apply "${patch_file}"
    return 0
  fi

  if git -C "${repo_dir}" apply --reverse --check "${patch_file}" >/dev/null 2>&1; then
    echo "Patch already applied: ${patch_file}" >&2
    return 0
  fi

  echo "Failed to apply patch ${patch_file} in ${repo_dir}" >&2
  return 1
}

apply_patch_file "${ROOT_DIR}/src/ament/ament_cmake" \
  "${PATCH_DIR}/0001-ament_cmake-pass-cmake-make-program.patch"
apply_patch_file "${ROOT_DIR}/src/ros2/rcutils" \
  "${PATCH_DIR}/0002-rcutils-handle-ohos-musl-strerror.patch"
apply_patch_file "${ROOT_DIR}/src/ros2/rosidl_typesupport_fastrtps" \
  "${PATCH_DIR}/0003-rosidl_typesupport_fastrtps-fix-standalone-fastdds-prefix.patch"
apply_patch_file "${ROOT_DIR}/src/ros2/rmw_fastrtps" \
  "${PATCH_DIR}/0004-rmw_fastrtps-fix-shared-topic-cleanup.patch"
apply_patch_file "${ROOT_DIR}/src/eProsima/foonathan_memory_vendor" \
  "${PATCH_DIR}/0005-foonathan_memory_vendor-pass-cmake-make-program.patch"
