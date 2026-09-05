#!/usr/bin/env bash
# Materialize the exact Windows host tools and Python build frontends used by
# the Qt/PyQt cross build. Network content is accepted only after SHA-256
# verification against sources.lock.
set -euo pipefail
cd "$(dirname "$0")/.."

WORKSPACE_ROOT="$(pwd -W 2>/dev/null || pwd)"
SRC_DIR="$WORKSPACE_ROOT/target_deps_src"
HOST_TOOLS="$SRC_DIR/qt-host-tools"
PYQT_DIR="$SRC_DIR/pyqt"
HOST_PYTHON="${HOST_PYTHON:-$WORKSPACE_ROOT/.pixi/envs/default/python.exe}"
# shellcheck source=lib/locked_sources.sh
source "$SRC_DIR/lib/locked_sources.sh"

[ -x "$HOST_PYTHON" ] || {
  echo "ERROR: host Python is missing: $HOST_PYTHON (run pixi install)" >&2
  exit 2
}
mkdir -p "$HOST_TOOLS" "$PYQT_DIR"

fetch_locked qt5_applications-5.15.2.2.3-py3-none-win_amd64.whl \
  "$HOST_TOOLS/qt5_applications-5.15.2.2.3-py3-none-win_amd64.whl"
fetch_locked sip-4.19.25-py310h8a704f9_1.tar.bz2 "$HOST_TOOLS/sip4.tar.bz2"
fetch_locked PyQt5-5.15.11.tar.gz "$PYQT_DIR/PyQt5-5.15.11.tar.gz"
fetch_locked pyqt5_sip-12.19.0.tar.gz "$PYQT_DIR/pyqt5_sip-12.19.0.tar.gz"
fetch_locked sip-6.8.6-py3-none-any.whl \
  "$HOST_TOOLS/sip-6.8.6-py3-none-any.whl"
fetch_locked pyqt_builder-1.19.1-py3-none-any.whl \
  "$HOST_TOOLS/pyqt_builder-1.19.1-py3-none-any.whl"

if [ ! -d "$HOST_TOOLS/qt5_applications/Qt/bin" ]; then
  "$HOST_PYTHON" -m zipfile -e \
    "$HOST_TOOLS/qt5_applications-5.15.2.2.3-py3-none-win_amd64.whl" \
    "$HOST_TOOLS"
fi
if [ ! -d "$HOST_TOOLS/sip4/Library/bin" ]; then
  mkdir -p "$HOST_TOOLS/sip4"
  tar -xf "$(cygpath "$HOST_TOOLS/sip4.tar.bz2")" -C "$(cygpath "$HOST_TOOLS/sip4")"
fi
if [ ! -d "$PYQT_DIR/PyQt5-5.15.11" ]; then
  tar -xf "$(cygpath "$PYQT_DIR/PyQt5-5.15.11.tar.gz")" -C "$(cygpath "$PYQT_DIR")"
fi
if [ ! -d "$PYQT_DIR/pyqt5_sip-12.19.0" ]; then
  tar -xf "$(cygpath "$PYQT_DIR/pyqt5_sip-12.19.0.tar.gz")" -C "$(cygpath "$PYQT_DIR")"
fi

package_version() {
  "$HOST_PYTHON" -c \
    "import importlib.metadata as m; print(m.version('$1'))" 2>/dev/null || true
}
if target_deps_clean_mode || [ "$(package_version sip)" != 6.8.6 ]; then
  "$HOST_PYTHON" -m pip --isolated install \
    --no-index --no-deps --no-cache-dir --force-reinstall \
    "$HOST_TOOLS/sip-6.8.6-py3-none-any.whl"
fi
if target_deps_clean_mode || [ "$(package_version pyqt-builder)" != 1.19.1 ]; then
  "$HOST_PYTHON" -m pip --isolated install \
    --no-index --no-deps --no-cache-dir --force-reinstall \
    "$HOST_TOOLS/pyqt_builder-1.19.1-py3-none-any.whl"
fi

for tool in qmake.exe moc.exe uic.exe rcc.exe; do
  [ -f "$HOST_TOOLS/qt5_applications/Qt/bin/$tool" ] || {
    echo "ERROR: verified Qt host wheel did not provide $tool" >&2
    exit 1
  }
done
[ -f "$HOST_TOOLS/sip4/Library/bin/sip.exe" ] || {
  echo "ERROR: verified sip4 archive did not provide sip.exe" >&2
  exit 1
}
[ "$(package_version sip)" = 6.8.6 ] || { echo "ERROR: sip host version drift" >&2; exit 1; }
[ "$(package_version pyqt-builder)" = 1.19.1 ] || {
  echo "ERROR: pyqt-builder host version drift" >&2
  exit 1
}

FINGERPRINT="$(recipe_fingerprint \
  "$(lock_field qt5_applications-5.15.2.2.3-py3-none-win_amd64.whl 3)" \
  "$(lock_field sip-4.19.25-py310h8a704f9_1.tar.bz2 3)" \
  "$(lock_field PyQt5-5.15.11.tar.gz 3)" \
  "$(lock_field pyqt5_sip-12.19.0.tar.gz 3)" \
  "$(lock_field sip-6.8.6-py3-none-any.whl 3)" \
  "$(lock_field pyqt_builder-1.19.1-py3-none-any.whl 3)" \
  "$(sha256_file "$0")")"
write_marker "$HOST_TOOLS/.bootstrap-done" "$FINGERPRINT"
printf 'QT_HOST_TOOLS_VERIFIED fingerprint=%s\n' "$FINGERPRINT"
