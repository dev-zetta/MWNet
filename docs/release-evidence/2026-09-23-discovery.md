# Public discovery API v1: local Linux acceptance

> Historical pre-rebrand evidence: LegacyMP/legacy-mp are display aliases for the former fork name in identifiers, commands, and artifact paths. They are not renamed artifacts or MWNet validation results. Hashes, revisions, measurements, and exit codes are unchanged; consult Git history for the original labels.

This evidence covers the discovery implementation on top of `f5b75a31eb922b7c1289e6c98a0d028dbf73962a`, tested on 2026-09-22 UTC / 2026-09-23 Europe/Prague. The worktree includes the new directory, announcement worker, public browser, neutral `GameEndpoint` naming, and deployment tooling. Gameplay protocol remains 12. No public hostname or VPS has been selected or deployed. Windows/macOS, independent security review, public staging, legal and beta gates remain open.

The Linux build used Ubuntu 24.04, Clang 18 for the full client/server and native/sanitizer tests, and the pinned Docker build for the standalone release directory. The final directory test image is `legacy-mp-directory:gate`, manifest `sha256:7428e80eea5f1edb3fd412f483f557761fb487a32102684697b3299da6740a5b`. It runs with a read-only root filesystem, UID 10001, a 512 MiB memory limit, two CPUs, a 96-process limit and a persistent disk volume. The pinned Caddy 2.10.2 proxy has a separate 128 MiB limit, bounded logs and the checked-in local configuration. Its public test CA was explicitly trusted; TLS certificate and hostname checks remained enabled. No test endpoint was exposed outside the local Docker fixtures.

| Acceptance check | Local result |
| --- | --- |
| Full client/server build and six native CTest suites | Passed; 47.67 seconds for CTest after the disconnect/reconnect fix |
| ASan/UBSan discovery and headless suites | Passed; 21.08 seconds, leak detection enabled and UBSan configured to halt on error |
| Signed registration, tampering, replay, challenge expiry, wrong identity, endpoint change and withdrawal | Passed in the native discovery suite |
| Lease expiry, restart verification, backup restoration, pagination and input/resource caps | Passed in the native discovery suite |
| Real encrypted reachability and saved-key conflict | Passed in the native and sanitizer discovery tests; first-use confirmation remains separate from the expected listing identity |
| Real server announcement and public browser | Passed; real fixture listed its name, capacity, protocol, ordered content count and password requirement |
| Incompatible advertised protocol | Graphical browser displayed the warning and refused selection of a locally signed protocol-11 declaration; this is advisory metadata, not an old-protocol game endpoint |
| Content mismatch | Real game server rejected the client with missing expansions; repeated rejected joins released and reused player slot 0 |
| Password protection | Missing access password was rejected; valid synthetic credentials passed content and account checks |
| Compatible graphical join | Passed through the public listing, character creation and entry into the game world after the Lua synchronization fix described below |
| Slow directory request and cancellation | Search remained editable while the local backend was paused; cancelling exited the client in 0.912 seconds |
| Directory outage and recovery | Browser reported the outage; direct connection remained available; the established graphical game continued receiving movement through a 70-second directory outage |
| Return from game to browser | Passed after orderly shutdown of the isolated game-server fixture |
| Private operator controls | Endpoint and identity block/unblock commands passed; a blocked endpoint disappeared and could register again after unblocking |
| Online backup and restored service | Passed against the frozen executable; restored SQLite integrity was `ok`, and the restored listing passed fresh endpoint verification |
| Deployment definitions | Local Compose validated; production Compose validated with example settings and rejected a missing hostname; Caddy served only `/v1/*`, returning 404 for public `/healthz` |
| Two-hour bounded churn/outage gate | Passed; 3,562 successful cycles, recovery from the injected outage, all process exit codes 0, and zero retained state after cleanup |

The [retained terminal manifest and resource summary](2026-09-23-discovery.json) record the final run from 2026-09-22 22:06:59 UTC to 2026-09-23 00:08:10 UTC: 7,200 seconds requested and 7,269.84 seconds elapsed including startup and cleanup. The workload exited 0; both directory processes exited 0. Sixteen request failures occurred during the deliberate 30-second outage, followed by successful recovery. Neither the workload container nor its HTTPS proxy was OOM-killed or restarted.

Across 3,598 resource samples, peak directory RSS was 14,495,744 bytes (13.82 MiB), peak workload RSS was 19,316,736 bytes (18.42 MiB), peak database/WAL storage was 588,856 bytes, and at most one listing-associated verification was pending. Directory file descriptors peaked at 18. Sampled CPU totals were 24.32 seconds for the two directory processes and 70.18 seconds for the workload. These are small-fixture observations, not public-load capacity claims. Final listings, visible entries, pending checks, challenges, rate buckets and retained listing bytes were all zero; the copied SQLite database contained zero listing rows and passed integrity verification. Binary and artifact hashes were checked after completion.

Graphical testing found two existing lifecycle defects that the public join path made reproducible. First, explicit secure-transport disconnect erased connection state before forwarding the backend terminal event, so rejected joins retained player slots. The secure endpoint now queues exactly one local terminal event; reconnecting client consumers ignore events for previous connection IDs. The new transport regression checks local close and duplicate close, and the existing headless reconnect cycles pass. Two actual rejected game joins both emptied slot 0.

Second, browser-driven world entry ran before `Engine::frame()` stopped the previous frame's background Lua garbage collection. A debugger trace showed both the main thread in `LuaManager::clear()` and the worker in `LuaManager::gcStep()` collecting the same state. The client now calls `finishGc()` before both deferred world transitions. The corrected graphical client completed character creation, survived the directory outage, and returned to the browser. This client-only correction does not change the directory or workload binaries used by the two-hour gate. No sanitizer coverage of the complete graphical engine is claimed.

The tested client SHA-256 is `34866b2d8acd9f974f6de496f4a26832ea5d418656f8eee3127bd59ecda5835c`; the server SHA-256 is `3b3b2b8cc8aeb444f0abcd5d716f41894321c408d68d88ce4e50d95ad71a5414`. Matching binaries and redistributable resources are staged separately in `build/discovery-preview/runtime/`, with a file-hash manifest and an explicit uncommitted-development provenance note. This preview requires a compatible Ubuntu 24.04 runtime; shared libraries are not bundled. It does not replace the previous gameplay build or contain game data or private test state.

Raw local evidence is retained under `.legacy-mp-test/discovery/`, including native/sanitizer logs, graphical screenshots, the debugger trace, operator/restore counters, build logs and the bounded workload artifacts. Proprietary game assets and fixture private keys are not included in release evidence. Docker Desktop could not mount this workspace into its VM, so the local proxy configuration was staged with `docker cp`; the resource and network configuration otherwise follows the local Compose fixture. A superseded preflight run was intentionally interrupted after the transport correction and is retained separately; it does not count toward the final gate.

See [the operator and deployment guide](../public-discovery.md) for the API, signing format, explicit opt-in settings, limits, local test commands and production acceptance requirements.
