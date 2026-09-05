#!/usr/bin/env bash
set -euo pipefail

if (($# != 2)); then
  echo "usage: rebuild_host_python.sh CPYTHON_SOURCE OUTPUT_DIR" >&2
  exit 2
fi

source_dir="$(realpath "$1")"
output="$(realpath -m "$2")"
test -f "$source_dir/configure"
test ! -e "$output"
mkdir -p "$output"
cd "$output"

# A same-source host interpreter must not inherit optimization, linker, or
# Python-path state from the invoking machine.
unset CC CXX AR RANLIB CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LIBS CONFIG_SITE \
  PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR PYTHONHOME \
  PYTHONPATH _PYTHON_SYSCONFIGDATA_NAME
export PYTHONDONTWRITEBYTECODE=1

"$source_dir/configure" \
  --without-ensurepip \
  --disable-test-modules \
  > configure.log 2>&1
python3 - Makefile <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
old = "PYTHON_FOR_BUILD=./$(BUILDPYTHON) -E\n"
new = "PYTHON_FOR_BUILD=./$(BUILDPYTHON) -E -B\n"
if text.count(old) != 1:
    raise SystemExit("host Makefile PYTHON_FOR_BUILD marker not found exactly once")
text = text.replace(old, new)
for old, new, label in (
    ("PYTHON_FOR_FREEZE=./_bootstrap_python\n", "PYTHON_FOR_FREEZE=./_bootstrap_python -B\n", "freeze"),
    ("PYTHON_FOR_REGEN?=python3\n", "PYTHON_FOR_REGEN?=python3 -B\n", "regen"),
):
    if text.count(old) != 1:
        raise SystemExit(f"host Makefile Python {label} marker not found exactly once")
    text = text.replace(old, new)
path.write_text(text, encoding="utf-8")
PY
make -j"$(nproc)" python > build.log 2>&1
test -x python
./python -c 'import platform,sys; assert sys.version_info[:3] == (3,12,7); print(platform.python_version())'
sha256sum python
