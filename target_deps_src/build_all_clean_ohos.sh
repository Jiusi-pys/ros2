#!/usr/bin/env bash
# Build the complete KaihongOS target-dependency prefix from a materialization-
# free source workspace. Unlike the individual developer recipes, this entry
# never reuses downloads, extracted trees, build directories, or install files.
#
# A successful run leaves two independently verifiable controls in install_ohos:
#   .ohos-target-deps.clean-prefix.json   exact path/type/mode/content inventory
#   .ohos-target-deps.clean-receipt.json  inputs, source journal, and self hash
#
# Usage (Git Bash):
#   OHOS_NATIVE_SDK=C:/path/to/sdk/native ./target_deps_src/build_all_clean_ohos.sh
set -euo pipefail
cd "$(dirname "$0")/.."

WORKSPACE_ROOT="$(pwd -W 2>/dev/null || pwd)"
SRC_DIR="$WORKSPACE_ROOT/target_deps_src"
PREFIX="$WORKSPACE_ROOT/install_ohos"
RECEIPT_TOOL="$SRC_DIR/lib/clean_deps_receipt.py"
HOST_PYTHON="$WORKSPACE_ROOT/.pixi/envs/default/python.exe"
PYTHON_TARGET_ROOT="$WORKSPACE_ROOT/python_target/usr"
PYTHON_SITEPKGS_ROOT="$WORKSPACE_ROOT/python_target/sitepkgs"
PYTHON_TARGET_MANAGER="$WORKSPACE_ROOT/scripts/python_target.py"
PYTHON_TARGET_LOCK="$WORKSPACE_ROOT/scripts/python/ohos_python.lock.json"
LOCK_DIR="$WORKSPACE_ROOT/.ohos-target-deps-clean.lock"
LOCK_OWNED=0
RUN_DIR=""
BEGIN_RECORD=""
JOURNAL=""

cleanup() {
  local status=$?
  set +e
  # Keep BEGIN and the source journal even on failure. They are diagnostic
  # evidence, never a substitute for the separately verified COMPLETE receipt.
  if [ -n "$RUN_DIR" ]; then
    printf 'TARGET_DEPS_CLEAN_EVIDENCE exit_status=%s directory=%s\n' "$status" "$RUN_DIR" >&2
  fi
  if [ "$LOCK_OWNED" = 1 ]; then rmdir -- "$LOCK_DIR" 2>/dev/null; fi
  trap - EXIT
  exit "$status"
}
trap cleanup EXIT

[ -f "$RECEIPT_TOOL" ] || {
  echo "ERROR: clean dependency receipt helper is missing: $RECEIPT_TOOL" >&2
  exit 2
}
[ -x "$HOST_PYTHON" ] || {
  echo "ERROR: host Python is missing: $HOST_PYTHON (run pixi install)" >&2
  exit 2
}
[ -f "$WORKSPACE_ROOT/pixi.lock" ] || {
  echo "ERROR: pixi.lock is required for a clean dependency build" >&2
  exit 2
}
[ -d "$PYTHON_TARGET_ROOT" ] || {
  echo "ERROR: target Python sysroot is missing: $PYTHON_TARGET_ROOT" >&2
  exit 2
}
[ -d "$PYTHON_SITEPKGS_ROOT" ] || {
  echo "ERROR: locked target Python site-packages are missing: $PYTHON_SITEPKGS_ROOT" >&2
  exit 2
}
[ -f "$PYTHON_TARGET_MANAGER" ] && [ -f "$PYTHON_TARGET_LOCK" ] || {
  echo "ERROR: target Python verifier or lock is missing" >&2
  exit 2
}
[ ! -e "$PREFIX" ] && [ ! -L "$PREFIX" ] || {
  echo "ERROR: clean target dependency prefix must be absent: $PREFIX" >&2
  exit 2
}

