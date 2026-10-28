# Home hosting through playit.gg

The directory can use a playit Premium HTTPS tunnel behind CGNAT. Public HTTPS reaches the playit agent, then Caddy on the NUC, then the private directory container. Tailscale SSH remains the private administration path. A multiplayer game server needs its own UDP tunnel; installing this agent does not deploy a game server.

The optional files work with Docker Compose 2.30 or newer. `compose.playit-agent.yml` pins the official stable 1.1.0 image by digest, uses host networking, restarts after reboot, and bounds memory, CPU, processes and logs. Image updates are explicit. The directory and agent use separate Compose projects, so directory restarts do not stop the tunnel. Host networking lets an agent origin of `127.0.0.1:443` reach Caddy's host loopback listener; the agent needs no published inbound ports or Docker socket mount.

## Install or adopt the agent

For a repository checkout, run the commands below from its root and set `DISCOVERY_DIR` to its discovery directory. On an appliance containing these new files, use the second assignment instead:

```bash
DISCOVERY_DIR="$PWD/docker/discovery"
# Appliance installation:
# DISCOVERY_DIR=/opt/mwnet/discovery-appliance
```

An older installed ISO does not gain files when the checkout changes. Copy `compose.playit-agent.yml`, `compose.playit.yml` and this guide from the updated checkout to the server before using them, or use the checked-out directory there. Preserve the appliance's generated `compose.yml`, which names its loaded images. Do not replace it with the source checkout's build-based Compose file, and do not rerun first-boot provisioning just to add a tunnel.

Create the credential file only if it does not exist, then edit it on the server. The file must contain one `SECRET_KEY=...` line, without quotes. Keep the actual key out of chat, Git and shell command arguments:

```bash
sudo install -d -m 0700 /etc/playit
sudo touch /etc/playit/agent.env
sudo chown root:root /etc/playit/agent.env
sudo chmod 0600 /etc/playit/agent.env
sudoedit /etc/playit/agent.env
```

`playit.env.example` documents the format; it contains no usable credential. `env_file` passes the value directly to the container, including under sudo, and `format: raw` preserves literal dollar signs. The key remains visible to Docker administrators. Do not publish unredacted `docker inspect` or `docker compose config` output; use `config --quiet` when checking a real deployment. Playit 1.1 also supports a raw-key file through `SECRET_KEY_FILE`; this deployment keeps the existing `SECRET_KEY` environment-file format so upgrading preserves the current credential.

If you already followed the `/etc/playit/compose.yml` instructions and the agent is online, it can stay there. To adopt the repository configuration later, use the same project name `mwnet-playit`, service name `playit` and credential path. The `up` command may recreate that one container, briefly disconnecting tunnels; do not start a second copy of the same agent under a different project. First compare `sudo docker compose ls` with the intended project, then:

```bash
sudo docker compose -p mwnet-playit \
  -f "$DISCOVERY_DIR/compose.playit-agent.yml" config --quiet
sudo docker compose -p mwnet-playit \
  -f "$DISCOVERY_DIR/compose.playit-agent.yml" up -d
sudo docker compose -p mwnet-playit \
  -f "$DISCOVERY_DIR/compose.playit-agent.yml" ps
```

The initial start downloads the pinned agent image. The ISO packages the optional configuration and guide but does not package this image, copy a host secret, create an agent, enroll an account or start the agent automatically. Check that the chosen agent appears online in the playit dashboard. Local container status alone does not prove account authentication or public tunnel operation. If you review logs with `logs --tail 50 playit`, keep any claim URLs or credentials private.

## Update an existing NUC agent

For the existing `/etc/playit/compose.yml` deployment under project `mwnet-playit`, copy `update-playit.sh`, `compose.playit-agent.yml`, `compose.playit.yml` and this guide into one folder on the server. Run the reviewed helper there with your local sudo password:

```bash
sudo bash "$DISCOVERY_DIR/update-playit.sh"
```

The helper requires exactly one existing project/service, validates Compose without printing credentials, downloads the pinned image before changing configuration, backs up the old Compose file, preserves the secret's contents, enforces root ownership/mode 0600, and recreates only the agent. It checks the running image ID, restart count and OOM state after five seconds. A failed start restores the old Compose configuration and uses the retained local image. It also installs the optional loopback overlay and guide under `/etc/playit`; when activating the directory from the appliance, use `-f /opt/mwnet/discovery-appliance/compose.yml -f /etc/playit/compose.playit.yml`. It does not activate the directory or change the original ISO payload. Check the dashboard for authenticated/online state afterward; process survival is not authentication acceptance.

To update Docker and Tailscale host packages as well, include `setup-host.sh` and `update-host-and-playit.sh` in that folder. The wrapper runs the existing, supported host bootstrap followed by the agent updater; it can upgrade bootstrap-managed packages, including OpenSSH, but preserves existing configuration and Tailscale enrollment. Package changes are not rolled back if a later agent update fails. Run this maintenance through systemd so it survives the SSH interruption caused by a Tailscale or SSH service restart:

