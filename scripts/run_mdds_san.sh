#!/usr/bin/env bash
# Cross-build the mdds gtest suites with ThreadSanitizer / AddressSanitizer
# (OHOS NDK clang 15.0.4 ships both aarch64 runtimes) and run them on a board.
# This exercises the round-4 concurrency work — CallbackRegistry generations,
# DSoftBus single-flight stop, fragment budget CAS, participant discovery —
# under real instrumentation, on the real target.
#
# A run FAILS if any gtest fails OR the sanitizer prints a report — a green
# gtest summary with a ThreadSanitizer warning above it is NOT a pass.
#
#   ./scripts/run_mdds_san.sh [thread|address|all] [board_serial]
# default: all sanitizers on board A.
set -uo pipefail
cd "$(dirname "$0")/.."

HDC="${HDC:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/toolchains/hdc.exe}"
OHOS_NATIVE="${OHOS_NATIVE_SDK:-C:/Users/17715/Downloads/commandline-tools-windows-x64-6.1.1.300/command-line-tools/sdk/default/openharmony/native}"
CXX="$OHOS_NATIVE/llvm/bin/clang++.exe"
SYSROOT="$OHOS_NATIVE/sysroot"
RTDIR="$OHOS_NATIVE/llvm/lib/clang/15.0.4/lib/aarch64-linux-ohos"
MDDS=src/Jiusi-pys/mdds
GTEST_DIR=build_ohos/mdds/gtest   # cross-built aarch64 libgtest.a/libgtest_main.a
GTEST_INC=install_ohos/src/gtest_vendor/include   # headers for the above (gtest_vendor)
BOARD_A=3e01ff55454d202020104033bf453b00
DEVICE_DIR=/data/local/tmp/mdds_san
LOGDIR=ohos_test_logs/mdds_san
RUN_ID="san_${$}_${RANDOM}"
RUN_LOGDIR="$LOGDIR/$RUN_ID"
RUN_MARKER="$RUN_LOGDIR/.run_marker"
MANIFEST="$RUN_LOGDIR/manifest.txt"
mkdir -p "$RUN_LOGDIR" || { echo "error: cannot create run log directory $RUN_LOGDIR" >&2; exit 2; }
touch "$RUN_MARKER" || { echo "error: cannot create run marker $RUN_MARKER" >&2; exit 2; }
printf 'run_id=%s\nboard=%s\n' "$RUN_ID" "${2:-$BOARD_A}" > "$MANIFEST"

SANS="${1:-all}"
BOARD="${2:-$BOARD_A}"
[ "$SANS" = all ] && SANS="thread address"

TESTS="test_callback_registry test_fragment test_frame test_participant test_qos test_send_lane test_transport_udp test_transport_dsoftbus_fake"
MDDS_SOURCES="
  $MDDS/src/discovery.cpp
  $MDDS/src/frame.cpp
  $MDDS/src/fragment.cpp
  $MDDS/src/participant.cpp
  $MDDS/src/qos.cpp
  $MDDS/src/transport_udp.cpp
  $MDDS/src/transport_dsoftbus.cpp
"

export MSYS2_ARG_CONV_EXCL='*'
shell() { "$HDC" -t "$BOARD" shell "$1" </dev/null; }
push()  { "$HDC" -t "$BOARD" file send "$(cygpath -w "$1")" "$2" </dev/null > /dev/null; }
sha256_local() { sha256sum "$1" | cut -d ' ' -f1; }
sha256_remote() { shell "sha256sum '$1' 2>/dev/null | cut -d ' ' -f1" | tr -d '\r\n'; }

require_target() {
  if ! "$HDC" list targets 2>/dev/null | tr -d '\r' | grep -qx "$BOARD"; then
    echo "error: requested HDC target is not ready: $BOARD" >&2
    return 1
  fi
}

push_verified() { # push_verified <local-file> <remote-directory>
  local src="$1" dst="$2" base want got
  base=$(basename "$src")
  if [ ! -f "$src" ]; then
    echo "   missing local artifact: $src" >&2
    return 1
  fi
  if ! push "$src" "$dst/"; then
    echo "   HDC push failed: $src" >&2
    return 1
  fi
  want=$(sha256_local "$src")
  got=$(sha256_remote "$dst/$base")
  if [ -z "$got" ] || [ "$got" != "$want" ]; then
    echo "   remote hash mismatch: $base local=$want remote=${got:-MISSING}" >&2
    return 1
  fi
  printf 'artifact=%s local_sha256=%s remote_sha256=%s remote_path=%s\n' \
    "$base" "$want" "$got" "$dst/$base" >> "$MANIFEST"
}

