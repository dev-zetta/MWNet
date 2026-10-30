# Public discovery API v1

MWNet gameplay protocol 13 is incompatible with previous gameplay protocols. Discovery API v1 is independently versioned and introduces no gameplay packets. The old master service, RakNet transport and standalone browser remain removed. The current endpoint class is `GameEndpoint`; authentication and discovery signatures use MWNet domain labels, so directory services and gameplay peers must be rebuilt together.

The new service provides opt-in public discovery, signed announcements, encrypted endpoint verification and an in-game browser. It does not relay game traffic, open NAT mappings, hold game accounts or certify advertised compatibility. Direct connections remain available during directory outages. Public deployment and the recorded security assessment remain release gates under the [current policy](../RELEASE_GATES.md); no public directory hostname is configured by default.

## Game configuration

In `mwnet-server.cfg`, explicitly opt into public listening and announcements:

```ini
[General]
localAddress = 0.0.0.0
publicListen = true
port = 25565
hostname = My MWNet server

[Discovery]
announce = true
directoryUrl = https://directory.your-domain.example
publicAddress = YOUR_PUBLIC_NUMERIC_IP
publicPort = 25565
```

The public address must be a globally routable numeric IPv4 or IPv6 address; configure any required UDP forwarding separately. Announcement hostnames are intentionally unsupported in v1, which removes DNS rebinding and resolver stalls from endpoint verification. Direct-connect hostnames remain supported. Invalid announcement configuration disables discovery with a log message while the game server continues running. Private/local game servers do not announce automatically. For an isolated graphical test only, `Discovery/localTest = true` permits a loopback game address and a loopback HTTPS directory origin; the directory must also explicitly use `--local-test`. It does not permit announcing private endpoints to a remote public directory.

Set the same `Discovery/directoryUrl` in `mwnet-client.cfg`. An optional `Discovery/caFile` supplies a trusted CA file for a local HTTPS test deployment; certificate and hostname verification are always enabled. Use **Public servers**, search the loaded list, **More** for further pages, then double-click an entry and supply the usual game account credentials. An incompatible advertised gameplay protocol cannot be selected. Content requirements remain advisory until the existing join check succeeds. New identities still require explicit confirmation; listings never replace saved fingerprints. Refresh and encrypted probes run asynchronously, and closing the browser cancels outstanding requests.

The game server signs metadata with its existing `server-identity.key` through a domain-limited signing method; private key material never crosses HTTPS. A single background worker publishes a bounded snapshot, including player counts and current password/content requirements, every 60 seconds. Failed requests back off between 5 and 60 seconds. Changed announcement status appears in the periodic server log. Shutdown cancels the worker; the last lease expires within 180 seconds rather than delaying shutdown for an HTTP withdrawal.

## Build and local validation

The native directory build requires C++20, Boost.System/Beast 1.70+, SQLite3, libcurl 7.85+, libsodium and the existing pinned GameNetworkingSockets dependencies. It does not require rendering libraries or Morrowind data:

