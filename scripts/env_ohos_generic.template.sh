#!/bin/sh
# Generic ROS 2 environment for KaihongOS/RK3588A.
# deploy_ohos_generic.sh prepends immutable release/provenance assignments.

export ROS2_HOME="${ROS2_HOME:-/data/local/tmp/ros2-generic}"

fail_env() {
  printf 'ERROR: %s\n' "$1" >&2
}

if [ -z "${ROS2_DEPLOY_EXPECTED_MARKER:-}" ] || \
   [ ! -f "$ROS2_HOME/.ros2_deploy_complete" ] || \
   [ -L "$ROS2_HOME/.ros2_deploy_complete" ] || \
   [ "$(cat "$ROS2_HOME/.ros2_deploy_complete" 2>/dev/null)" != "$ROS2_DEPLOY_EXPECTED_MARKER" ]; then
  fail_env "incomplete or mixed generic ROS 2 deployment at $ROS2_HOME"
  return 70 2>/dev/null || exit 70
fi

if [ -z "${ROS2_RELEASE_PROVENANCE_SHA256:-}" ] || \
   [ ! -f "$ROS2_HOME/release_provenance.json" ] || \
   [ -L "$ROS2_HOME/release_provenance.json" ] || \
   [ "$(sha256sum "$ROS2_HOME/release_provenance.json" 2>/dev/null | cut -d ' ' -f1)" != "$ROS2_RELEASE_PROVENANCE_SHA256" ]; then
  fail_env "release provenance record is missing or has the wrong digest"
  return 70 2>/dev/null || exit 70
fi

if [ -z "${ROS2_BUILD_RECEIPT_SHA256:-}" ] || \
   [ ! -f "$ROS2_HOME/build_receipt.json" ] || \
   [ -L "$ROS2_HOME/build_receipt.json" ] || \
   [ "$(sha256sum "$ROS2_HOME/build_receipt.json" 2>/dev/null | cut -d ' ' -f1)" != "$ROS2_BUILD_RECEIPT_SHA256" ]; then
  fail_env "clean-build receipt is missing or has the wrong digest"
  return 70 2>/dev/null || exit 70
fi

case "${RMW_IMPLEMENTATION:-}" in
  rmw_fastrtps_cpp|rmw_cyclonedds_cpp) ;;
  *)
    fail_env "deployment did not pin an accepted generic RMW implementation"
    return 70 2>/dev/null || exit 70
    ;;
esac

# The verified profile must not silently resolve a missing DSO from an older
# ROS installation or inherit an unrecorded preloaded library / DDS XML file.
unset LD_PRELOAD FASTRTPS_DEFAULT_PROFILES_FILE FASTDDS_DEFAULT_PROFILES_FILE CYCLONEDDS_URI \
  PYTHONHOME PYTHONUSERBASE _PYTHON_SYSCONFIGDATA_NAME _PYTHON_PROJECT_BASE
export LD_LIBRARY_PATH="$ROS2_HOME/Lib"
export AMENT_PREFIX_PATH="$ROS2_HOME"
export RCUTILS_COLORIZED_OUTPUT=0
export RCUTILS_CONSOLE_OUTPUT_FORMAT="[{severity}] [{name}]: {message}"
# Keep the caller's HOME identity intact.  ROS-specific writable state belongs
# under this deployment instead of silently repurposing a process-wide account
# directory used by Python, LTTng and unrelated tools.
export ROS_HOME="$ROS2_HOME/.ros"
export ROS_LOG_DIR="$ROS2_HOME/log"
export PATH="$ROS2_HOME/bin:$PATH"
export ROS2_CLI_WRAPPER="$ROS2_HOME/env.sh:generic-ros2"

# This release profile selects UDPv4; SHM remains an experimental opt-in that
# requires separate acceptance evidence.
export FASTDDS_BUILTIN_TRANSPORTS=UDPv4

# Tracing is supported through the deployed LTTng tools, but merely sourcing
# ROS 2 must not start or kill a process.  The tracing acceptance gate owns the
# session daemon it launches and cleans up that exact PID.
export LTTNG_SESSION_CONFIG_XSD_PATH="$ROS2_HOME/share/xml/lttng"
export LTTNG_CONSUMERD64_BIN="$ROS2_HOME/Lib/lttng/libexec/lttng-consumerd"
export LTTNG_CONSUMERD64_LIBDIR="$ROS2_HOME/Lib"

# GUI support on these boards is headless/offscreen.  Visible display output
# remains outside the accepted profile until a display-server gate is added.
export QT_PLUGIN_PATH="$ROS2_HOME/plugins"
export QT_QPA_PLATFORM=offscreen

