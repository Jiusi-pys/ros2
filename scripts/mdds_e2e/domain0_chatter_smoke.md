# Production domain-0 `/chatter` smoke

`run_domain0_chatter_smoke.sh` is the deliberately narrow release smoke that
uses the deployed production profile without changing its `domain 0` or its
literal `/chatter` route.  It is not part of `run_mdds_gw.sh`, whose normal
scenarios intentionally use isolated domains and run-unique topic names.

The gate runs two sequential legs through the existing single gateway on board
A:

1. Windows PC (`rmw_cyclonedds_cpp`) → A (`mdds_gateway`) → B (`rmw_mdds` over
   DSoftBus).
2. B (`rmw_mdds` over DSoftBus) → A (`mdds_gateway`) → Windows PC
   (`rmw_cyclonedds_cpp`).

Each endpoint transfers exactly ten `std_msgs/msg/ByteMultiArray` messages on
`/chatter`.  The probe payload contains a versioned `D0CH` header, the entire
run token, the direction, sequence number, deterministic body and CRC32.  The
machine result requires exact `10/10`, with no loss, reorder, CRC failure,
wrong token, wrong direction, malformed payload, or foreign `ByteMultiArray`
traffic.

## Invocation

Run it only during a domain-0 maintenance window, after the final deployment
has completed and no other board or PC ROS test is active:

```bash
cd /c/Users/17715/Documents/codes/M-DDS/ros2
MDDS_RUN_ID=c2m_route1_domain0_YYYYMMDDThhmmss \
MDDS_RUN_NONCE=c2mroute1_domain0_nonce_YYYYMMDDThhmmss \
MDDS_DOMAIN0_LOGROOT=/c/mdds-v11/c2m_route1_YYYYMMDDThhmmss/raw_logs/domain0 \
./scripts/mdds_e2e/run_domain0_chatter_smoke.sh
```

Before using HDC or launching any payload, the runner:

- acquires the existing exact-owner activity lock on A and B;
- rejects any visible board process that has the deployed MDDS/Cyclone library
  mapped and is in domain 0 (including absent/unreadable `ROS_DOMAIN_ID`);
- rejects a visible `com.kaihong.mdds.d0` marker in `/proc/net/unix`;
- rejects candidate Windows ROS/RMW/gateway processes and DDS-port listeners;
- checks the local and deployed SHA-256 values for `libmdds.so`,
  `librmw_mdds.so`, `librmw_cyclonedds_cpp.so`, `mdds_gateway`, the deployed
  DSoftBus RMW profile and the production gateway profile;
- hash-verifies the board probe, remote supervisor/spawn/self-test helpers and
  board-A CycloneDDS XML after transfer;
- records the SHA-256 of the main `run_domain0_chatter_smoke.sh` decision
  script as part of the run evidence.

The gateway starts with the installed
`share/mdds_gateway/mdds_gateway_ohos_dsoftbus.conf`; its required profile
fields are `cyclone_domain_id = 0`, `mdds_domain_id = 0`,
`mdds_transport = dsoftbus`, and exactly `topic = /chatter`.  Board B sources
the installed `ohos_dsoftbus.env`; the PC batch pins `RMW_IMPLEMENTATION` to
CycloneDDS and `ROS_DOMAIN_ID=0` with unicast discovery toward board A.

Each DSoftBus leg derives both local network IDs from the reciprocal `OnBind`
records, applies the production lexical ordering rule, requires at least one
successful `BindAsync` on the selected active endpoint, and requires exactly
zero `BindAsync` calls on the passive endpoint. Active-side retries are
allowed.

All board cleanup uses a run-scoped, create-only record containing supervisor
PID/start, child PID/start and an isolated process-group ID. Graceful cleanup
signals the supervisor, which forwards to the child; timeout cleanup targets
the verified process group and does not succeed until the supervisor, child
and every group member are gone. If that cannot be established, the activity
lock is retained as an unhealthy/quarantine marker. PC cleanup likewise
verifies its own guard process start time before using a process-tree stop.
The runner never searches or kills a process by image or command line.

## Evidence

The supplied log root receives these raw files:

- `domain0_board_a_preflight.log`, `domain0_board_b_preflight.log`, and
  `domain0_pc_preflight.log`;
- `artifact_hashes.txt`, `helper_transfer_transcript.txt`, `run_binding.txt`,
  `activity_locks.txt`, `dialer_decisions.txt`, and launch/cleanup records;
- one PC log, board-B log and gateway log for each direction;
- `domain0_machine_result.txt` containing both exact directional outcomes and
  the final `D0_DOMAIN0_SMOKE_RESULT` marker.

`D0_DOMAIN0_SMOKE_RESULT ... result=PASS` is meaningful only together with the
preflight and raw endpoint/gateway logs.  It is a smoke gate, not a substitute
for the longer isolated gateway, DSoftBus, RMW, or capacity suites.

## Static check

This action does not access HDC, boards, PC ROS, or the network:

```bash
./scripts/mdds_e2e/run_domain0_chatter_smoke.sh --validate-only
```

The platform does not expose a complete public enumeration API for arbitrary
DSoftBus Socket sessions.  The preflight therefore blocks on every visible
MDDS/Cyclone process and the available domain-0 session marker, rather than
claiming a global absence proof.  A maintenance window is still required:
another uncooperative participant could start after preflight, in which case
the payload token and exact counts protect the result parser but do not turn
the run into exclusive-domain evidence.

The board-side hard-stop fault injection is a separate destructive test and is
not run by `--validate-only` or by the payload smoke itself. Run
`domain0_remote_guard_selftest.sh` in an isolated maintenance test phase; it
launches a child that ignores TERM and requires the cleanup helper to remove
the supervisor, child and entire process group before reporting PASS.