```bash
cmake -S . -B build-directory -G Ninja -DMWNET_DIRECTORY_ONLY=ON -DMWNET_FETCH_DEPS=ON
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
docker compose -p discovery-local -f docker/discovery/compose.local.yml exec test python3 /usr/local/bin/run_mwnet_discovery_soak.py --directory /usr/local/bin/mwnet-directory --tests /usr/local/bin/mwnet-discovery-tests --ca /data/local-ca.crt --artifacts /data/soak-2h --seconds 7200 --revision YOUR_CANDIDATE_REVISION
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

The envelope fields are exactly `origin`, `operation`, `nonce`, `publicKey`, `payload`, and `signature`. The signature covers raw ASCII `MWNet discovery v1\n`, followed by four length-prefixed UTF-8 strings (`origin`, `operation`, nonce hex, public-key hex), then the length-prefixed decoded payload. Lengths and integers are unsigned 32-bit little-endian. Ed25519 signatures are 64 bytes. The shared native codec is the authoritative implementation; no JSON canonicalization is involved.

The binary payload contains, in order: schema integer `1`; host string; port integer; display-name string; software-version string; gameplay-protocol, player-count, capacity and password-flag integers; content-file count; then each ordered filename string, hash count and ordered 32-bit hashes. Empty allowed-hash lists preserve the existing wildcard rule. Empty content requirements advertise no enforced manifest. Content order and alternative hashes are preserved exactly. The client verifies the envelope, then the game connection must present its identity; gameplay and content checks still run normally.

Challenges are bounded to two per key/source and 2,048 globally. The service permits at most 1,000 listings, 16 MiB of retained encoded listing data, two concurrent verification jobs, 32 HTTP connections, 4,096 source-rate buckets and 600 requests/source/minute. Requests/responses are capped at 2 MiB, signed payloads at 256 KiB, headers at 8 KiB, HTTP operations at five seconds and encrypted verification at four seconds. JSON is checked before recursive parsing for a depth of at most eight and a structural allocation budget of 4,096 nodes; quoted punctuation does not consume that budget. SQLite has a 64 MiB page budget with bounded WAL checkpointing. These are initial limits for a small directory, not a public-load capacity claim.

Fresh/changed endpoints remain hidden until the encrypted identity handshake succeeds. Heartbeats refresh a 180-second lease. Verified endpoints are rechecked after five minutes; failures hide the entry and retry after 30 seconds. Verification schedules the least recently checked eligible entries first. Restarts retain unexpired signed records but discard all verification trust and challenges. First-use game trust remains separate. The production address policy rejects private, loopback, link-local, multicast, reserved, documentation and transition address ranges. `--local-test` is only for isolated fixtures.

## Host installation and private administration

For a bootable installation medium with the containers and bootstrap included, use the [directory appliance ISO builder](../docker/discovery/appliance/README.md). It retains interactive disk/account selection, downloads host packages on first boot and leaves public deployment disabled until the hostname and ingress are configured.

Use Ubuntu Server 26.04 LTS on an amd64 machine with persistent SSD storage; Ubuntu Server 24.04 LTS is also supported by the bootstrap. The host release is independent of the directory image's pinned Ubuntu 24.04 userspace. The host does not need C++ build dependencies because Docker builds them inside the image. The bootstrap selects the matching `resolute` or `noble` package repositories and rejects unsupported OS/architecture combinations. Docker and Tailscale both provide official packages for these releases: [Docker Ubuntu installation](https://docs.docker.com/engine/install/ubuntu/) and [Tailscale stable packages](https://pkgs.tailscale.com/stable/).

During OS installation, create a normal administrator account with sudo access and install OpenSSH with your SSH public key. First access can use the machine's console or SSH over your home LAN. Copy the checkout or just `docker/discovery/setup-host.sh` onto the server, review the script, and run these commands there (from the repository root when using a checkout):

```bash
bash docker/discovery/setup-host.sh --dry-run
sudo bash docker/discovery/setup-host.sh
sudo tailscale up
tailscale ip -4
```

The script installs Docker Engine, Buildx, Compose, Tailscale, OpenSSH, Git, curl and CA certificates, enables their services, and checks Docker/Compose and SSH configuration. Downloads use HTTPS and apt repositories have scoped signing keys. It stops on conflicting container packages or customized repository definitions instead of removing workloads or overwriting those definitions. Rerunning it accepts its own repository files and can upgrade installed packages. It preserves existing SSH, firewall and Docker daemon settings, adds no docker-group members, and does not deploy a directory, enroll Tailscale, configure tunnels or reboot. A dry run only prints the plan; it does not validate the host. Install OS security updates separately and reboot when required. Bootstrap checks are syntax and offline safeguard tests; installation on the intended server still needs acceptance.

Follow the authentication URL from `tailscale up`. Install Tailscale on your administration computer, join the same tailnet, then use ordinary OpenSSH through the encrypted Tailscale network:

```bash
ssh YOUR_LINUX_USER@SERVER_TAILSCALE_IP
# Or use the server's MagicDNS name when enabled in your tailnet.
```

Tailscale supplies connectivity through NAT; OpenSSH supplies the Linux login and key authentication. Alternatively, run `sudo tailscale set --ssh` on the server to use Tailscale identity and SSH policy for tailnet logins without adding an OpenSSH public key. The bootstrap does not enable that feature automatically. Limit tailnet and SSH-policy access to your administrators and intended local account, and verify a second session before tightening authentication or firewall rules. Keep any ordinary SSH private key on your administration computer. Existing OpenSSH listeners remain as configured (normally all interfaces); installing Tailscale does not restrict SSH to the tailnet. Avoid forwarding port 22 from the internet; on a VPS, restrict public SSH with the provider firewall after validating tailnet access. Use sudo for Docker commands: membership in the docker group grants root-equivalent access. See [Tailscale quickstart](https://tailscale.com/docs/how-to/quickstart) and [Tailscale SSH](https://tailscale.com/docs/features/tailscale-ssh).

Private administration and public hosting are separate. At home behind CGNAT, Tailscale lets administrators connect, but public players and directory clients still need a public ingress service. A game tunnel must carry UDP; discovery needs HTTPS for your chosen hostname. The base Compose deployment below expects externally reachable ports 80/443 for Caddy's certificate challenges and HTTPS. Choose and validate a tunnel and certificate arrangement before using it at home. Do not expose the directory backend on port 8080. Docker publishes the proxy ports on all interfaces by default and published ports can bypass UFW rules; use deliberate port bindings and upstream firewall controls. The host bootstrap leaves tunnel-provider installation and enrollment to the operator; the optional playit files below supply that deployment path.

## Home hosting with playit Premium

Use the optional [playit integration guide](../docker/discovery/playit.md) for the pinned agent Compose file, root-owned `/etc/playit/agent.env`, existing-agent adoption, HTTPS gateway/DNS configuration and separate directory activation. The `compose.playit.yml` overlay replaces Caddy's public host bindings with `127.0.0.1:80` and `127.0.0.1:443`. The agent uses host networking and a separate Compose project. Nothing publishes the directory backend or operational routes. The appliance builder includes the optional templates and guide, but no secret or playit image; existing installed machines can copy the files without reinstalling.

Playit forwards TLS to Caddy. Accept certificate issuance and renewal through the selected gateway before public launch. Leave PROXY protocol disabled until a restricted receiving listener is implemented and tested. The current proxy overwrites client-supplied `X-Real-IP` with its TCP peer, which means tunneled clients share one observed-source rate bucket; do not claim per-client abuse isolation through the tunnel yet. The agent's host-network access and its credential are additional trust boundaries: keep the credential outside Git, do not mount the Docker socket, retain bounded container resources/logs, and protect the playit account. Agent/account compromise can redirect tunnels to other host services, so only configure the intended origins and do not treat host networking as container network isolation. Global directory bounds, signatures, endpoint verification and first-use game identity confirmation remain enforced.

## VPS deployment and operations

Choose a new hostname and Linux VPS, point DNS at it, and copy `docker/discovery/.env.example` to `docker/discovery/.env` with the actual hostname and source revision. Then run `docker compose --env-file docker/discovery/.env -f docker/discovery/compose.yml up --build -d`. Caddy publishes ports 80/443, manages certificates, overwrites `X-Real-IP`, and forwards only `/v1/*`. The proxy applies bounded header/body/write/idle timeouts and an 8 KiB header limit, using [Caddy server options](https://caddyserver.com/docs/caddyfile/options#timeouts). The directory backend has no host port publication. Do not expose it directly while `--trust-proxy` is enabled. Its `/healthz` and `/metrics` endpoints remain private. Health fails if periodic storage maintenance fails.

Named volumes preserve SQLite and Caddy state. The directory runs as UID 10001 with a read-only root filesystem, a 512 MiB memory limit and one CPU; Caddy has 128 MiB and one CPU. Logs rotate at three 10 MiB files per service. Allow additional VPS RAM for the OS and Docker; determine production sizing from representative load, not just the small local fixture. Keep database and certificate volumes on persistent disk.

Run operator commands inside the private service container, supplying the same origin/database options as the running service. `--block-identity SERVER_ID` and `--block-endpoint IP:PORT` add durable blocks; matching `--unblock-*` options remove them. Bracket IPv6 endpoints; equivalent numeric spellings are normalized for endpoint blocks. The service reloads blocks each second. There is no public administration API.

Use `mwnet-directory --origin https://YOUR_HOST --database /data/directory.sqlite3 --backup /data/backup.sqlite3` for an online SQLite backup, then copy the backup off the VPS using your normal backup process. Copying only the live `.sqlite3` file can miss WAL data. For restore, stop the directory, preserve the existing database and its WAL/SHM files, replace them with the verified backup under UID 10001, then restart and check private health/metrics. Restored entries must pass verification again. Database schemas newer than v1 are rejected; a new directory origin requires a fresh database and fresh announcements. Preserve Caddy volumes separately.

Before public launch, validate DNS/TLS, public endpoint reachability, first-use and changed-key behavior, platform packaging, abuse controls, outage recovery and backup restoration on staging. The remaining independent security, Windows/macOS, legal and beta gates still block stable 1.0.
