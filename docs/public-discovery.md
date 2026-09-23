# Public discovery API v1

TES3MP gameplay protocol 12 is incompatible with previous gameplay protocols. Discovery API v1 is independently versioned and introduces no gameplay packets. The old master service, RakNet transport and standalone browser remain removed. The current endpoint class is `GameEndpoint`; historical cryptographic handshake domain labels remain unchanged.

The new service provides opt-in public discovery, signed announcements, encrypted endpoint verification and an in-game browser. It does not relay game traffic, open NAT mappings, hold game accounts or certify advertised compatibility. Direct connections remain available during directory outages. Public deployment and independent security review remain release gates; no public directory hostname is configured by default.

## Game configuration

In `tes3mp-server.cfg`, explicitly opt into public listening and announcements:

```ini
[General]
localAddress = 0.0.0.0
publicListen = true
port = 25565
hostname = My TES3MP server

[Discovery]
announce = true
directoryUrl = https://directory.your-domain.example
publicAddress = YOUR_PUBLIC_NUMERIC_IP
publicPort = 25565
```

The public address must be a globally routable numeric IPv4 or IPv6 address; configure any required UDP forwarding separately. Announcement hostnames are intentionally unsupported in v1, which removes DNS rebinding and resolver stalls from endpoint verification. Direct-connect hostnames remain supported. Invalid announcement configuration disables discovery with a log message while the game server continues running. Private/local game servers do not announce automatically. For an isolated graphical test only, `Discovery/localTest = true` permits a loopback game address and a loopback HTTPS directory origin; the directory must also explicitly use `--local-test`. It does not permit announcing private endpoints to a remote public directory.

Set the same `Discovery/directoryUrl` in `tes3mp-client.cfg`. An optional `Discovery/caFile` supplies a trusted CA file for a local HTTPS test deployment; certificate and hostname verification are always enabled. Use **Public servers**, search the loaded list, **More** for further pages, then double-click an entry and supply the usual game account credentials. An incompatible advertised gameplay protocol cannot be selected. Content requirements remain advisory until the existing join check succeeds. New identities still require explicit confirmation; listings never replace saved fingerprints. Refresh and encrypted probes run asynchronously, and closing the browser cancels outstanding requests.

The game server signs metadata with its existing `server-identity.key` through a domain-limited signing method; private key material never crosses HTTPS. A single background worker publishes a bounded snapshot, including player counts and current password/content requirements, every 60 seconds. Failed requests back off between 5 and 60 seconds. Changed announcement status appears in the periodic server log. Shutdown cancels the worker; the last lease expires within 180 seconds rather than delaying shutdown for an HTTP withdrawal.

## Build and local validation

The native directory build requires C++20, Boost.System/Beast 1.70+, SQLite3, libcurl 7.85+, libsodium and the existing pinned GameNetworkingSockets dependencies. It does not require rendering libraries or Morrowind data:

```bash
cmake -S . -B build-directory -G Ninja -DTES3MP_DIRECTORY_ONLY=ON -DTES3MP_FETCH_DEPS=ON
cmake --build build-directory --parallel 2
ctest --test-dir build-directory --output-on-failure
```

The Docker build pins Ubuntu and Caddy image digests and the existing GNS source revision. Apt dependencies are resolved at build time; `/package-versions.txt` records the runtime package versions. Preserve the resulting image digest and source revision for releases. This is a reproducible build procedure, not a claim that mutable apt repositories yield byte-identical images indefinitely.

The local Compose fixture uses its own certificate authority, private endpoint verification and loopback-only HTTPS publication. Run from the repository root:

```bash
docker compose -p discovery-local -f docker/discovery/compose.local.yml up --build -d
# Wait for Caddy to create its local CA, then copy its public certificate.
docker compose -p discovery-local -f docker/discovery/compose.local.yml cp https:/data/caddy/pki/authorities/local/root.crt /tmp/discovery-local-ca.crt
chmod 644 /tmp/discovery-local-ca.crt
docker compose -p discovery-local -f docker/discovery/compose.local.yml cp /tmp/discovery-local-ca.crt test:/data/local-ca.crt
docker compose -p discovery-local -f docker/discovery/compose.local.yml exec test python3 /usr/local/bin/run_tes3mp_discovery_soak.py --directory /usr/local/bin/tes3mp-directory --tests /usr/local/bin/tes3mp-discovery-tests --ca /data/local-ca.crt --artifacts /data/soak-2h --seconds 7200 --revision YOUR_CANDIDATE_REVISION
docker compose -p discovery-local -f docker/discovery/compose.local.yml cp test:/data/soak-2h ./discovery-evidence
docker compose -p discovery-local -f docker/discovery/compose.local.yml down
```

The artifact directory must be new and on disk. Named volumes retain evidence after `down`; do not remove them until evidence is copied. Only the small public CA certificate above goes through `/tmp`. The workload owns and restarts its local directory, exercises reachable/unreachable endpoints, periodically registers and withdraws servers, injects a directory outage, and allows 65 seconds for final cleanup. `result.json` records requested/elapsed duration, binary SHA-256 hashes, revision, workload and directory exit codes, peak RSS and final counters. `samples.jsonl` records CPU time, RSS/high-water RSS, file descriptors, database bytes and queue/state counts. The gate requires directory RSS below 256 MiB, no more than two pending checks and no retained leases/challenges/rate buckets after cleanup. A short run with `--seconds 30` validates the harness but does not satisfy the two-hour gate.

