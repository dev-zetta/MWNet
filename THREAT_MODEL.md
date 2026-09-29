# MWNet 1.0 threat model

## Scope and assets

This model covers the MWNet multiplayer client, dedicated server, protocol 12 transport, server-side Lua boundary and JSON/SQLite persistence. The protected assets are account credentials, server identity keys, player/world state, process availability, gameplay integrity and the user's trust decision for a server endpoint.

Morrowind content, plugins and server-side Lua scripts are trusted inputs selected by the operator. A compromised host, malicious administrator, malicious game plugin or arbitrary native Lua module is outside the protocol's protection boundary.

## Trust boundaries

1. Unauthenticated network peers may reach only transport setup and tightly limited pre-authentication messages.
2. An encrypted GameNetworkingSockets session establishes confidentiality and binds traffic to a transport connection.
3. Content verification and account authentication precede gameplay packet handling and Lua mutation.
4. Authenticated clients remain untrusted for gameplay outcomes. They submit intents and predicted presentation; the server owns persistent state and authoritative results.
5. Server Lua is trusted extension code, but native bindings validate lifecycle, ownership, sizes and exceptions before native state is mutated.
6. Persistence crosses from process memory to an operator-controlled filesystem and must survive interrupted or failed writes.

## Threats and controls

| Threat | Required control | Current alpha status |
| --- | --- | --- |
| Malformed, truncated or oversized packets | Sticky fail-closed decoder, fixed-width fields, explicit limits and no partial destination mutation | Implemented across the protocol 12 packet boundary; the required long-running fuzz campaign remains a release gate |
| Spoofed player identity | Connection-bound sender identity; ignore client-supplied sender GUIDs | Implemented |
| Replay, interception or server impersonation | Authenticated encryption, signed ephemeral handshake, TOFU fingerprint pinning and hard mismatch failure | Implemented |
| Flooding and authentication CPU exhaustion | Per-connection byte/message/chat buckets, pre-KDF limiter and account-plus-IP lockout | Implemented; public-load tuning remains a gate |
| Credential disclosure | Passwords only inside encrypted sessions, 128-byte cap, protected temporary buffers, Argon2id and redacted logging | Implemented for native protocol 12 authentication |
| Legacy credential retention | Verify once, atomically replace with Argon2id, and avoid backups containing obsolete material | Implemented for the native account store |
| Unauthorized movement or actor simulation | Canonical movement bounds and expiring server-issued actor authority leases | Implemented foundation and live validation |
| Forged combat, inventory, jail, object or respawn outcomes | Intent/result messages and canonical server models | Canonical ledgers and validation are active for the listed paths; integration, adversarial and soak evidence remain release gates |
| Lua exception terminating the server | Remove false `noexcept` boundaries, catch native/Sol/conversion/filesystem failures and return Lua errors | Implemented for audited boundaries; ongoing as APIs migrate |
| Corrupt or partial persistence | Bounded coalescing worker, sibling temporary file, flush, atomic replacement and one recoverable backup | Implemented for native account data and CoreScripts JSON saves; fault-injection matrix remains a gate |
| Resource exhaustion through allocations or queues | Checked protocol limits, bounded transport/persistence queues, world/object quotas, file quotas and runtime metrics | Implemented; public-load tuning and soak evidence remain gates |
| Dependency or licensing surprise | Pinned dependency manifest, generated SBOM, third-party notices and specialist legal review | Manifest present; independent legal review remains mandatory and unresolved |

## Security invariants

- Gameplay envelopes with a version other than 12 are rejected; discovery API v1 does not change the gameplay wire format.
- No gameplay packet, mutation callback or relay is accepted before account authentication.
- A fingerprint mismatch cannot fall back to an unauthenticated connection.
- Decode failure cannot invoke C++, Lua or relay behavior with partially decoded state.
- Only the server may commit security-sensitive gameplay outcomes or persistent state.
- An interrupted persistence operation leaves either the previous complete record or the new complete record recoverable.
- Secrets and player-controlled strings are never used as logging format strings.

## Residual risks and release gates

The alpha has canonical combat, movement, inventory/equipment, container, object, active-effect, death/respawn and justice foundations, but some game-event provenance paths still require adversarial integration coverage before they can be treated as release evidence. Earlier fuzz, integration, persistence and paired-soak evidence is recorded in [the release gates](RELEASE_GATES.md); it applies to the revisions it tested. The final release candidate still needs its required validation evidence, independent security review and specialist review of MWNet's additional GPL terms and third-party notices. These remain explicit release blockers.

Public discovery is included in the 1.0.0 scope and requires a separate independent security review before public deployment. HTTPS authenticates the directory origin; a server-signed, bounded listing binds metadata to the existing game identity. A fresh, single-use challenge binds each mutation to the directory origin, operation, key and requesting source. The directory confirms identity and reachability through the encrypted handshake without accessing game accounts. This does not independently certify advertised protocol/content compatibility, server conduct or player counts; normal join checks remain authoritative.

Production verification accepts numeric global-unicast addresses only, with two concurrent checks and four-second handshake deadlines. DNS names in announcements are intentionally unsupported to avoid DNS rebinding and resolver stalls. Private addresses require explicit `--local-test`. The proxy is the only public entry point; `--trust-proxy` is valid only when direct backend access is prevented. Caddy overwrites `X-Real-IP`. Public listings, challenge state, rate buckets, request sizes, HTTP connections and SQLite growth have explicit caps; details and residual limits are documented in [public discovery](docs/public-discovery.md).

The directory may suppress listings, replay previously signed metadata for its origin, or become unavailable; clients retain direct connections. A signature authenticates the metadata's author, not its freshness in a compromised directory. Directory metadata never grants trust or overwrites a saved fingerprint, and the actual game connection must present the advertised identity. Public launch remains gated on staging acceptance, operator abuse controls, HTTPS/DNS checks, platform validation and independent review. The service does not relay game traffic, traverse NAT or centralize accounts.