# Ambient compiler, Python, Qt, and package-discovery overrides are not part of
# the recipe contract and must not silently alter a build called "clean".
unset CC CXX CPP LD AR AS NM RANLIB STRIP OBJCOPY OBJDUMP \
  CFLAGS CXXFLAGS CPPFLAGS LDFLAGS CPATH CPLUS_INCLUDE_PATH LIBRARY_PATH \
  CMAKE_PREFIX_PATH CMAKE_TOOLCHAIN_FILE CMAKE_GENERATOR CMAKE_BUILD_TYPE \
  PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR \
  AMENT_PREFIX_PATH COLCON_PREFIX_PATH DESTDIR MAKE MAKEFLAGS MFLAGS \
  SHELL CONFIG_SHELL CONFIG_SITE BASH_ENV ENV CDPATH \
  PYTHONHOME PYTHONPATH PYTHONUSERBASE \
  QMAKESPEC QMAKEFEATURES QTDIR QT_PLUGIN_PATH QT_QPA_PLATFORM
export PYTHONNOUSERSITE=1
export LC_ALL=C

# PyQt consumes both trees. Their own complete manifests must validate against
# the checked-in Python lock before they are accepted as clean-build inputs.
"$HOST_PYTHON" "$PYTHON_TARGET_MANAGER" --lock "$PYTHON_TARGET_LOCK" \
  verify-runtime --root "$PYTHON_TARGET_ROOT" >/dev/null
"$HOST_PYTHON" "$PYTHON_TARGET_MANAGER" --lock "$PYTHON_TARGET_LOCK" \
  verify-stage --site "$PYTHON_SITEPKGS_ROOT" >/dev/null

# Keep the Git Bash runtime that launched this script first.  The conda/pixi
# Library/bin/sh.exe is a login shim and Library/usr/bin/sh.exe starts a second
# MSYS runtime; either can rewrite PATH so recursive configure/qmake shells lose
# Library/bin/make.exe.  /usr/bin is the launcher's real Git Bash mount.  BEGIN
# records every command that this ordering actually resolves, including the
# Git Bash tools and the pixi make/cmake/ninja executables.
PIXI_HOST_BIN="$WORKSPACE_ROOT/.pixi/envs/default/Library/bin"
PIXI_HOST_USR_BIN="$WORKSPACE_ROOT/.pixi/envs/default/Library/usr/bin"
for tool in \
  "$PIXI_HOST_BIN/cmake.exe" \
  "$PIXI_HOST_BIN/ninja.exe" \
  "$PIXI_HOST_BIN/make.exe" \
  "$PIXI_HOST_BIN/curl.exe" \
  "$PIXI_HOST_BIN/git.exe" \
  "$WORKSPACE_ROOT/.pixi/envs/default/Library/mingw64/bin/git.exe"; do
  [ -f "$tool" ] || { echo "ERROR: pixi host tool is missing: $tool" >&2; exit 2; }
done
PIXI_HOST_BIN_POSIX="$(cygpath "$PIXI_HOST_BIN")"
PIXI_HOST_USR_BIN_POSIX="$(cygpath "$PIXI_HOST_USR_BIN")"
export PATH="/usr/bin:$PIXI_HOST_BIN_POSIX:$PIXI_HOST_USR_BIN_POSIX:$HOME/.pixi/bin:$PATH"
hash -r
for name in sh bash tar patch which sha256sum make cmake ninja git curl; do
  command -v "$name" >/dev/null 2>&1 || {
    echo "ERROR: required host tool is not resolvable after PATH setup: $name" >&2
    exit 2
  }
