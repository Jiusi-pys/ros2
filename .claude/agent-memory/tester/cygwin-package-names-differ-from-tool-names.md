---
name: cygwin-package-names-differ-from-tool-names
description: Cygwin's setup.exe/cygcheck package namespace uses gawk, not awk - a tool name is not a package name, so fail-closed inventory checks over cygcheck -c output break
metadata:
  type: project
---

On Cygwin the package that provides the `awk` command is named **`gawk`**.
Cygwin has no package called `awk`: `setup-x86_64.exe` logs
`Package 'awk' not found.` (soft warning, exit code still 0) and installs
`gawk` as a dependency anyway, while `cygcheck -c` lists only the `gawk`
row. So a tool name and its package name are not interchangeable in
Cygwin's namespace.

**Why:** F106 v5's `ros2/scripts/java/acquire_cygwin.py` requested `awk`
in its `PACKAGES` set and then asserted every requested name appeared as
an `OK` row of `cygcheck -c`. `awk` never appears there, so the
fail-closed inventory check failed deterministically
(`Cygwin package set is incomplete or not OK: ['awk']`) and no
`cygwin.lock.json` was ever written, blocking the whole F106 path even
though the Cygwin install itself was complete and working.

**How to apply:** When writing or reviewing a Cygwin acquisition /
verification step, validate *commands* with `command -v <tool>` and
validate *packages* with `cygcheck -c <package>`. Never derive one list
from the other, and confirm each name exists in Cygwin's namespace before
pinning it. Same class of trap applies to `make` (real package) vs
build tools provided by another package, and to Cygwin "alternatives"
(`/usr/bin/awk` exists as an alternative even with no `awk` package).
Reusable evidence: Cygwin 3.6.10-1.x86_64 provides awk via
`gawk 5.4.0-1`. See [[f106-cygwin-build-host]].