pull_run_log() { # pull_run_log <remote-file> <local-file> <test-name>
  local remote="$1" local_file="$2" test_name="$3" temp
  temp="$RUN_LOGDIR/.${test_name}.${RANDOM}.tmp"
  if ! shell "test -s '$remote' && grep -Fq 'MDDS_SAN_RUN=$RUN_ID test=$test_name ' '$remote' && grep -Eq '^MDDS_SAN_EXIT=[0-9]+ run=$RUN_ID test=$test_name$' '$remote' && echo SAN_LOG_READY" \
      | tr -d '\r' | grep -qx 'SAN_LOG_READY'; then
    echo "   remote log is absent, stale, or incomplete: $remote" >&2
    rm -f "$temp"
    return 1
  fi
  if ! shell "cat '$remote'" > "$temp" 2>/dev/null || [ ! -s "$temp" ]; then
    echo "   unable to pull remote log: $remote" >&2
    rm -f "$temp"
    return 1
  fi
  if ! mv -f "$temp" "$local_file"; then
    echo "   unable to install local log: $local_file" >&2
    rm -f "$temp"
    return 1
  fi
  if [ ! "$local_file" -nt "$RUN_MARKER" ]; then
    echo "   local log predates this run marker: $local_file" >&2
    return 1
  fi
}

verify_sanitizer_runtime() { # <executable> <sanitizer> <remote-run-directory>
  local executable="$1" sanitizer="$2" remote_dir="$3" dynamic
  dynamic=$("$OHOS_NATIVE/llvm/bin/llvm-readelf.exe" -d "$executable" 2>&1) || {
    echo "   unable to inspect sanitizer runtime linkage: $executable" >&2
    return 1
  }
  if printf '%s\n' "$dynamic" | grep -Fq "libclang_rt.$sanitizer.so"; then
    if ! printf '%s\n' "$dynamic" | grep -Fq "$remote_dir"; then
      echo "   dynamic $sanitizer runtime lacks RUNPATH $remote_dir: $executable" >&2
      return 1
    fi
    printf 'runtime=%s mode=dynamic runpath=%s executable=%s\n' \
      "$sanitizer" "$remote_dir" "$(basename "$executable")" >> "$MANIFEST"
  else
    # The current OHOS clang links both sanitizer runtimes into the test ELF.
    # Record that fact rather than mistaking the separately pushed .so for
    # proof that the current executable actually loaded it.
    printf 'runtime=%s mode=embedded_or_static executable=%s\n' \
      "$sanitizer" "$(basename "$executable")" >> "$MANIFEST"
  fi
}

[ -f "$GTEST_DIR/libgtest.a" ] || { echo "error: $GTEST_DIR/libgtest.a missing - run scripts/build_ohos.sh --packages-select mdds first"; exit 1; }
require_target || exit 2

total_pass=0; total_fail=0; failed=()

