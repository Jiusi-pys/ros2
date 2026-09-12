---
name: gitbash-mangles-posix-argv
description: Git Bash (MSYS2) silently rewrites POSIX-looking command-line arguments before native .exe tools see them; use MSYS2_ARG_CONV_EXCL='*' when driving Cygwin paths through python.exe or clang.exe
metadata:
  type: project
---

On this Windows host the Bash tool runs **Git Bash (MSYS2)**. Before MSYS2
spawns a native Windows executable (`python.exe`, `clang.exe`, `bash.exe`),
it rewrites every argv entry that looks like a POSIX path into a Git-rooted
Windows path. `/usr/bin/autoconf-2.69` arrives as
`C:/Program Files/Git/usr/bin/autoconf-2.69`.

Two consequences worth remembering:

- A tool that legitimately wants a **Cygwin** POSIX path (e.g. the F106
  Cygwin tree) receives a Git-Bash-rooted path instead, and fails with a
  confusing "does not point to a valid executable" error — or, for
  `clang.exe`, silently loses its `--sysroot` and fails at link with
  `cannot open Scrt1.o` / `unable to find library -lc`.
- The trigger is argv only. Environment-variable values pass through
  verbatim (verified: `AUTOCONF_TEST=/usr/bin/...` survives intact), so a
  probe that sets paths via `env` is safe while the same value on the
  command line is not.

**Why:** it cost a wasted probe run and produced a misleading
"invalid autoconf executable" symptom that looked like a toolchain defect.

**How to apply:** prefix any Bash-tool command that passes an absolute
POSIX path as an argument to a native `.exe` with
`MSYS2_ARG_CONV_EXCL='*'`. Do not first conclude the target tool is broken.
Note this is a *harness* artifact, not a finding about the build host — the
same invocation driven from a real Cygwin shell or from Python `subprocess`
with a path built in code is unaffected.
