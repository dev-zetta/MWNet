# TES3MP 1.0 threat model

## Scope and assets

This model covers the TES3MP multiplayer client, dedicated server, protocol 11 transport, server-side Lua boundary and JSON/SQLite persistence. The protected assets are account credentials, server identity keys, player/world state, process availability, gameplay integrity and the user's trust decision for a server endpoint.

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
| Malformed, truncated or oversized packets | Sticky fail-closed decoder, fixed-width fields, explicit limits and no partial destination mutation | Implemented for the protocol 11 envelope and migrated packet boundary; exhaustive per-message migration remains a release gate |
| Spoofed player identity | Connection-bound sender identity; ignore client-supplied sender GUIDs | Implemented |
| Replay, interception or server impersonation | Authenticated encryption, signed ephemeral handshake, TOFU fingerprint pinning and hard mismatch failure | Implemented |
| Flooding and authentication CPU exhaustion | Per-connection byte/message/chat buckets, pre-KDF limiter and account-plus-IP lockout | Implemented; public-load tuning remains a gate |
| Credential disclosure | Passwords only inside encrypted sessions, 128-byte cap, protected temporary buffers, Argon2id and redacted logging | Implemented for native protocol 11 authentication |
| Legacy credential retention | Verify once, atomically replace with Argon2id, and avoid backups containing obsolete material | Implemented for the native account store |
| Unauthorized movement or actor simulation | Canonical movement bounds and expiring server-issued actor authority leases | Implemented foundation and live validation |
| Forged combat, inventory, jail, object or respawn outcomes | Intent/result messages and canonical server models | In progress; public testing is blocked until all listed outcome paths are server-owned |
| Lua exception terminating the server | Remove false `noexcept` boundaries, catch native/Sol/conversion/filesystem failures and return Lua errors | Implemented for audited boundaries; ongoing as APIs migrate |
| Corrupt or partial persistence | Bounded coalescing worker, sibling temporary file, flush, atomic replacement and one recoverable backup | Implemented for native account data and CoreScripts JSON saves; fault-injection matrix remains a gate |
| Resource exhaustion through allocations or queues | Checked protocol limits, bounded transport/persistence queues and file quotas | Partially implemented; world/object quotas and metrics remain gates |
| Dependency or licensing surprise | Pinned dependency manifest, generated SBOM, third-party notices and specialist legal review | Manifest present; independent legal review remains mandatory and unresolved |

## Security invariants

- Protocol 10 input is never decoded by a protocol 11 endpoint.
- No gameplay packet, mutation callback or relay is accepted before account authentication.
- A fingerprint mismatch cannot fall back to an unauthenticated connection.
- Decode failure cannot invoke C++, Lua or relay behavior with partially decoded state.
- Only the server may commit security-sensitive gameplay outcomes or persistent state.
- An interrupted persistence operation leaves either the previous complete record or the new complete record recoverable.
- Secrets and player-controlled strings are never used as logging format strings.

## Residual risks and release gates

The alpha still contains transitional client-outcome paths while canonical combat, container, object, effects and crime models are integrated. It has not completed the required decoder fuzz budget, eight-client soak, failure injection, independent security review or specialist review of TES3MP's additional GPL terms and third-party notices. These are explicit blockers, not accepted stable-release risks.

Public discovery is outside the 1.0.0 scope. A future service must use HTTPS, return signed server-identity metadata and receive a separate threat review.
