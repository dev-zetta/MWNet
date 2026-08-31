# TES3MP 1.0 Lua API compatibility

Protocol 11 changes the network and trust boundaries, not the safe TES3MP 0.8.1 server-script vocabulary. An automated name-surface comparison against tag `tes3mp-0.8.1` finds all 833 previously bound function and callback names still present in 1.0.0-alpha.1. Existing signatures remain the compatibility contract unless a row below explicitly narrows when a call is legal.

| Surface | 0.8.1 compatibility | 1.0 behavior |
| --- | --- | --- |
| Actor, book, cell, class, chat, dialogue, faction, GUI, item, mechanics, miscellaneous, position, quest, record, shapeshift, settings, spell, stat, object and worldstate APIs | Names and signatures retained | Mutations require an authenticated player and the relevant player, actor or cell authority |
| Timer and server utility APIs | Names and signatures retained | Exceptions are reported as Lua errors instead of terminating the server |
| Deprecated aliases already present in 0.8.1 | Retained for 1.x | Supported with deprecation notices; removal is deferred to a later major version |
| `OnPlayerConnect(pid)` | Retained | Runs only after native account authentication; it is no longer a pre-login mutation hook |
| `OnPlayerFinishLogin(pid)` | Retained | Runs after initial state delivery, as in CoreScripts 0.8.1 |
| `OnTransportConnect(pid)` | New | Limited observation before account authentication; mutating player/world APIs raise a Lua error |
| `OnPlayerAuthenticated(pid, accountName, isNewAccount)` | New | Marks the secure authentication boundary, before `OnPlayerConnect` |
| `OnPlayerMovementViolation(pid, reason, actualDistance, allowedDistance, violationCount)` | New | Observes rejected movement and may apply script policy; it cannot make the rejected snapshot canonical |
| `OnPlayerInventoryIntent(pid)` | New | Runs before canonical inventory commit; `false` denies, while `true` or `nil` allows native validation |
| `OnPlayerInventoryIntentRejected(pid)` | New | Optional cleanup notification when a script-modified inventory intent fails canonical validation |
| `OnPlayerAttackIntent(pid, isRanged, targetPid, refNum, mpNum, strength)` | New | Runs before server combat resolution; `false` denies, while `true` or `nil` allows the sanitized intent |
| `OnPlayerAttackIntentRejected(pid, reason)` | New | Reports script denial or native validation failure without applying a client-claimed outcome |
| `GetPlayerAttackStrength(pid)` / `SetPlayerAttackStrength(pid, strength)` | New | Lets an attack-intent validator inspect or modify normalized strength; native validation runs again before resolution |
| `WriteFileAtomically(path, contents)` | New | Synchronous atomic write below the configured server data directory |
| `QueueFileWrite(path, contents)` | New | Bounded, coalesced atomic write below the configured server data directory |
| `FlushPersistence()` | New | Waits for queued persistence writes to finish |

## Lifecycle order

For a successful new or returning protocol 11 session, callbacks occur in this order:

1. `OnTransportConnect(pid)`
2. transport identity and encrypted-session establishment
3. content verification
4. account registration or authentication
5. `OnPlayerAuthenticated(pid, accountName, isNewAccount)`
6. `OnPlayerConnect(pid)`
7. initial player/world state delivery
8. `OnPlayerFinishLogin(pid)`

Duplicate or out-of-order initialization is rejected. Gameplay mutation and relay APIs are unavailable before step 4. A script error at a native boundary is contained and returned to Lua; it does not grant authority or make a rejected operation valid.

## Migration guidance

- Move connection telemetry that does not mutate state to `OnTransportConnect`.
- Keep account-dependent setup in `OnPlayerConnect`, which now has an authenticated identity.
- Use `OnPlayerAuthenticated` when a script needs the canonical account name or whether registration just occurred.
- Use `OnPlayerInventoryIntent` to allow or deny an inventory request before commit. Existing inventory-change setters may propose a modified intent, which is validated again. `OnPlayerInventory` keeps its 0.8.1 signature and now runs after canonical commit.
- Use `OnPlayerAttackIntent` to inspect or deny a sanitized combat request. `SetPlayerAttackStrength` may modify its normalized strength; the server validates the result, rolls hit chance, computes damage and publishes canonical health.
- Treat incoming gameplay callbacks as requests. Validators may deny an intent, but only a native canonical result may change protected server state.
- Do not rely on an old callback being able to mutate another player or an actor outside the caller's authority lease.

The compatibility promise applies to safe calls. Behavior that depended on malformed packets, unauthenticated mutation, client-selected identity, unchecked cross-player access or a native crash is intentionally not preserved.