export ROS2_TALKER_RAW="$ROS2_HOME/Lib/demo_nodes_cpp/talker"
export ROS2_LISTENER_RAW="$ROS2_HOME/Lib/demo_nodes_cpp/listener"
export ROS2_PY_TALKER_RAW="$ROS2_HOME/Lib/demo_nodes_py/talker-script.py"
export ROS2_PY_LISTENER_RAW="$ROS2_HOME/Lib/demo_nodes_py/listener-script.py"

PY_RUNTIME_PREFIX="${ROS2_PYTHON_REMOTE_PREFIX:-}"
PY_OVERLAY="${ROS2_PYTHON_REMOTE_OVERLAY:-}"
if ! printf '%s\n' "$PY_RUNTIME_PREFIX" | grep -Eq '^/data/[A-Za-z0-9._+-]+$'; then
  fail_env "invalid provenance-bound Python runtime prefix"
  return 70 2>/dev/null || exit 70
fi
if [ "$PY_OVERLAY" != "$PY_RUNTIME_PREFIX/ros2-site-packages" ]; then
  fail_env "Python overlay path is not bound to the runtime prefix"
  return 70 2>/dev/null || exit 70
fi
PY312="$PY_RUNTIME_PREFIX/usr"
export OPENSSL_CONF="$PY312/etc/ssl/openssl.cnf"
export OPENSSL_MODULES="$PY312/lib/ossl-modules"
export SSL_CERT_FILE="$PY312/etc/ssl/cert.pem"
export SSL_CERT_DIR="$PY312/etc/ssl/certs"
if [ ! -x "$PY312/bin/python3.12" ] || [ -L "$PY312/bin/python3.12" ] || \
   [ ! -f "$PY312/lib/libpython3.12.so.1.0" ] || [ -L "$PY312/lib/libpython3.12.so.1.0" ]; then
  fail_env "verified CPython 3.12 runtime is missing at $PY312"
  return 70 2>/dev/null || exit 70
fi
if [ -z "${ROS2_PYTHON_RUNTIME_LIBRARY_SHA256:-}" ] || \
   [ "$(sha256sum "$PY312/lib/libpython3.12.so.1.0" 2>/dev/null | cut -d ' ' -f1)" != "$ROS2_PYTHON_RUNTIME_LIBRARY_SHA256" ]; then
  fail_env "libpython differs from the release Python lock"
  return 70 2>/dev/null || exit 70
fi
PY_RUNTIME_MARKER="$PY_RUNTIME_PREFIX/PYTHON_RUNTIME_DEPLOYMENT.json"
PY_ARTIFACT_MARKER="$PY_RUNTIME_PREFIX/PYTHON_RUNTIME_ARTIFACT.manifest.json"
PY_OVERLAY_MARKER="$PY_OVERLAY/.ros2-ohos-python-deployment.json"
for py_marker in "$PY_RUNTIME_MARKER" "$PY_ARTIFACT_MARKER" "$PY_OVERLAY_MARKER" \
  "$PY_OVERLAY/.ros2-ohos-python-stage.json" \
  "$PY_OVERLAY/.ros2-ohos-python-files.sha256" \
  "$PY_OVERLAY/.ros2-ohos-python-paths.txt"; do
  if [ ! -f "$py_marker" ] || [ -L "$py_marker" ]; then
    fail_env "Python provenance or full-tree manifest is missing: $py_marker"
    return 70 2>/dev/null || exit 70
  fi
done
if ! PYTHONNOUSERSITE=1 LD_LIBRARY_PATH="$PY312/lib" \
  LD_PRELOAD="$PY312/lib/libpython3.12.so.1.0" "$PY312/bin/python3.12" -I -B - \
  "$PY_RUNTIME_MARKER" "$PY_ARTIFACT_MARKER" "$PY_OVERLAY_MARKER" \
  "$PY_RUNTIME_PREFIX" "$PY_OVERLAY" "$ROS2_PYTHON_LOCK_SHA256" \
  "$ROS2_PYTHON_RUNTIME_ARCHIVE_SHA256" "$ROS2_PYTHON_RUNTIME_TREE_SHA256" \
  "$ROS2_PYTHON_RUNTIME_ENTRY_COUNT" "$ROS2_PYTHON_RUNTIME_PAYLOAD_BYTES" \
  "$ROS2_PYTHON_RUNTIME_INTERFACE_SHA256" "$ROS2_PYTHON_STAGE_MARKER_SHA256" \
  "$ROS2_PYTHON_STAGE_TREE_SHA256" "$ROS2_PYTHON_PROVENANCE_MODE" \
  "${ROS2_PYTHON_SOURCE_BUILD_RECEIPT_SHA256:-NOT_APPLICABLE}" \
  "${ROS2_PYTHON_SOURCE_LOCK_SHA256:-NOT_APPLICABLE}" \
  "${ROS2_PYTHON_SOURCE_BUILD_RECIPE_SHA256:-NOT_APPLICABLE}" <<'PY'
import hashlib
import json
import pathlib
import sys

