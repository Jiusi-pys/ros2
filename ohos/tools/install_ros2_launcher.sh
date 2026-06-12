#!/usr/bin/env bash
# Install a global `ros2` launcher on an RK3588A/OHOS board so the CLI is
# callable as plain `ros2 <verb>` (e.g. `ros2 node list`) from any shell,
# instead of the full /data/local/tmp/ohos-colcon-rk3588a/bin/ros2 path.
#
# The board PATH is `/usr/local/bin:/bin:/usr/bin`; all live on the read-only
# root ext4 partition (mmcblk0p6). We remount / rw, drop a tiny launcher at
# /usr/local/bin/ros2 (the first PATH entry), then remount ro. The launcher
# `exec`s the real overlay wrapper by ABSOLUTE path — a symlink would break the
# wrapper's `dirname $0`-based PREFIX detection and resolve PREFIX to "/".
#
# Usage: install_ros2_launcher.sh <device_id> [overlay_prefix]
set -euo pipefail

DEV="${1:?device id required}"
OVERLAY="${2:-/data/local/tmp/ohos-colcon-rk3588a}"
WRAPPER="${OVERLAY}/bin/ros2"
TARGET_DIR="/usr/local/bin"
TARGET="${TARGET_DIR}/ros2"
TMP_REMOTE="/data/local/tmp/.ros2_launcher"

hdc -t "${DEV}" shell "test -x ${WRAPPER}" >/dev/null 2>&1 \
  || { echo "wrapper missing or not executable: ${WRAPPER}" >&2; exit 1; }

LOCAL_TMP="$(mktemp)"
trap 'rm -f "${LOCAL_TMP}"' EXIT
cat > "${LOCAL_TMP}" <<EOF
#!/bin/sh
# Global ros2 launcher -> standalone OHOS overlay wrapper (absolute path so the
# wrapper's dirname-based PREFIX detection stays correct).
exec ${WRAPPER} "\$@"
EOF

hdc -t "${DEV}" file send "${LOCAL_TMP}" "${TMP_REMOTE}" >/dev/null
# The device-side script must fail loudly: hdc shell often returns 0 even when a
# remote command failed, so we gate on an explicit INSTALL_OK marker rather than
# the hdc exit status. ro,remount failing is non-fatal (the file is already
# written and persists) but is surfaced as a warning instead of silently passing.
# hdc on this host intermittently segfaults AFTER emitting valid device output,
# so its exit status is unreliable; gate purely on the device-emitted INSTALL_OK
# marker and keep `|| true` so a host-side hdc crash doesn't abort us under set -e.
out=$(hdc -t "${DEV}" shell "
mount -o rw,remount / 2>&1 || { echo REMOUNT_RW_FAIL; exit 1; }
mkdir -p ${TARGET_DIR} || { echo MKDIR_FAIL; exit 1; }
cp ${TMP_REMOTE} ${TARGET} || { echo CP_FAIL; exit 1; }
chmod 755 ${TARGET} || { echo CHMOD_FAIL; exit 1; }
[ -x ${TARGET} ] || { echo VERIFY_FAIL; exit 1; }
mount -o ro,remount / 2>/dev/null || echo REMOUNT_RO_WARN
rm -f ${TMP_REMOTE}
echo INSTALL_OK
" 2>/dev/null || true)
echo "${out}"
case "${out}" in
  *INSTALL_OK*) echo "ros2 launcher installed on ${DEV} at ${TARGET}";;
  *) echo "ros2 launcher install FAILED on ${DEV} (${out})" >&2; exit 1;;
esac
case "${out}" in *REMOUNT_RO_WARN*) echo "warning: / left writable on ${DEV} (ro,remount failed)" >&2;; esac
