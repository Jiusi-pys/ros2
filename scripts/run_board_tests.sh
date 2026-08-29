#!/usr/bin/env bash
# Run the cross-built gtest suites of selected packages on a board.
#
# ament test binaries are not installed into install_ohos/; they live at the
# top level of build_ohos/<pkg>/. For each package this script
#   1. generates a board-side driver from build_ohos/<pkg>/CTestTestfile.cmake
#      (scripts/_parse_ctest_env.py) so the ament test fixtures (env vars,
#      library-path appends, --skip-test markers) are replayed faithfully,
#   2. pushes the package's test executables + helper .so files (layout kept)
#      plus the driver to $ROS2_HOME/tests/<pkg>/ on the board,
#   3. runs the driver and reports BOARDTEST verdict lines.
#
# Usage: ./scripts/run_board_tests.sh [board_serial] [pkg ...]
#   default board: board A; default pkgs: core set (see below)
set -uo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
BOARD_A=3e01ff55454d202020104033bf453b00

# First argument is the board serial if it is not a package build dir.
BOARD="$BOARD_A"
if [ $# -gt 0 ] && [ ! -d "build_ohos/$1" ]; then BOARD="$1"; shift; fi
PKGS=("$@")
if [ ${#PKGS[@]} -eq 0 ]; then
  PKGS=(rcutils rcpputils rosidl_runtime_c rosidl_runtime_cpp rmw
        rcl_yaml_param_parser rcl rcl_action rcl_lifecycle rclcpp test_msgs)
fi

export MSYS2_ARG_CONV_EXCL='*'
export PATH="$HOME/.pixi/bin:$PATH"
WS_ROOT="$(pwd -W 2>/dev/null || pwd)"
ROS2_HOME=/data/local/tmp/ros2

# package name -> source dir (for test fixture resource files)
declare -A PKG_SRC=()
while read -r name path; do
  PKG_SRC["$name"]="$path"
done < <(pixi run colcon list --base-paths src 2>/dev/null | awk '{print $1, $2}')

# tests that can never pass off the build host (assert build-machine RPATH
# mechanics etc.)
is_known_skip() {
  case "$1" in
    rcutils/test_shared_library_in_run_paths) return 0 ;;  # DT_RPATH bakes host build path
  esac
  return 1
}

# hdc consumes stdin when it feels like it - never let it eat a loop's pipe
shell() { "$HDC" -t "$BOARD" shell "$*" </dev/null; }
push() { "$HDC" -t "$BOARD" file send "$(cygpath -w "$1")" "$2" </dev/null > /dev/null; }

total_pass=0; total_fail=0; total_skip=0
failed_tests=()

for pkg in "${PKGS[@]}"; do
  dir="build_ohos/$pkg"
  [ -f "$dir/CTestTestfile.cmake" ] || { echo "== $pkg: no CTestTestfile, skipped"; continue; }
  # collect native test executables + helper libs (build-tree layout kept)
  mapfile -t exes < <(find "$dir" -type f \
    -not -path "*/CMakeFiles/*" -not -path "*/.cmake/*" \
    -not -path "*/gtest/*" -not -path "*/gmock/*" \
    -exec file {} + \
    | grep "ELF 64-bit" | grep -i "aarch64" | grep -iE "executable|interpreter" | cut -d: -f1 \
    | grep -v "/benchmark_")
  [ ${#exes[@]} -eq 0 ] && { echo "== $pkg: no test executables, skipped"; continue; }
  mapfile -t libs < <(find "$dir" -type f -name "*.so" -not -path "*/.cmake/*" -not -path "*/CMakeFiles/*")
  echo "== $pkg: ${#exes[@]} executables, ${#libs[@]} helper libs"
  # driver script replaying the ament test fixtures
  driver="build_ohos/$pkg/run_tests_board.sh"
  pixi run python scripts/_parse_ctest_env.py \
    "$WS_ROOT/$dir/CTestTestfile.cmake" "$pkg" "$WS_ROOT" | tr -d '\r' > "$driver"
  shell "mkdir -p $ROS2_HOME/tests/$pkg && rm -rf $ROS2_HOME/tests/$pkg/*"
  push "$driver" "$ROS2_HOME/tests/$pkg/"
  for f in "${exes[@]}" ${libs[@]+"${libs[@]}"}; do
    rel="${f#$dir/}"
    shell "mkdir -p $ROS2_HOME/tests/$pkg/$(dirname "$rel")"
    push "$f" "$ROS2_HOME/tests/$pkg/$rel"
  done
  # fixture resource files referenced relative to the test working directory
  # (e.g. rcutils' <cwd>/test/dummy_readable_file.txt). Files go through CR
  # stripping (the Windows checkout has CRLF and tests assert byte counts
  # computed on LF content), except known binary formats.
  srcdir="${PKG_SRC[$pkg]:-}"
  if [ -n "$srcdir" ] && [ -d "$srcdir/test" ]; then
    tmpfix="$(mktemp -d)"
    (cd "$srcdir" && find test -type f) | while read -r rel; do
      shell "mkdir -p $ROS2_HOME/tests/$pkg/$(dirname "$rel")"
      case "$rel" in
        *.so|*.png|*.jpg|*.jpeg|*.bin|*.db3|*.mcap|*.bag|*.gz|*.zip|*.urdf)
          push "$srcdir/$rel" "$ROS2_HOME/tests/$pkg/$rel"
          ;;
        *)
          mkdir -p "$tmpfix/$(dirname "$rel")"
          tr -d '\r' < "$srcdir/$rel" > "$tmpfix/$rel"
          push "$tmpfix/$rel" "$ROS2_HOME/tests/$pkg/$rel"
          ;;
      esac
    done
    shell "mkdir -p $ROS2_HOME/tests/$pkg/test"
    rm -rf "$tmpfix"
  fi
  # tests using the compiled-in BUILD_DIR macro get the host path; mirror it
  # (relative, so "C:" becomes a plain directory) under the test dir
  shell "mkdir -p '$ROS2_HOME/tests/$pkg/$WS_ROOT/build_ohos/$pkg'"
  # likewise for macros baking the package SOURCE dir (e.g. rviz_common's
  # _TEST_PLUGIN_DESCRIPTIONS): point the mirrored src path at the pushed
  # test/ fixtures via a symlink
  if [ -n "$srcdir" ] && [ -d "$srcdir/test" ]; then
    abssrc="$(cd "$srcdir" && (pwd -W 2>/dev/null || pwd))"
    shell "mkdir -p '$ROS2_HOME/tests/$pkg/$abssrc' && ln -sfn '$ROS2_HOME/tests/$pkg/test' '$ROS2_HOME/tests/$pkg/$abssrc/test'"
  fi
  # hdc file send does not preserve the exec bit
  shell "cd $ROS2_HOME/tests/$pkg && find . -type f -exec chmod +x {} + 2>/dev/null || true"
  out="$(shell "cd $ROS2_HOME/tests/$pkg && sh ./run_tests_board.sh")"
  while IFS= read -r line; do
    case "$line" in
      "BOARDTEST "*" PASS"*)
        total_pass=$((total_pass+1)) ;;
      "BOARDTEST "*" SKIP"*)
        total_skip=$((total_skip+1)) ;;
      BOARDTEST*FAIL*)
        name="$(printf '%s' "$line" | awk '{print $2}')"
        if is_known_skip "$pkg/$name"; then
          total_skip=$((total_skip+1))
          echo "   SKIP(known) $pkg/$name"
          continue
        fi
        total_fail=$((total_fail+1)); failed_tests+=("$pkg/$name")
        echo "   FAIL $pkg/$name"
        shell "grep -E 'FAILED|Failure|Error' $ROS2_HOME/tests/$pkg/$name.log 2>/dev/null | head -3" | sed 's/^/      /'
        ;;
    esac
  done <<< "$out"
done

echo
echo "== board test summary: $total_pass passed, $total_fail failed, $total_skip skipped =="
if [ ${#failed_tests[@]} -gt 0 ]; then
  printf '   %s\n' "${failed_tests[@]}"
fi
[ "$total_fail" -eq 0 ]
