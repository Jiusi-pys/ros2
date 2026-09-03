# M-DDS verification run contract — hash-sealed local

Run ID: `<RUN_ID>`  
Prepared UTC: `<UTC_TIMESTAMP>`  
Prepared by: `<NAME>`

## Evidence boundary

This contract describes a planned or completed verification run. Its manifest
is **hash-sealed local**, not immutable: local files and SHA-256 values become
tamper-evident only after their digests are retained by an independent signer
or WORM-capable store. Do not describe the manifest, its directory, or its tar
archive as immutable unless that external retention is recorded separately.

The collector creates one Git bundle for each required repository (`ros2`,
`mdds`, `rmw_mdds`, and `rmw_cyclonedds`) from the captured HEAD. It validates every bundle in a
fresh bare Git repository by fetching the advertised ref and checking the exact
HEAD and tree. The bundle covers committed Git objects; the manifest's binary
diffs and untracked-file inventory remain the evidence for any dirty worktree
content.

## Source and build identity

| Repository | Captured HEAD | Worktree status | Git bundle verification |
| --- | --- | --- | --- |
| ros2 | `<SHA>` | `<CLEAN / DESCRIBE>` | `<PENDING / PASS>` |
| mdds | `<SHA>` | `<CLEAN / DESCRIBE>` | `<PENDING / PASS>` |
| rmw_mdds | `<SHA>` | `<CLEAN / DESCRIBE>` | `<PENDING / PASS>` |
| rmw_cyclonedds | `<SHA>` | `<CLEAN / DESCRIBE>` | `<PENDING / PASS>` |

- Build host and SDK: `<HOST / SDK VERSION / PATH>`
- Build commands and complete transcripts: `<PATH OR INPUT NAME>`
- Deployed artifact SHA-256 values: `<libmdds.so / librmw_mdds.so / librmw_cyclonedds_cpp.so / mdds_gateway>`
- Gateway profile SHA-256 and runtime identification: `<VALUE / LOG LOCATION>`

## Network and device topology

- Board A HDC serial: `3e01ff55454d202020104033bf453b00`
- Board B HDC serial: `3e01ff55454d202020104433991c3b00`
- PC address and selected route: `<CAPTURED VALUE>`
- Board A addresses/routes: `<CAPTURED VALUE>`
- Board B addresses/routes before isolation: `<CAPTURED VALUE>`
- Exact reversible isolation action and restoration evidence: `<COMMAND LOG / RESULT>`
- Scope of the negative reachability proof: `<BOUND SOCKET/PROTOCOL/TIMEOUT>`

Do not claim that the PC has no route to the 192.168.77.0/24 network unless
all competing host interfaces/routes were explicitly inspected and, where
authorized, disabled. A board-side removal of one 192.168.8.0/24 address proves
only that exact address-state change plus the specified bound reachability test.

## Required verification gates

- [ ] MDDS and gateway board package tests, with raw per-board logs.
- [ ] DS-01 through DS-07, including the pending-cleanup fault-injection gate.
- [ ] GW-01 through GW-08 plus the C2M sustained/history-cap gates for this run.
- [ ] Official RMW suite, with FastDDS baseline or a named, independently
      evidenced exemption for any known unrelated failure.
- [ ] Exact production-domain `/chatter` smoke, recorded separately from
      isolated-topic tests.
- [ ] Isolated-topology E2E after the Board B 192.168.8.0/24 address is
      reversibly removed, with pre/post addresses, routes, negative direct-PC
      probe, DSoftBus callback chain, and final exact payload results.
- [ ] All result counts, timeouts, failures, skips, CRC/order checks, and
      artifacts are represented by raw inputs, not only a summary report.

## Collection and release decision

- Collector invocation and output directory: `<COMMAND / OUTPUT>`
- Manifest SHA-256: `<VALUE>`
- Archive SHA-256: `<VALUE>`
- External immutable retention (WORM/signed record): `<NOT YET DONE OR LOCATION>`
- Independent non-author reviewer: `<NAME / DATE / COMMIT SHA / MANIFEST SHA>`
- Release decision: `<NOT SIGNED / CONDITIONAL / SIGNED>`

The prior evidence bundle is not amended or overwritten. Any source, build,
configuration, deployment, or test change requires a new run ID and a new
hash-sealed-local manifest.
