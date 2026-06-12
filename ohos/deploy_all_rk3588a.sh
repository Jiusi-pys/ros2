#!/usr/bin/env bash
# One-shot reproducible deploy of the full ROS 2 stack to an RK3588A/OHOS board
# from the host install/ prefixes. Wipes the prior on-device ROS 2 deployment
# and re-creates it entirely from build artifacts + scripts — no hand-patching.
#
# Deploys:
#   1. standalone underlay   install/ohos-ros2          -> /data/local/tmp/ohos-prefix   (chunked)
#   2. FastDDS prefix        install/ohos-fastdds        -> /data/local/tmp/ohos-fastdds
#   3. colcon overlay        install/ohos-colcon-rk3588a -> /data/local/tmp/ohos-colcon-rk3588a
#   4. validation scripts    ohos/tools/*               -> /data/local/tmp/ros2-validate
#   5. global launcher       /usr/local/bin/ros2        (via install_ros2_launcher.sh)
#
# Usage: deploy_all_rk3588a.sh <device_id> [--wipe]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEV="${1:?device id required}"
WIPE="${2:-}"
INSTALL="${ROOT_DIR}/install"
REMOTE="/data/local/tmp"

UNDERLAY="${INSTALL}/ohos-ros2"
FASTDDS="${INSTALL}/ohos-fastdds"
OVERLAY="${INSTALL}/ohos-colcon-rk3588a"

for d in "${UNDERLAY}" "${OVERLAY}"; do
  [ -d "${d}" ] || { echo "missing prefix: ${d}" >&2; exit 1; }
done

# device-side success is gated on emitted markers (hdc host exit status is
# unreliable — it segfaults after valid output), so wrap captures with || true.
dev_sh() { hdc -t "${DEV}" shell "$1" 2>/dev/null || true; }

deploy_tar() {  # deploy_tar <host_dir> <remote_name>
  # separate `local` stmts: a single `local a=$1 b=${a}` expands all RHS before
  # assignment, so ${name} would be unbound under set -u
  local host_dir="$1"
  local name="$2"
  local tgz="/tmp/${name}.deploy.tgz"
  echo "  packaging ${name}..."
  tar czf "${tgz}" -C "$(dirname "${host_dir}")" "$(basename "${host_dir}")"
  hdc -t "${DEV}" file send "${tgz}" "${REMOTE}/${name}.tgz" >/dev/null
  local out
  out=$(dev_sh "cd ${REMOTE} && rm -rf ${name} && tar xzf ${name}.tgz && rm -f ${name}.tgz && [ -d ${name} ] && echo ${name}_OK")
  echo "${out}" | grep -q "${name}_OK" || { echo "  FAILED: ${name}" >&2; return 1; }
  echo "  ${name} deployed"
  rm -f "${tgz}"
}

echo "=== deploy_all_rk3588a -> ${DEV} ==="

if [ "${WIPE}" = "--wipe" ]; then
  echo "wiping prior deployment..."
  dev_sh "rm -rf ${REMOTE}/ohos-prefix ${REMOTE}/ohos-fastdds ${REMOTE}/ohos-colcon-rk3588a ${REMOTE}/ros2-validate ${REMOTE}/valcli ${REMOTE}/valext ${REMOTE}/val ${REMOTE}/roslogs ${REMOTE}/ohos-prefix-chunked* ${REMOTE}/*.tgz" >/dev/null
fi

# 1. underlay (chunked, robust over flaky hdc)
echo "[1/5] underlay (chunked)..."
OHOS_DEVICE_ID="${DEV}" ROS2_OHOS_PREFIX_DIR="${UNDERLAY}" \
  bash "${ROOT_DIR}/ohos/deploy_ros2_prefix_chunked.sh" "${DEV}" >/tmp/deploy_underlay_${DEV}.log 2>&1 \
  && grep -q ros2_prefix_chunked_deploy_ok /tmp/deploy_underlay_${DEV}.log \
  && echo "  underlay OK" || { echo "  underlay deploy FAILED (see /tmp/deploy_underlay_${DEV}.log)" >&2; exit 1; }

