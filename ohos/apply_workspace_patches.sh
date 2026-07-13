#!/usr/bin/env bash
# codex-file-meta: begin
# relative_path: "ohos/apply_workspace_patches.sh"
# language: "shell"
# summary: "Shell script defining `apply_patch_file`."
# symbols: ["apply_patch_file"]
# generated_by: "codebase-frontmatter-summary"
# codex-file-meta: end

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
apply_patch_file "${ROOT_DIR}/src/ros2/geometry2" \
  "${PATCH_DIR}/0006-geometry2-static-transform-publisher-shutdown-help.patch"
apply_patch_file "${ROOT_DIR}/src/ros2/geometry2" \
  "${PATCH_DIR}/0007-geometry2-ohos-python-targets.patch"
apply_patch_file "${ROOT_DIR}/src/ros2/rmw_implementation" \
  "${PATCH_DIR}/0008-rmw-implementation-destroy-qos-test-entities.patch"
apply_patch_file "${ROOT_DIR}/src/ros2/rcl" \
  "${ROOT_DIR}/ohos/patches_full/ros2_rcl.patch"
apply_patch_file "${ROOT_DIR}/src/ros2/rclcpp" \
  "${ROOT_DIR}/ohos/patches_full/ros2_rclcpp.patch"
apply_patch_file "${ROOT_DIR}/src/ros2/rosbag2" \
  "${ROOT_DIR}/ohos/patches_full/ros2_rosbag2.patch"