(runtime_path, artifact_path, overlay_path, prefix, overlay, lock_sha,
 archive_sha, runtime_tree, runtime_entries, runtime_bytes, interface_sha,
 stage_marker_sha, stage_tree, provenance_mode,
 source_receipt_sha, source_lock_sha, source_recipe_sha) = sys.argv[1:]
runtime = json.loads(pathlib.Path(runtime_path).read_text(encoding="utf-8"))
artifact = json.loads(pathlib.Path(artifact_path).read_text(encoding="utf-8"))
overlay_marker = json.loads(pathlib.Path(overlay_path).read_text(encoding="utf-8"))
assert runtime.get("complete") is True
assert runtime.get("remote_runtime_prefix") == prefix
assert runtime.get("python_lock_sha256") == lock_sha
assert runtime.get("python_runtime_archive_sha256") == archive_sha
assert runtime.get("python_runtime_tree_sha256") == runtime_tree
assert runtime.get("python_runtime_entry_count") == int(runtime_entries)
assert runtime.get("python_runtime_payload_bytes") == int(runtime_bytes)
assert runtime.get("runtime_provenance_mode") == provenance_mode
assert artifact.get("python_lock_sha256") == lock_sha
assert artifact.get("archive", {}).get("sha256") == archive_sha
assert artifact.get("runtime_tree_sha256") == runtime_tree
assert artifact.get("runtime_entry_count") == int(runtime_entries)
assert artifact.get("runtime_payload_bytes") == int(runtime_bytes)
assert artifact.get("provenance_mode") == provenance_mode
assert overlay_marker.get("complete") is True
assert overlay_marker.get("remote_runtime_prefix") == prefix
assert overlay_marker.get("remote_overlay") == overlay
assert overlay_marker.get("python_lock_sha256") == lock_sha
assert overlay_marker.get("python_runtime_archive_sha256") == archive_sha
assert overlay_marker.get("python_runtime_tree_sha256") == runtime_tree
assert overlay_marker.get("python_runtime_interface_sha256") == interface_sha
assert overlay_marker.get("python_stage_marker_sha256") == stage_marker_sha
assert overlay_marker.get("python_stage_tree_sha256") == stage_tree
assert overlay_marker.get("runtime_provenance_mode") == provenance_mode
if provenance_mode == "source-reproducible":
    source_path = pathlib.Path(prefix) / "PYTHON_SOURCE_BUILD_RECEIPT.json"
    assert source_path.is_file() and not source_path.is_symlink()
    assert hashlib.sha256(source_path.read_bytes()).hexdigest() == source_receipt_sha
    source = json.loads(source_path.read_text(encoding="utf-8"))
    assert artifact.get("source_build_receipt", {}).get("sha256") == source_receipt_sha
    assert source.get("python_source_lock_sha256") == source_lock_sha
    assert source.get("build_recipe_sha256") == source_recipe_sha
    for control in (runtime, overlay_marker):
        assert control.get("python_source_build_receipt_sha256") == source_receipt_sha
        assert control.get("python_source_lock_sha256") == source_lock_sha
        assert control.get("python_source_build_recipe_sha256") == source_recipe_sha
PY
then
  fail_env "Python runtime/overlay provenance does not match this ROS 2 release"
  return 70 2>/dev/null || exit 70
fi
PATH="$PY312/bin:$PATH"
LD_LIBRARY_PATH="$PY312/lib:$LD_LIBRARY_PATH"
export PATH LD_LIBRARY_PATH
export LD_PRELOAD="$PY312/lib/libpython3.12.so.1.0${LD_PRELOAD:+:$LD_PRELOAD}"
export PYTHONNOUSERSITE=1
export PYTHONDONTWRITEBYTECODE=1
export PYTHONPATH="$ROS2_HOME/Lib/site-packages:$PY_OVERLAY:$PY312/lib/python3.12/site-packages"

unalias ros2 2>/dev/null || true
unset -f ros2 2>/dev/null || true
ros2() {
  if [ "${ROS2_CLI_WRAPPER:-}" != "$ROS2_HOME/env.sh:generic-ros2" ]; then
    printf '%s\n' "ERROR: ROS 2 CLI overlay provenance is not this deployment" >&2
    return 70
  fi
  python3.12 -c 'import sys; from ros2cli.cli import main; sys.exit(main())' "$@"
}

rqt() {
  LD_PRELOAD="$ROS2_HOME/Lib/site-packages/qt_gui_cpp/libqt_gui_cpp_sip.so${LD_PRELOAD:+:$LD_PRELOAD}" \
    python3.12 -c '
import os
import sys
from rqt_gui.main import main
try:
    result = main()
except SystemExit as error:
    result = error.code if isinstance(error.code, int) else 0
sys.stdout.flush()
sys.stderr.flush()
os._exit(int(result or 0))
' "$@"
}