# numpy's _multiarray_umath.so (Alpine musl) DT_NEEDEDs libc.musl-aarch64.so.1.
# That is a DEVICE-specific symlink to the on-device musl libc; it is NOT shipped
# in the host tarball (an absolute symlink escaping the prefix makes toybox tar
# fail the whole extraction), so create it on-device after extraction.
echo "  linking libc.musl-aarch64.so.1 (numpy dep)..."
dev_sh "ln -sf /lib/ld-musl-aarch64.so.1 ${REMOTE}/ohos-prefix/lib/libc.musl-aarch64.so.1; [ -e ${REMOTE}/ohos-prefix/lib/libc.musl-aarch64.so.1 ] && echo MUSL_LINK_OK" | grep -q MUSL_LINK_OK \
  && echo "  musl link OK" || { echo "  musl link FAILED" >&2; exit 1; }

# 2. fastdds prefix (optional — runtime libs already merged into underlay lib/)
echo "[2/5] fastdds prefix..."
if [ -d "${FASTDDS}" ]; then deploy_tar "${FASTDDS}" ohos-fastdds; else echo "  (skipped, no ohos-fastdds)"; fi

# 3. overlay
echo "[3/5] colcon overlay..."
deploy_tar "${OVERLAY}" ohos-colcon-rk3588a

# 4. validation scripts
echo "[4/5] validation scripts..."
hdc -t "${DEV}" shell "mkdir -p ${REMOTE}/ros2-validate" >/dev/null 2>&1 || true
tar czf /tmp/ros2-validate.deploy.tgz -C "${ROOT_DIR}/ohos/tools" \
  rk3588a_validate_all.sh rk3588a_validate_ext.sh rk3588a_validate_cli.sh rk3588a_bag_lanes.sh \
  run_rclcpp_service_roundtrip.sh run_rclcpp_action_binary_roundtrip.sh run_rclcpp_action_roundtrip.sh \
  run_tf2_static_echo_roundtrip.sh run_composition_dlopen_roundtrip.sh run_component_cli_roundtrip.sh \
  run_urdf_robot_state_publisher_probe.sh rclpy_cli_node.py rclpy_cli_service.py rclpy_cli_action.py
hdc -t "${DEV}" file send /tmp/ros2-validate.deploy.tgz "${REMOTE}/ros2-validate.tgz" >/dev/null
dev_sh "cd ${REMOTE} && rm -rf ros2-validate && mkdir ros2-validate && tar xzf ros2-validate.tgz -C ros2-validate && rm -f ros2-validate.tgz && echo VAL_OK" | grep -q VAL_OK \
  && echo "  validation scripts OK" || { echo "  validation scripts FAILED" >&2; exit 1; }
rm -f /tmp/ros2-validate.deploy.tgz

# 5. global launcher
echo "[5/5] global ros2 launcher..."
bash "${ROOT_DIR}/ohos/tools/install_ros2_launcher.sh" "${DEV}" >/tmp/deploy_launcher_${DEV}.log 2>&1 \
  && grep -q "launcher installed" /tmp/deploy_launcher_${DEV}.log \
  && echo "  launcher OK" || { echo "  launcher FAILED (see /tmp/deploy_launcher_${DEV}.log)" >&2; exit 1; }

# verification
echo "=== verifying deployment ==="
ver=$(dev_sh "
echo -n 'ros2_path: '; command -v ros2
echo -n 'underlay_rcl: '; [ -f ${REMOTE}/ohos-prefix/lib/librcl.so ] && echo OK || echo MISSING
echo -n 'fastrtps: '; [ -f ${REMOTE}/ohos-prefix/lib/libfastrtps.so.2.14.6 ] && echo OK || echo MISSING
echo -n 'numpy: '; [ -f ${REMOTE}/ohos-prefix/lib/python3.12/site-packages/numpy/core/_multiarray_umath.cpython-312-aarch64-linux-ohos.so ] && echo OK || echo MISSING
echo -n 'ament_copyright: '; [ -d ${REMOTE}/ohos-prefix/lib/python3.12/site-packages/ament_copyright ] && echo OK || echo MISSING
echo -n 'zstd_plugin: '; [ -f ${REMOTE}/ohos-prefix/lib/librosbag2_compression_zstd.so ] && echo OK || echo MISSING
echo -n 'overlay_resources: '; ls ${REMOTE}/ohos-colcon-rk3588a/share/ament_index/resource_index/packages 2>/dev/null | wc -l
")
echo "${ver}"
echo "=== deploy_all_rk3588a -> ${DEV} COMPLETE ==="
