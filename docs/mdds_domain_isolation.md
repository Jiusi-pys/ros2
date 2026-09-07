# Domain isolation design and tests

Before implementation: run real native DSoftBus brokers for domains 175 and
176 on both RK3588A boards. Each source process creates one context per domain
using the explicit rclpy domain argument and verifies `Context.get_domain_id()`.
Both domains use identical node names, namespaces, topic names and types.
Enclaves and payloads identify the domain so a merged/replaced foreign graph
or cross-domain delivery cannot masquerade as a same-domain positive result.

Each domain must independently discover exactly the two intended test nodes
and their 18 endpoints, with one same-domain service and client per role and
two sample publishers/subscribers. Three samples per board per domain are
actually published. Every receiver must get exactly its domain's six local/
peer samples, never another domain's samples. RPC operands also encode the
domain and must be checked by the actual peer server. Both brokers' Socket
bindings, native SDK/library/process provenance and cleanup must be retained.

All four publishers finish sending before an observation window of at least
one second begins. Nodes/endpoints/GIDs are checked throughout this window.
Same-domain positive controls are mandatory in both domains; empty discovery
or an unstarted second broker cannot pass. The test program begins in
`test_domain_isolation_contract.py`. The initial missing-module RED was retained.

First native run `domain_isolation_20260908_01` completed the domain traffic,
but the secondary broker inspector still required the primary daemon's fixed
ownership tag. Its live inspection record was missing, so collection failed
and the run was not accepted. The inspector now uses the exact selected role's
tag. Existing diagnostics were preserved; no inspection was reconstructed for
an exited process.

Final HDC run `domain_isolation_20260908_02` passed. Both explicit domain IDs
were read back correctly. Each domain exposed exactly its two nodes and 18
endpoints; domain participant GID sets were disjoint despite identical names
and types. Every receiver obtained exactly six own-domain local/peer samples,
and each context completed its domain-tagged peer RPC. The observation windows
were 1.027342270 s and 1.031737687 s after both boards completed all sends.

The second broker on each board was verified live using exact PID/start,
`--domain 176`, its `d176/b.sock` path, the native SDK/library hash, and actual
Socket/Listen/Bind records. Both domains' brokers and all source/CLI processes
exited normally, with no remaining broker resources.

Three contract tests, 14 receipt adversaries, 18 generic acceptance tests and
11 baseline broker adversaries passed. The receipt-test copy initially omitted
the broker executable needed for hash verification; that test setup was fixed
to retain it, with the failed test log preserved. No validation was removed.

- Manifest: `2c0be7ad887520df270fc76c3a04ff6bbf3aa73129a95e4c44c5fb38ecb4589a`.
- Domain receipt: `87466feb88df8b9fa4d0537cfe7159a7b6a525e3c3dd8025fe5278c83381de7a`.

Coverage is 95/98 across frozen runs. Dedicated transport cases, advanced
API/liveliness work and a unified release remain open. Gateway remains outside
the current goal. This feature adds acceptance evidence; the existing native
MDDS/RMW implementation passed the isolation scenario.