for san in $SANS; do
  case "$san" in
    thread)  rtlib=libclang_rt.tsan.so ;;
    address) rtlib=libclang_rt.asan.so ;;
    *) echo "unknown sanitizer: $san"; exit 2 ;;
  esac
  flag="-fsanitize=$san"
  outdir="build_ohos_san/$san"
  remote_dir="$DEVICE_DIR/$san/$RUN_ID"
  mkdir -p "$outdir"

  echo "== building ($san) =="
  build_fail=0
  for t in $TESTS; do
    # MDDS_WITH_DSOFTBUS=1 mirrors the real OHOS configuration (including the
    # strict-transport fail-closed branch); softbus libs resolve on-device
    # via /system/lib64/platformsdk, the sanitizer runtime via rpath below.
    if ! "$CXX" --target=aarch64-linux-ohos --sysroot="$SYSROOT" \
        -std=c++17 -g -O1 $flag -D__MUSL__ -DMDDS_WITH_DSOFTBUS=1 \
        -I "$MDDS/include" -I "$MDDS/third_party/dsoftbus/include" -I "$GTEST_INC" \
        $MDDS_SOURCES "$MDDS/test/$t.cpp" \
        "$GTEST_DIR/libgtest.a" "$GTEST_DIR/libgtest_main.a" \
        -L "$MDDS/third_party/dsoftbus/lib" \
        -l:libsoftbus_client.z.so -l:libnativetoken_shared.z.so -l:libtokensetproc_shared.z.so \
        $flag -Wl,--allow-shlib-undefined \
        -Wl,-rpath,$remote_dir -Wl,-rpath,/system/lib64/platformsdk \
        -o "$outdir/$t" 2> "$RUN_LOGDIR/build_${san}_$t.log"; then
      echo "   BUILD FAIL $t (see $RUN_LOGDIR/build_${san}_$t.log)"
      build_fail=1
    fi
  done
  if [ "$build_fail" -eq 0 ]; then
    for t in $TESTS; do
      verify_sanitizer_runtime "$outdir/$t" "$san" "$remote_dir" || build_fail=1
    done
  fi
  if [ "$build_fail" -ne 0 ]; then
    total_fail=$((total_fail+1)); failed+=("build-$san")
    continue
  fi

  echo "== deploying ($san) to ${BOARD:0:8} =="
  # A unique directory makes it impossible for a failed cleanup or push to
  # execute an earlier binary.  The run token and hashes below make the same
  # guarantee independently observable in the pulled result log.
  if ! shell "mkdir -p '$remote_dir' && test -d '$remote_dir' && echo SAN_DEPLOY_READY=$RUN_ID" \
      | tr -d '\r' | grep -qx "SAN_DEPLOY_READY=$RUN_ID"; then
    echo "   DEPLOY FAIL: cannot prepare $remote_dir"
    total_fail=$((total_fail+1)); failed+=("deploy-$san")
    continue
  fi
  deploy_fail=0
  push_verified "$RTDIR/$rtlib" "$remote_dir" || deploy_fail=1
  for t in $TESTS; do push_verified "$outdir/$t" "$remote_dir" || deploy_fail=1; done
  if [ "$deploy_fail" -ne 0 ]; then
    total_fail=$((total_fail+1)); failed+=("deploy-$san")
    continue
  fi
  if ! shell "chmod 755 '$remote_dir'/* && test -x '$remote_dir/test_callback_registry' && echo SAN_CHMOD_READY=$RUN_ID" \
      | tr -d '\r' | grep -qx "SAN_CHMOD_READY=$RUN_ID"; then
    echo "   DEPLOY FAIL: unable to make sanitizer executables runnable"
    total_fail=$((total_fail+1)); failed+=("deploy-$san")
    continue
  fi

  echo "== running ($san) =="
  # Keep this away from d42, whose DSoftBus bind-deny guard was deliberately
  # exercised by an earlier test round.  The test hook only changes the
  # DSoftBus-specific strict-transport test; all ordinary test semantics stay
  # unchanged.
  test_domain=$((10000 + (RANDOM % 20000)))
  printf 'sanitizer=%s dsoftbus_test_domain=%s remote_dir=%s\n' \
    "$san" "$test_domain" "$remote_dir" >> "$MANIFEST"
  for t in $TESTS; do
    remote_log="$remote_dir/$t.log"
    local_log="$RUN_LOGDIR/${san}_$t.log"
    # HDC does not faithfully propagate the remote process status.  Record
    # the actual timeout/gtest exit code in-band, tied to this exact run.
    shell "cd '$remote_dir' && printf '%s\\n' 'MDDS_SAN_RUN=$RUN_ID test=$t domain=$test_domain' > '$t.log'; MDDS_TEST_DSOFTBUS_DOMAIN=$test_domain timeout -k 10s 300s './$t' >> '$t.log' 2>&1; rc=\$?; printf 'MDDS_SAN_EXIT=%s run=$RUN_ID test=$t\\n' \"\$rc\" >> '$t.log'" > /dev/null
    local_bad=0
    pull_run_log "$remote_log" "$local_log" "$t" || local_bad=1
    [ "$local_bad" -eq 0 ] || { echo "   FAIL $t (missing current remote evidence)"; total_fail=$((total_fail+1)); failed+=("$san/$t"); continue; }
    grep -Eq "^MDDS_SAN_EXIT=0 run=$RUN_ID test=$t$" "$local_log" || local_bad=1
    grep -qE "^\[  FAILED  \]" "$local_log" && local_bad=1
    grep -qE "(WARNING: ThreadSanitizer|ERROR: AddressSanitizer|SUMMARY: (Thread|Address)Sanitizer|LeakSanitizer)" "$local_log" && local_bad=1
    grep -qE "^\[  PASSED  \]" "$local_log" || local_bad=1
    if [ "$local_bad" -eq 0 ]; then
      echo "   PASS $t"
      total_pass=$((total_pass+1))
    else
      echo "   FAIL $t (see $local_log)"
      grep -E "MDDS_SAN_EXIT|FAILED|ThreadSanitizer|AddressSanitizer" "$local_log" | head -3 | sed 's/^/      /'
      total_fail=$((total_fail+1)); failed+=("$san/$t")
    fi
  done
done

echo
echo "== mdds sanitizer summary: $total_pass passed, $total_fail failed (run=$RUN_ID) =="
echo "   evidence: $RUN_LOGDIR"
[ ${#failed[@]} -eq 0 ] || printf '   FAIL %s\n' "${failed[@]}"
[ "$total_fail" -eq 0 ]
