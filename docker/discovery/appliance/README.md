# MWNet directory installer appliance

This is a custom Ubuntu Server 26.04 LTS amd64 installer ISO with the MWNet directory containers and host bootstrap included. The builder and bootstrap also support Ubuntu 24.04 LTS. It retains Ubuntu's installer screens for disk selection, network configuration and administrator account creation. It is not an Ubuntu Core image: Core requires a separate model/gadget/snap packaging design. It installs the master directory host only; the multiplayer game server remains a separate deployment.

The ISO includes the directory and Caddy container images, their image IDs, Compose/Caddy settings, the setup scripts and corresponding directory build inputs. Optional playit configuration, a credential template and a guide are included; the agent image is downloaded only when explicitly started. It contains no administrator password, SSH private key, Tailscale enrollment token, playit agent secret, game account or production hostname. Host Docker/Tailscale packages are downloaded from their official signed repositories on first boot, so internet access is required. This is not a fully offline installer. Snap packaging is not needed for the directory.

## Build

Use a Linux amd64 build machine with Docker/Buildx access, Python 3 with PyYAML, xorriso, gpgv and the Ubuntu archive keyring. On Ubuntu, the non-Docker build tools are available in `python3-yaml xorriso gpgv ubuntu-keyring`. Reserve at least 15 GiB of persistent disk space for the base ISO, output, temporary files and container build; avoid a RAM-backed `/tmp`. The builder creates its temporary files alongside the output. Docker also needs sufficient space in its own storage.

