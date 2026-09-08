# KaihongOS (RK3588A) ROS 2 Jazzy Support Matrix

This document is the release boundary for the generic ROS 2 port. It does not
make build success stand in for board-runtime acceptance. A row is
**Supported** only after its required gate has passed from a clean lock replay,
a clean cross-build and a clean-board deployment whose evidence records the
source, SDK and artifact hashes.

Status meanings:

- **Verified supported**: every listed build and runtime gate passed for one
  hash-bound release candidate. This label must never be inferred from source
  review, a previous install tree or build success alone.
- **Pending support gate**: intended core release scope, but the current repair
  has not yet produced the required clean-build and clean-board evidence. It
  must not be advertised as supported.
- **Blocked**: intended scope with a known release-blocking gate still open.
- **Experimental**: evaluation-only; failure does not block the core release
  and no compatibility guarantee is made.
- **Out of scope**: deliberately disabled or not shipped; it must not be
  advertised as available.

The status below is the current evidence verdict for this repaired input set,
not an aspirational feature list. A row may move to **Verified supported** only
when the required gate is present in the hash-bound release evidence.

Core verdict updated on 2026-09-08 for archive
`2ab1a4a02c405ee98b5c27377f7f74cb355f5d4d56e80ecd6b4f49c64a807dfd`
on two KaihongOS 6.1.0.04 RK3588A boards. See
`rk3588a_core_delivery_20260908.md` for exact source/SDK/runtime hashes, test
counts, retained skips and rollback locations. Verification is limited to that
documented acceptance matrix, not the entire upstream test suite.

| Capability | Current status | Configuration boundary | Required acceptance evidence |
| --- | --- | --- | --- |
| `rcl`, `rclcpp`, parameters, lifecycle | Verified supported | aarch64-linux-ohos, musl | clean replay/build; recorded native API and signal tests; C++ pub/sub, services/actions, lifecycle and shutdown passed |
| CPython 3.12, `rclpy`, `ros2cli` | Verified supported | Fixed-source CPython 3.12.7; target SOABI `cpython-312-aarch64-linux-ohos` | source receipt, full Python-tree checks, service-lifetime tests, Python messaging/services/actions and cold default CLI passed |
| CycloneDDS UDP | Verified supported | `rmw_cyclonedds_cpp`; SHM disabled | both boards: 131 native API cases passed, 4 positive loan cases capability-skipped; C++/Python end-to-end matrix passed |
| Fast DDS UDPv4 | Verified supported | `rmw_fastrtps_cpp`; `SHM_TRANSPORT_DEFAULT=OFF` | both boards: 135 native API cases passed; full C++/Python end-to-end matrix passed |
| Default RMW | Verified supported | `rmw_fastrtps_cpp`, compiled into `rmw_implementation` | native default identifier and cross-board communication with RMW/transport environment overrides unset passed |
| rosbag2 SQLite3 | Verified supported | local storage; hash-bound ROS-only Python global-RTTI policy | C++/Python record, inspect, replay and shutdown passed under both RMWs |
| LTTng/`ros2 trace` | Verified supported | tracked musl patch; non-interactive, private mount namespace | clean dependency build; session/consumer lifecycle, real CTF ROS event decode and cleanup passed for both RMWs |
| Qt5/PyQt5/rqt/turtlesim, headless | Experimental | `offscreen` QPA only; no screen or input-device claim | deterministic Qt/PyQt recipe, widget smoke, plugin discovery and bounded offscreen runs |
| Qt/RViz visible GUI | Out of scope | no supported X11/Wayland or native window-system integration is supplied | none; offscreen process survival is not visible rendering evidence |
| RViz2/OGRE GLES2, headless | Experimental | Mali EGL pbuffer/offscreen; no visual-reference guarantee | clean OGRE build, process-survival gate, Ogre exception scan and topic/TF activity |
| DDS shared memory (SHM)/iceoryx | Experimental | Fast DDS: `SHM_TRANSPORT_DEFAULT=OFF`; CycloneDDS: `ENABLE_SHM=OFF`. RouDi is needed for the CycloneDDS/iceoryx profile, not Fast DDS SHM | separate opt-in transport and cleanup gates; CycloneDDS additionally requires RouDi and loaned-message evidence; no current release evidence |
| DDS Security, TLS, DTLS and SROS2 | Out of scope | Fast DDS and Qt are built without SSL; no DDS certificate/key lifecycle is supplied. CPython's separate `_ssl` module does not enable DDS security | none; a future status change requires DDS-specific crypto integration, security tests and threat review |
| RTI Connext DDS | Out of scope | proprietary dependency is not provided | none |
| Gazebo and desktop display-server integration | Out of scope | no X11/Wayland display stack is supplied | none |

## Release gates

The CPython source profile builds 70 dynamic extensions, including `_ctypes`,
`_socket`, `select`, `_posixsubprocess`, `zlib`, `_bz2`, `_lzma`, `_sqlite3`,
`_ssl` and `_hashlib`. It does not provide the optional native extensions
`_curses`, `_curses_panel`, `_dbm`, `_gdbm`, `_tkinter`, `_uuid`, `nis`, `readline`
or `spwd`; `_scproxy` is not applicable to OHOS. Their absence is not a claim
that pure-Python fallbacks such as `uuid` or `dbm.dumb` are unavailable.

CPython's OpenSSL configuration and trust paths are deployment-owned. The
default CA file/directory are empty: applications must explicitly provide their
trusted CAs. A local TLS test with an explicit test CA does not prove general
Internet trust-store readiness, and does not enable DDS Security/TLS. The
source-build receipt records the actual SDK/compiler used for Python; it is a
separate input from the SDK used for the ROS/dependency cross-build.

1. `python scripts/verify_release_inputs.py` validates the immutable ROS lock,
   patch metadata, target dependency source lock and transitive ROS vendor
   source lock. `scripts/test_ohos_vendor_lock.py` exercises the CMake resolver
   and rejection of unlocked versions; the full build must actually download
   and compile those sources too.
2. `scripts/verify_fresh_lock_replay.sh <empty-dir>` imports the pinned lock and
   verifies every replayed repository tree. CI runs this from an empty directory.
3. `target_deps_src/build_all_clean_ohos.sh` is the release dependency entry
   point. It refuses an existing install prefix or ignored source cache, invokes
   all recipes with HTTPS inputs locked by SHA-256 or Git commit, and seals the
   source journal, recipe/SDK/Python inputs and exact installed file inventory.
4. The dependency pipeline starts with an empty install prefix. The ROS 2
   cross-build starts with empty build/log directories and that verified
   dependency-only install prefix. Its
   result is accepted only when the release evidence binds the meta-repository
   commit, lock digest, patch-set digest, SDK digest and install archive digest.
5. Board evidence must identify the RK3588A device serial and OS build, selected
   RMW, test command, start/end time, exit status and the same install digest.
6. A clean shutdown means every process created by the gate exits, no owned
   process remains and no participant/session teardown error is present.

Experimental rows may be promoted only by changing this file in the same
reviewed change that adds their deterministic build and runtime gates.
