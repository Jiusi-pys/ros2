---
name: absolute-paths-required-in-bash-tool
description: Bash-tool cwd drifts between calls in this workspace; use absolute paths, and pass Windows-form (C:/...) paths to the pixi python.exe, not /c/... MSYS paths.
metadata:
  type: project
---

In this agent thread the Bash tool's working directory is **not** stable: it was
observed to change mid-session to a subdirectory (`ros2/src/Jiusi-pys/openjdk_ohos`)
with no `cd` issued, silently turning correct relative paths into
"file not found" errors.

Two rules follow:

1. Always use absolute paths in Bash calls (`R=/c/Users/.../ros2; ... "$R/..."`).
2. When invoking the pixi interpreter `ros2/.pixi/envs/default/python.exe`, pass
   **Windows-form** paths (`C:/Users/...`); Python on this host cannot resolve
   MSYS-style `/c/...` paths and raises `FileNotFoundError`.

**Why:** misreading a cwd-drift "file not found" as evidence of missing or deleted
artifacts would corrupt a test report; it looked briefly like the K build tree had
vanished during the F106 v5 gate.

**How to apply:** before concluding an artifact is missing, re-check it with an
absolute path. Bash `ls`/`cmp`/`git` accept `/c/...`; Python needs `C:/...`.
See also [[cygwin-package-names-differ-from-tool-names]].