```bash
sudo systemd-run --unit=mwnet-maintenance --collect \
  --property=Type=oneshot --property=TimeoutStartSec=30min \
  /bin/bash "$DISCOVERY_DIR/update-host-and-playit.sh"
journalctl -u mwnet-maintenance -f
```

Reconnect over Tailscale if needed and read `journalctl -u mwnet-maintenance --no-pager` for the final result. Inspect `systemctl show mwnet-maintenance -p Result -p ExecMainStatus` while the unit exists; `--collect` can unload it after completion, so use its journal to confirm success. This updates the bootstrap's package set, not every OS package, and does not reboot. Schedule broader OS maintenance and any required reboot separately.

## Configure the directory tunnel

In playit, create an **HTTPS** tunnel using the online agent. Add the public hostname to that tunnel's gateway and set the DNS records shown by playit. Use that exact hostname in `DIRECTORY_HOST` in `/etc/mwnet-directory.env`, without a scheme or path. Set the HTTPS origin to `127.0.0.1:443`. Keep PROXY protocol **disabled** with the current Caddy configuration. Forwarding to port 8080 would bypass Caddy's route and header restrictions and expose operational endpoints.

Playit forwards TLS; Caddy terminates it and owns the certificate. Caddy's TLS-ALPN-01 challenge requires public port 443 to pass the ACME handshake intact, while HTTP-01 requires public port 80 to reach local port 80. Confirm the selected gateway's forwarding before relying on automatic certificate issuance and renewal; use a separately configured DNS challenge if the gateway cannot carry either challenge. No DNS-provider token is included in these files. A tunnel showing online is not certificate acceptance.

Once the hostname, DNS and gateway are configured, create or edit the root-owned `/etc/mwnet-directory.env` as described in the appliance guide. Apply the loopback overlay after the base configuration. It replaces the original 80/443 bindings rather than appending extra listeners, and leaves the backend unpublished:

```bash
sudo docker compose -p mwnet-directory \
  --env-file /etc/mwnet-directory.env \
  -f "$DISCOVERY_DIR/compose.yml" \
  -f "$DISCOVERY_DIR/compose.playit.yml" config --quiet

# Appliance: use only its bundled images.
sudo docker compose -p mwnet-directory \
  --env-file /etc/mwnet-directory.env \
  -f "$DISCOVERY_DIR/compose.yml" \
  -f "$DISCOVERY_DIR/compose.playit.yml" up -d --no-build --pull never
```

For a source checkout, replace the last command's `--no-build --pull never` with `--build` to build the directory and fetch Caddy. Always include the overlay on later directory `up` commands; using only the base file would restore its public host bindings. The named data/certificate volumes and project name are unchanged, so existing directory state survives recreation.

## Acceptance and current limits

Check `sudo docker compose ... ps`, the private directory health check, and the published listeners with `ss -lnt`. Public discovery must have only the intended loopback host bindings; port 8080 and operational endpoints stay private. From a device outside the home LAN, test HTTPS with normal certificate verification, `/v1/servers`, a complete signed challenge/update, expiry, and a pinned game-server reachability probe. `/healthz` and `/metrics` must return 404 through the public hostname. Test a restart and certificate renewal before public acceptance; never use `curl -k` to claim HTTPS success.

With PROXY protocol off, Caddy sees the agent or Docker gateway as the TCP peer. It overwrites incoming `X-Real-IP`, so arbitrary request headers cannot invent a client identity, but tunneled clients share the directory's 600 requests/minute source bucket. The challenge/update source binding uses that same observed peer. Preserve the existing global bounds and do not enable arbitrary trusted HTTP headers to work around this. Restoring individual source limits requires a dedicated, restricted PROXY-protocol listener plus forwarding/spoofing tests. This limitation and public certificate/reachability acceptance remain open; an online agent alone does not satisfy the public discovery release gate.

For a future game tunnel, select UDP, forward to the game server's configured UDP port, and announce the assigned relay's numeric public IP and UDP port. A playit DNS name is not accepted as `Discovery/publicAddress` in API v1. Complete the directory's encrypted, identity-pinned probe and a real remote join through that tunnel before advertising it as validated.

References: [playit Docker usage](https://github.com/playit-cloud/playit-agent#docker), [playit HTTPS tunnel guide](https://playit.gg/support/https-tunnel/), [playit PROXY protocol](https://playit.gg/support/what-is-proxy-protocol/), [Caddy automatic HTTPS](https://caddyserver.com/docs/automatic-https), [Docker env_file](https://docs.docker.com/reference/compose-file/services/#env_file), [Compose override merging](https://docs.docker.com/reference/compose-file/merge/).
