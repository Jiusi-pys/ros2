# Python executable launchers on KaihongOS

The installation finalizer must generate `#!/bin/env python3.12`.
The two tested RK3588A boards provide `/bin/env` as a toybox link and do not
provide `/usr/bin/env`. Using the latter makes an existing executable script
fail with `FileNotFoundError` at `subprocess.Popen`, including through
`ros2 run demo_nodes_py talker`.

Design: retain the generated entry-point body and environment-selected target
Python interpreter; change only the OS-specific env path. The finalizer may
upgrade a prior generated launcher with exactly the corresponding old header
and body. It continues to reject native executable collisions. The normal
`build_ohos.sh` workflow invokes this finalizer before packaging.

Tests are in `scripts/test_finalize_ohos_install.py`. Before implementation,
new-launcher and existing-launcher upgrade checks both failed; after the fix,
all three tests pass, including native-executable collision protection.

Real-board evidence from 2026-09-07:

- `cli_run_languages_20260907_01` reproduced the missing-interpreter failure.
  The script existed, had execute permission and the wrong `/usr/bin/env`
  header. Fourteen raw failure artifacts were fetched with matching hashes in
  `../verification_evidence/goal1_20260906/run_languages_01_failure`.
- `cli_run_languages_20260907_02` used the corrected generated launcher.
  Both actual Python `ros2 run` processes returned 0, published through the
  private rmw_mdds/MDDS DSoftBus broker, and exited. Their interpreter argv
  begins `python3.12` followed by the run-owned `demo_nodes_py/talker` path.
  The overall CLI gate still failed because C++ and Python retirement records
  lacked distinct identities. That evidence gap is separate work; this run
  is not a complete CLI acceptance claim.

The full multi-language run regression is maintained by the process CLI
acceptance harness. This launcher fix does not imply a completed unified
deployment or a complete ROS 2/graph release.