done
case "$(cygpath -am "$(command -v sh)")" in
  "$WORKSPACE_ROOT"/.pixi/*)
    echo "ERROR: clean dependency build must run under Git Bash, not pixi/MSYS sh" >&2
    exit 2
    ;;
esac
sh -c 'command -v make >/dev/null 2>&1' || {
  echo "ERROR: recursive Git Bash shell cannot resolve pixi make" >&2
  exit 2
}

OHOS_NATIVE="${OHOS_NATIVE_SDK:-}"
[ -n "$OHOS_NATIVE" ] || {
  echo "ERROR: set OHOS_NATIVE_SDK to the OpenHarmony native SDK directory" >&2
  exit 2
}
[ -d "$OHOS_NATIVE" ] || {
  echo "ERROR: OpenHarmony native SDK directory is missing: $OHOS_NATIVE" >&2
  exit 2
}
OHOS_NATIVE="$(cd "$OHOS_NATIVE" && (pwd -W 2>/dev/null || pwd -P))"
[ -d "$OHOS_NATIVE/sysroot" ] && [ -f "$OHOS_NATIVE/oh-uni-package.json" ] || {
  echo "ERROR: incomplete OpenHarmony native SDK: $OHOS_NATIVE" >&2
  exit 2
}
OHOS_NATIVE_API="$("$HOST_PYTHON" -I -c \
  'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["apiVersion"])' \
  "$OHOS_NATIVE/oh-uni-package.json")"
case "$OHOS_NATIVE_API" in
  ''|*[!0-9]*) echo "ERROR: invalid native SDK apiVersion: $OHOS_NATIVE_API" >&2; exit 2 ;;
esac
if [ "$OHOS_NATIVE_API" -lt 23 ]; then
  echo "ERROR: native SDK API 23 or newer is required (found $OHOS_NATIVE_API)" >&2
  exit 2
fi
printf 'OHOS_NATIVE_SDK_VERIFIED api=%s path=%s\n' "$OHOS_NATIVE_API" "$OHOS_NATIVE"
DERIVED_SDK_ROOT="$(cd "$OHOS_NATIVE/.." && (pwd -W 2>/dev/null || pwd -P))"
if [ -n "${OHOS_SDK_PATH:-}" ]; then
  [ -d "$OHOS_SDK_PATH" ] || {
    echo "ERROR: OHOS_SDK_PATH directory is missing: $OHOS_SDK_PATH" >&2
    exit 2
  }
  SUPPLIED_SDK_ROOT="$(cd "$OHOS_SDK_PATH" && (pwd -W 2>/dev/null || pwd -P))"
  [ "$SUPPLIED_SDK_ROOT" = "$DERIVED_SDK_ROOT" ] || {
    echo "ERROR: OHOS_SDK_PATH and OHOS_NATIVE_SDK select different SDK trees" >&2
    exit 2
  }
fi
export OHOS_NATIVE_SDK="$OHOS_NATIVE"
export OHOS_SDK_PATH="$DERIVED_SDK_ROOT"

# mkdir is the lock acquisition. A stale directory is deliberately not stolen:
# its owner must first determine whether an interrupted build is still active.
if ! mkdir -- "$LOCK_DIR" 2>/dev/null; then
  echo "ERROR: another or interrupted clean dependency build owns $LOCK_DIR" >&2
  exit 1
fi
LOCK_OWNED=1

GIT_ADMIN_DIR="$(git -C "$WORKSPACE_ROOT" rev-parse --path-format=absolute --git-dir)"
GIT_ADMIN_DIR="$(cd "$GIT_ADMIN_DIR" && (pwd -W 2>/dev/null || pwd -P))"
RUN_DIR="$GIT_ADMIN_DIR/ohos-target-deps-clean.$$.${RANDOM}${RANDOM}"
case "$RUN_DIR" in
  "$GIT_ADMIN_DIR"/ohos-target-deps-clean.*) ;;
  *) echo "ERROR: unsafe temporary directory: $RUN_DIR" >&2; exit 70 ;;
esac
if ! mkdir -- "$RUN_DIR"; then
  echo "ERROR: could not create private clean-build evidence directory: $RUN_DIR" >&2
  exit 1
fi
BEGIN_RECORD="$RUN_DIR/begin.json"
JOURNAL="$RUN_DIR/sources.jsonl"

EXPECTED_RECIPES=(
  qt_host_tools
  core_target_deps
  qtbase
  assimp
  ogre
  qtsvg
  pyqt_sip
  pyqt5
)

# BEGIN is create-only and checks both that PREFIX is absent and that no Git-
# ignored materialization exists under target_deps_src. It also records complete
# recipe, SDK, and target-Python inputs before any output directory is created.
"$HOST_PYTHON" "$RECEIPT_TOOL" begin \
  --workspace "$WORKSPACE_ROOT" \
  --prefix "$PREFIX" \
  --sdk-root "$OHOS_NATIVE" \
  --python-target-root "$PYTHON_TARGET_ROOT" \
  --python-sitepkgs-root "$PYTHON_SITEPKGS_ROOT" \
  --journal "$JOURNAL" \
  --expected-recipe "${EXPECTED_RECIPES[@]}" \
  --output "$BEGIN_RECORD"

# Do not use mkdir -p here. The absence proved by BEGIN remains an enforced
# create-only transition even if another process races this runner.
mkdir -- "$PREFIX"

export OHOS_TARGET_DEPS_CLEAN=1
export OHOS_TARGET_DEPS_JOURNAL="$JOURNAL"
export OHOS_TARGET_DEPS_PYTHON="$HOST_PYTHON"
export OHOS_TARGET_DEPS_RECEIPT_TOOL="$RECEIPT_TOOL"

run_recipe() { # <stable-recipe-name> <command> [args...]
  local name="$1"
  shift
  printf 'TARGET_DEPS_CLEAN_RECIPE name=%s state=begin\n' "$name"
  "$@"
  "$HOST_PYTHON" "$RECEIPT_TOOL" journal \
    --journal "$JOURNAL" --event recipe_complete --name "$name"
  printf 'TARGET_DEPS_CLEAN_RECIPE name=%s state=complete\n' "$name"
}

run_recipe qt_host_tools "$SRC_DIR/bootstrap_qt_host_tools.sh"
run_recipe core_target_deps "$WORKSPACE_ROOT/scripts/build_target_deps.sh"
run_recipe qtbase "$SRC_DIR/build_qtbase_ohos.sh"
run_recipe assimp "$SRC_DIR/build_assimp_ohos.sh"
run_recipe ogre "$SRC_DIR/build_ogre_ohos.sh"
run_recipe qtsvg "$SRC_DIR/build_qtsvg_ohos.sh"
run_recipe pyqt_sip "$SRC_DIR/pyqt/build_sip_runtime.sh"
run_recipe pyqt5 "$SRC_DIR/pyqt/build_pyqt5.sh"

# These sentinels make an incomplete recipe fail before COMPLETE is minted.
# The receipt's exact inventory then protects every other installed path too.
REQUIRED_OUTPUTS=(
  .ohos-target-deps.recipe.sha256
  bin/lttng
  include/eigen3/signature_of_eigen3_matrix_library
  share/eigen3/cmake/Eigen3Config.cmake
  lib/libtinyxml2.so
  lib/libconsole_bridge.so
  lib/libBulletCollision.so
  lib/libopencv_core.so
  lib/liburcu.so
  lib/liblttng-ust.so
  lib/libpopt.so
  lib/libxml2.so
  lib/libQt5Core.so
  lib/libQt5Gui.so
  lib/libQt5Widgets.so
  plugins/platforms/libplugins_platforms_qoffscreen.so
  lib/libassimp.so
  lib/libOgreMain.so
  lib/OGRE/RenderSystem_GLES2.so
  lib/libQt5Svg.so
  plugins/imageformats/libplugins_imageformats_qsvg.so
  Lib/site-packages/PyQt5/sip.cpython-312-aarch64-linux-ohos.so
  Lib/site-packages/PyQt5/QtCore.cpython-312-aarch64-linux-ohos.so
)
for relative in "${REQUIRED_OUTPUTS[@]}"; do
  [ -f "$PREFIX/$relative" ] || {
    echo "ERROR: clean dependency prefix is incomplete: $relative" >&2
    exit 1
  }
done

MANIFEST="$PREFIX/.ohos-target-deps.clean-prefix.json"
RECEIPT="$PREFIX/.ohos-target-deps.clean-receipt.json"
"$HOST_PYTHON" "$RECEIPT_TOOL" finish \
  --workspace "$WORKSPACE_ROOT" \
  --begin "$BEGIN_RECORD" \
  --journal "$JOURNAL" \
  --prefix "$PREFIX" \
  --manifest "$MANIFEST" \
  --output "$RECEIPT"
"$HOST_PYTHON" "$RECEIPT_TOOL" verify \
  --workspace "$WORKSPACE_ROOT" \
  --receipt "$RECEIPT" \
  --manifest "$MANIFEST" \
  --prefix "$PREFIX"

printf 'TARGET_DEPS_CLEAN_TERMINAL result=PASS receipt=%s manifest=%s\n' \
  "$RECEIPT" "$MANIFEST"
