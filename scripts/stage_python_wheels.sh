#!/usr/bin/env bash
# Unpack wheels from python_target/wheels/ into python_target/sitepkgs/ for
# staging onto the board. Binary extension modules inside musllinux wheels are
# suffixed for the generic musl target (e.g. .cpython-312-aarch64-linux-musl.so
# or plain .abi3.so); the board's CPython build looks for
# .cpython-312-aarch64-linux-ohos.so, so rename them here.
#
# Usage: ./scripts/stage_python_wheels.sh [wheel-file ...]
#   With no arguments, stages every wheel in python_target/wheels/.
set -euo pipefail
cd "$(dirname "$0")/.."

export PATH="$HOME/.pixi/bin:$PATH"
WHEELS_DIR="$(pwd)/python_target/wheels"
SITE_PKGS="$(pwd)/python_target/sitepkgs"
SOABI="cpython-312-aarch64-linux-ohos"

shopt -s nullglob
WHEELS=("$@")
if [ ${#WHEELS[@]} -eq 0 ]; then
  WHEELS=("$WHEELS_DIR"/*.whl)
fi

for wheel in "${WHEELS[@]}"; do
  echo "== staging $(basename "$wheel")"
  pixi run python -m zipfile -e "$wheel" "$SITE_PKGS/"
done

# Rename extension modules to the SOABI suffix the board interpreter expects.
# Bundled plain shared libraries (*.libs/*.so*) keep their original names.
find "$SITE_PKGS" -name '*.so' \
  ! -name "*.${SOABI}.so" \
  ! -path '*.libs/*' | while read -r f; do
  base="$(basename "$f")"
  # strip any cpython/abi tag: foo.cpython-312-aarch64-linux-musl.so -> foo
  #                        or: foo.abi3.so -> foo
  stem="${base%%.*}"
  mv "$f" "$(dirname "$f")/${stem}.${SOABI}.so"
  echo "   renamed $base -> ${stem}.${SOABI}.so"
done

echo "staged into $SITE_PKGS"
