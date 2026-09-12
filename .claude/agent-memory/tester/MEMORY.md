# Tester agent memory — M-DDS ros2 workspace

- [Cygwin package names differ from tool names](cygwin-package-names-differ-from-tool-names.md) — `awk` is not a Cygwin package; `gawk` is, so `cygcheck -c` inventory checks must not use tool names.
- [Absolute paths required in Bash tool](absolute-paths-required-in-bash-tool.md) — cwd drifts between calls; use absolute paths, and `C:/...` (not `/c/...`) for the pixi python.exe.
- [Git Bash mangles POSIX argv](gitbash-mangles-posix-argv.md) — MSYS2 rewrites `/usr/...` args before native `.exe` tools see them; prefix `MSYS2_ARG_CONV_EXCL='*'`. UNVERIFIED (relocated from a stray path on 2026-09-10); authoritative candidate entry is `docs/knowledge/investigations/code/CODE-005-windows-bash-tool-harness-traps.md` — reproduce before relying on it.
