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

| Capability | Current status | Configuration boundary | Required acceptance evidence |
| --- | --- | --- | --- |
| `rcl`, `rclcpp`, parameters, lifecycle | Pending support gate | aarch64-linux-ohos, musl | clean lock replay and full cross-build; package CTests; C++ pub/sub, service and action round trips; clean shutdown |
| CPython 3.12, `rclpy`, `ros2cli` | Pending support gate | Official CPython 3.12.7 plus fixed `Jiusi-pys/python` OHOS configuration; target SOABI `cpython-312-aarch64-linux-ohos` | Python source-build receipt and import gate; Python pub/sub and service/action; `ros2 --help`, node/topic/service/action CLI on a clean board |
| CycloneDDS UDP | Pending support gate | multicast or the checked-in peer profile | RMW suite plus single-board and two-board C++/Python communication from the same release archive |
| Fast DDS UDPv4 | Pending support gate | `SHM_TRANSPORT_DEFAULT=OFF`; generic environment selects UDPv4 | focused repair verified: content-filter 1/1, subscription 31/31, cpp/dynamic RMW suites 16/16 each, default A-to-B pub/sub; full clean-release acceptance still required |
| Default RMW | Pending support gate | `rmw_fastrtps_cpp`, compiled into `rmw_implementation` | focused two-board run with `RMW_IMPLEMENTATION` unset passed; complete generic commands must pass from the same clean-release archive |
| rosbag2 SQLite3 | Pending support gate | local board storage | record, inspect and replay C++ and Python messages; output digest retained |
| LTTng/`ros2 trace` | Pending support gate | tracked LTTng UST musl patch; non-interactive start/stop | clean dependency build; sessiond/consumer startup; trace start/stop and non-empty CTF event decode |
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
