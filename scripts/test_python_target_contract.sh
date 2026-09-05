#!/usr/bin/env bash
# Regression contracts for fail-closed Python target staging and installation.
set -euo pipefail
cd "$(dirname "$0")/.."

PYTHON="${HOST_PYTHON:-$(pwd)/.pixi/envs/default/python.exe}"
if [ ! -x "$PYTHON" ]; then
  PYTHON="$(command -v python3 || true)"
fi
[ -n "$PYTHON" ] && [ -x "$PYTHON" ] || { echo "host Python 3 is required" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
MOCK_LOG="$TMP/hdc.log"
MOCK_HDC="$TMP/hdc"
cat >"$MOCK_HDC" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"${MOCK_HDC_LOG:?}"
exit 99
EOF
chmod +x "$MOCK_HDC"
export HDC="$MOCK_HDC"
export MOCK_HDC_LOG="$MOCK_LOG"

expect_pre_hdc_failure() {
  local site="$1"
  : >"$MOCK_LOG"
  if PYTHON_SITEPKGS_DIR="$site" ./scripts/install_board_python_deps.sh test-board \
      >"$TMP/stdout" 2>"$TMP/stderr"; then
    echo "expected install preflight to fail for $site" >&2
    exit 1
  fi
  if [ -s "$MOCK_LOG" ]; then
    echo "HDC was called before invalid stage was rejected: $(cat "$MOCK_LOG")" >&2
    exit 1
  fi
}

expect_pre_hdc_failure "$TMP/missing"
mkdir -p "$TMP/empty"
expect_pre_hdc_failure "$TMP/empty"
mkdir -p "$TMP/unsafe/unsafe name"
expect_pre_hdc_failure "$TMP/unsafe"

if grep -Fq 'python_target/sitepkgs/*' scripts/install_board_python_deps.sh; then
  echo "literal site-packages glob reintroduced" >&2
  exit 1
fi
grep -Fq 'verify-stage' scripts/install_board_python_deps.sh || {
  echo "install script no longer verifies the stage manifest" >&2
  exit 1
}

# A release-gated install must also fail before HDC when the runtime artifact
# binding is absent.
: >"$MOCK_LOG"
if PYTHON_REQUIRE_RUNTIME_ARTIFACT=1 \
    ./scripts/install_board_python_deps.sh test-board >"$TMP/stdout" 2>"$TMP/stderr"; then
  echo "expected missing runtime artifact gate to fail" >&2
  exit 1
fi
if [ -s "$MOCK_LOG" ]; then
  echo "HDC was called before missing runtime artifact was rejected" >&2
  exit 1
fi

# Actual deployment always requires the signed artifact pair; there is no
# valid-stage-only path that can reach HDC. Static assertions guard the remote
# overlay and board-side whole-tree verification contract.
if grep -Fq '/usr/lib/python3.12/site-packages' scripts/install_board_python_deps.sh; then
  echo "installer attempted to mutate CPython base site-packages" >&2
  exit 1
fi
grep -Fq 'remote_stage="$REMOTE_OVERLAY.stage.' scripts/install_board_python_deps.sh || {
  echo "installer no longer uses an isolated overlay stage" >&2
  exit 1
}
grep -Fq "mv '\$remote_stage'" scripts/install_board_python_deps.sh || {
  echo "installer no longer commits the completed overlay by rename" >&2
  exit 1
}
grep -Fq "sha256sum -c '\$remote_stage/\$FILE_MANIFEST'" scripts/install_board_python_deps.sh || {
  echo "installer no longer checks every staged file on the board" >&2
  exit 1
}
grep -Fq "cmp '\$remote_stage/\$PATH_INVENTORY'" scripts/install_board_python_deps.sh || {
  echo "installer no longer rejects unexpected staged paths" >&2
  exit 1
}
grep -Fq -- '--require-runtime-artifact' scripts/install_board_python_deps.sh || {
  echo "installer allows an unbound runtime deployment marker" >&2
  exit 1
}
grep -Fq "MSYS2_ARG_CONV_EXCL='--remote-runtime-prefix=;--remote-overlay='" \
    scripts/install_board_python_deps.sh || {
  echo "overlay marker creation no longer protects literal /data paths" >&2
  exit 1
}
grep -Fq "MSYS2_ARG_CONV_EXCL='--remote-prefix='" \
    scripts/deploy_python_runtime_artifact.sh || {
  echo "runtime marker creation no longer protects literal /data paths" >&2
  exit 1
}
grep -Fq 'shell "set -e; $command"' scripts/deploy_python_runtime_artifact.sh || {
  echo "runtime remote checks can continue after a failed marker/hash command" >&2
  exit 1
}

# The Python marker writers are a second line of defence against MSYS path
# conversion and accidental host paths in board provenance.
if "$PYTHON" scripts/python_target.py --lock scripts/python/ohos_python.lock.json \
    create-deployment-marker --site python_target/sitepkgs \
    --output "$TMP/bad-overlay-marker.json" --board test-board \
    --remote-runtime-prefix 'C:/Program Files/Git/data/python312-rk3588a' \
    --remote-overlay 'C:/Program Files/Git/data/python312-rk3588a/ros2-site-packages' \
    --file-manifest "$TMP/no-files" --path-inventory "$TMP/no-paths" \
    >"$TMP/stdout" 2>"$TMP/stderr"; then
  echo "overlay marker accepted a host-converted remote path" >&2
  exit 1
fi

if [ -f python_target/sitepkgs/.ros2-ohos-python-stage.json ]; then
  PREFLIGHT_OUTPUT="$(./scripts/install_board_python_deps.sh --preflight-only | tr -d '\r')"
  grep -q '^python_dependency_preflight=PASS$' <<<"$PREFLIGHT_OUTPUT"
  grep -Eq '^python_stage_tree_sha256=[0-9a-f]{64}$' <<<"$PREFLIGHT_OUTPUT"
fi

echo "python target fail-closed contracts: PASS"
