# Discovery OFF: design and tests before implementation

The MDDS participant already has a discovery-off gate, but the RMW DSoftBus
profile validator rejects OFF before reaching it. OFF is not an IP-scope
request: it can disable announcements and remote discovery while retaining the
same strict DSoftBus transport configuration. Permit OFF for implicit and
explicit profiles; continue rejecting unsupported IP scopes and all UDP/mixed
transport selections. The previous OFF rejection test is replaced by a positive
capability test that also requires DSoftBus-only flags, before implementation.

Physical test design: each board creates an OFF context with two local nodes
and an explicitly enabled context. All publishers/subscribers use matching
topic names and types. OFF must see only its own context's nodes/endpoints and
receive only its local publisher's exact samples. Enabled contexts must see
each other and exchange exact peer samples, while never discovering OFF nodes
or receiving OFF samples. Local OFF services must work; clients crossing the
context boundary must remain unavailable. Both boards publish real negative
control samples before a bounded observation interval. Final cleanup must
remove all temporary entities before the baseline continues.

Native RED `discovery_off_red_20260908` reproduced the profile rejection in
both policy/config targets. GREEN `discovery_off_green_20260908` passed all
9 board targets, with 6 existing host-only skips. The production change permits
OFF without changing any DSoftBus/UDP backend flag. The existing MDDS discovery
gate performs the isolation.

HDC run `discovery_off_20260908_01` passed on both boards. OFF views contained
only their own 2 nodes/16 endpoints, while enabled views contained the two
enabled nodes/26 endpoints. Each OFF subscriber received 3 local samples and
no foreign samples; each enabled subscriber received the exact 6 local/peer
samples. OFF local RPCs and enabled peer RPCs passed; the four local/remote
cross-boundary client checks remained false. Both sides sent all negative and
positive controls before observation windows of 1.016353436 s and 1.004316934 s.

Three independent contract tests, 13 receipt adversaries, 18 generic tests and
11 enclosing broker adversaries passed. Native process exits and owned cleanup
were normal. Package source/RED/GREEN details are in
`rmw_mdds/docs/discovery_off.md`.

- Manifest: `4635ce3125e2a648c19e6f0b8bd95b9b629834b36b7de7f1a7be886f26c501c4`.
- Receipt: `7300f2945ff6722fb0ad3c6311f734249e40b03f1c70ea9487ad1f69ef573092`.

Coverage is 94/98 across frozen runs. Domain isolation, dedicated transport
cases, advanced APIs/liveliness and a unified release remain open. Gateway is
outside the current goal.