Download an official Ubuntu 26.04 live-server-amd64 ISO plus `SHA256SUMS` and `SHA256SUMS.gpg` from [Ubuntu releases](https://releases.ubuntu.com/26.04/). Keep their original names. The builder verifies the signature against the installed Ubuntu keyring and checks the selected ISO before using it. For 24.04, use that release's ISO and signed checksum files instead. From the repository root:

```bash
python3 docker/discovery/appliance/build-iso.py \
  --base-iso build/discovery-appliance-26/cache/ubuntu-26.04.1-live-server-amd64.iso \
  --checksums build/discovery-appliance-26/cache/SHA256SUMS \
  --signature build/discovery-appliance-26/cache/SHA256SUMS.gpg \
  --output build/discovery-appliance-26/mwnet-directory-ubuntu-26.04-amd64.iso
```

Use a new output filename for each build. The build compiles and runs the directory tests in Docker, downloads the pinned Caddy image, exports both images, and remasters the verified ISO while preserving its BIOS/UEFI boot provisions. The directory context contains explicit tracked source paths with their current working-tree bytes; its aggregate hash is recorded separately from the Git revision. Source and appliance changes do not need to be committed first. Arbitrary untracked files, local test state and `.env` files are excluded; suspicious credential filenames in tracked build inputs cause a failure. The source archive contains the directory build inputs, not the entire game checkout. Container dependencies and host apt packages can change between builds; the manifest and checksums identify a particular result, not byte-for-byte reproducibility across future builds.

The output has an adjacent JSON build manifest with input/output hashes, container image IDs and BIOS/UEFI metadata, and an `.iso.sha256` checksum file. Verify that checksum after copying the ISO to another computer. There is no physical-disk writing command in the builder.

The host release and container base are independent. The directory image retains its pinned Ubuntu 24.04 userspace and tested dependencies while Docker runs it on the Ubuntu 26.04 host kernel. Host apt repositories use the validated release's suite (`resolute` for 26.04, `noble` for 24.04); the bootstrap rejects other OS releases and architectures rather than substituting packages from a different suite.

## Install and first login

Boot the ISO in a disposable VM first, then on the intended server using your usual USB installation workflow. Choose the destination disk carefully in Ubuntu's interactive storage screen; installing replaces the selected disk contents. Create your own administrator and password. Keep OpenSSH enabled and import your public key if available. Password login over SSH defaults off; local console login still uses the administrator password. If you skip public-key import, authorize a key from the console or enable policy-permitted Tailscale SSH after enrollment as described below. No shared default login is supplied.

After the first installed-system boot, a systemd service verifies the payload, runs `setup-host.sh`, loads the bundled containers, checks their image IDs and Compose configuration, and writes `/var/lib/mwnet-appliance/ready` only after success. It does not start public services or enroll Tailscale. Preparation runs after cloud-init's final stage and waits for network-online. Failures are visible in the journal, with up to three starts per hour; it does not silently mark incomplete preparation successful.

```bash
systemctl status mwnet-appliance-setup.service
sudo journalctl -u mwnet-appliance-setup.service -b
test -f /var/lib/mwnet-appliance/ready && echo 'Preparation complete'
```

If preparation fails, correct the reported network/package issue and retry:

```bash
sudo systemctl reset-failed mwnet-appliance-setup.service
sudo systemctl restart mwnet-appliance-setup.service
```

From the local console or an existing LAN SSH session, enroll the machine in your own tailnet:

```bash
sudo tailscale up
tailscale ip -4
```

Join the same tailnet from your administration computer, then run `ssh YOUR_LINUX_USER@SERVER_TAILSCALE_IP` with your SSH key. This uses ordinary OpenSSH over Tailscale. Existing SSH listeners are not restricted automatically. Keep internet SSH forwarding disabled and constrain tailnet access to administrators.

Alternatively, enable Tailscale SSH on the server with `sudo tailscale set --ssh`. It authenticates tailnet connections using your Tailscale identity and SSH access policy; it does not require a matching OpenSSH `authorized_keys` entry. Restrict the policy to the intended administrators and local Linux account. It leaves ordinary OpenSSH settings unchanged. See [Tailscale SSH](https://tailscale.com/docs/features/tailscale-ssh).

## Activate the directory after configuring public ingress

The hostname, DNS and public ingress must be decided before activation. The included Caddy configuration expects public ports 80/443; home hardware behind CGNAT needs a separately validated tunnel/certificate arrangement. Tailscale administration alone does not publish the directory to players. Preserve the private backend: do not publish port 8080. See the included `public-discovery.md` for operational controls, backup/restore and public release gates.

For playit Premium, follow [playit.md](../playit.md) in a source checkout or the adjacent `playit.md` on the installed appliance. It supplies a separate agent project and a loopback-only directory overlay. Use that guide's two-file directory commands instead of the base-only activation commands below. Older installed ISOs need the new optional files copied over; rebuilding or reinstalling the OS is unnecessary. An online agent establishes agent authentication, while public DNS, certificate issuance/renewal, discovery requests and source-rate-limit behavior still require acceptance.

On the installed host, create a root-owned environment file and set `DIRECTORY_HOST` to the actual hostname (no scheme or path):

```bash
sudo install -m 0600 /opt/mwnet/discovery-appliance/.env.example /etc/mwnet-directory.env
sudoedit /etc/mwnet-directory.env
sudo docker compose -p mwnet-directory \
  --env-file /etc/mwnet-directory.env \
  -f /opt/mwnet/discovery-appliance/compose.yml config --quiet
sudo docker compose -p mwnet-directory \
  --env-file /etc/mwnet-directory.env \
  -f /opt/mwnet/discovery-appliance/compose.yml up -d --no-build --pull never
```

The bundled runtime Compose file uses the loaded image tags and never pulls replacements implicitly; `SOURCE_REVISION` in the example environment file is unused here. `restart: unless-stopped` restarts activated containers on later boots. Updates require an explicitly verified replacement image/configuration; do not reinstall the OS to update the application. Directory database and Caddy state live in persistent named Docker volumes. Back up and test restoring that state independently of this installer.

## Validation boundaries

Run `python3 CI/test_discovery_appliance.py`, `python3 CI/test_discovery_host_setup.py` and `python3 CI/test_discovery_playit.py` for safeguards; the playit checks use the real Docker Compose parser without contacting its daemon. A successful build, boot metadata inspection and container tests do not establish successful OS installation, SSH access, physical hardware compatibility, Secure Boot acceptance or public reachability. Record those outcomes separately for the exact ISO. This image is a staging installation artifact until installation and deployment acceptance complete; stable 1.0 still needs the independent security, cross-platform, legal and beta gates.

References: [Ubuntu autoinstall on installation media](https://canonical-subiquity.readthedocs-hosted.com/en/latest/tutorial/providing-autoinstall.html), [Ubuntu Core image creation](https://documentation.ubuntu.com/core/how-to-guides/image-creation/use-ubuntu-image/), [Docker on Ubuntu Core](https://documentation.ubuntu.com/core/how-to-guides/container-deployment/build-an-image-for-docker-deployment/).