## API and signed format

All public requests use HTTPS at the configured origin, without redirects. API JSON scalars are strings. Public keys, nonces, signatures and payloads use lowercase hex. A server ID is the URL-safe base64 public-key portion of the existing `ed25519/...` fingerprint, without its prefix. JSON objects reject duplicate/unknown envelope fields and excessive nesting; text fields must be valid UTF-8 without control characters.

| Request | Body/result |
| --- | --- |
| `POST /v1/challenges` | `publicKey`, `operation` (`announce` or `withdraw`); returns `nonce`, valid for 30 seconds and one mutation from its issuing source |
| `PUT /v1/servers/{id}` | Signed announcement envelope; `202` means verification pending, `200` means already verified |
| `DELETE /v1/servers/{id}` | Signed withdrawal envelope with a fresh `withdraw` challenge; returns `200` |
| `GET /v1/servers?after={cursor}` | Up to 100 signed envelopes in `servers`, plus `next` (empty at the end); pages are also bounded by response bytes |

The envelope fields are exactly `origin`, `operation`, `nonce`, `publicKey`, `payload`, and `signature`. The signature covers raw ASCII `TES3MP discovery v1\n`, followed by four length-prefixed UTF-8 strings (`origin`, `operation`, nonce hex, public-key hex), then the length-prefixed decoded payload. Lengths and integers are unsigned 32-bit little-endian. Ed25519 signatures are 64 bytes. The shared native codec is the authoritative implementation; no JSON canonicalization is involved.

The binary payload contains, in order: schema integer `1`; host string; port integer; display-name string; software-version string; gameplay-protocol, player-count, capacity and password-flag integers; content-file count; then each ordered filename string, hash count and ordered 32-bit hashes. Empty allowed-hash lists preserve the existing wildcard rule. Empty content requirements advertise no enforced manifest. Content order and alternative hashes are preserved exactly. The client verifies the envelope, then the game connection must present its identity; gameplay and content checks still run normally.

Challenges are bounded to two per key/source and 2,048 globally. The service permits at most 1,000 listings, 16 MiB of retained encoded listing data, two concurrent verification jobs, 32 HTTP connections, 4,096 source-rate buckets and 600 requests/source/minute. Requests/responses are capped at 2 MiB, signed payloads at 256 KiB, headers at 8 KiB, HTTP operations at five seconds and encrypted verification at four seconds. SQLite has a 64 MiB page budget with bounded WAL checkpointing. These are initial limits for a small directory, not a public-load capacity claim.

Fresh/changed endpoints remain hidden until the encrypted identity handshake succeeds. Heartbeats refresh a 180-second lease. Verified endpoints are rechecked after five minutes; failures hide the entry and retry after 30 seconds. Verification schedules the least recently checked eligible entries first. Restarts retain unexpired signed records but discard all verification trust and challenges. First-use game trust remains separate. The production address policy rejects private, loopback, link-local, multicast, reserved, documentation and transition address ranges. `--local-test` is only for isolated fixtures.

## VPS deployment and operations

Choose a new hostname and Linux VPS, point DNS at it, and copy `docker/discovery/.env.example` to `docker/discovery/.env` with the actual hostname and source revision. Then run `docker compose --env-file docker/discovery/.env -f docker/discovery/compose.yml up --build -d`. Caddy publishes ports 80/443, manages certificates, overwrites `X-Real-IP`, and forwards only `/v1/*`. The proxy applies bounded header/body/write/idle timeouts and an 8 KiB header limit, using [Caddy server options](https://caddyserver.com/docs/caddyfile/options#timeouts). The directory backend has no host port publication. Do not expose it directly while `--trust-proxy` is enabled. Its `/healthz` and `/metrics` endpoints remain private. Health fails if periodic storage maintenance fails.

Named volumes preserve SQLite and Caddy state. The directory runs as UID 10001 with a read-only root filesystem, a 512 MiB memory limit and one CPU; Caddy has 128 MiB and one CPU. Logs rotate at three 10 MiB files per service. Allow additional VPS RAM for the OS and Docker; determine production sizing from representative load, not just the small local fixture. Keep database and certificate volumes on persistent disk.

Run operator commands inside the private service container, supplying the same origin/database options as the running service. `--block-identity SERVER_ID` and `--block-endpoint IP:PORT` add durable blocks; matching `--unblock-*` options remove them. Bracket IPv6 endpoints; equivalent numeric spellings are normalized for endpoint blocks. The service reloads blocks each second. There is no public administration API.

Use `tes3mp-directory --origin https://YOUR_HOST --database /data/directory.sqlite3 --backup /data/backup.sqlite3` for an online SQLite backup, then copy the backup off the VPS using your normal backup process. Copying only the live `.sqlite3` file can miss WAL data. For restore, stop the directory, preserve the existing database and its WAL/SHM files, replace them with the verified backup under UID 10001, then restart and check private health/metrics. Restored entries must pass verification again. Database schemas newer than v1 are rejected; a new directory origin requires a fresh database and fresh announcements. Preserve Caddy volumes separately.

Before public launch, validate DNS/TLS, public endpoint reachability, first-use and changed-key behavior, platform packaging, abuse controls, outage recovery and backup restoration on staging. The remaining independent security, Windows/macOS, legal and beta gates still block stable 1.0.
