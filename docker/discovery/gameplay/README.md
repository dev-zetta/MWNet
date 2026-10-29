# NUC canonical-content gameplay preparation

This private upgrade adds the operator's locally installed Morrowind, Tribunal and Bloodmoon ESM files to the accepted `mwnet-registration` game on `gmax-nuc`. It reuses the accepted alpha.2 image, persistent identity, account/world volume, access password and existing public UDP tunnel. It does not publish game content in an image, repository or directory response. The root helper is specific to this NUC's existing paths and accepted deployment receipt.

Prepare a new private bundle from the repository root, supplying the actual licensed installation's Data Files directory:

```bash
python3 docker/discovery/gameplay/prepare.py \
  --data-dir '/path/to/Morrowind/Data Files' \
  --output build/public-gameplay-nuc-ready
```

The preparer checks TES3 headers, the current allowed content CRC32 values and SHA-256 hashes; copies only the three ordered ESM files; and records a complete bundle checksum manifest. Keep the bundle's parent directory private. Its content subdirectory is readable inside the non-root container, while the enclosing host bundle remains mode `0700`. Copy it over the existing Tailscale SSH connection to `/home/gmax/mwnet-gameplay` and verify `sha256sum --check SHA256SUMS` there.

Start the reviewed helper from the operator's NUC SSH terminal:

```bash
sudo systemd-run --unit=mwnet-gameplay-setup --collect \
  --property=Type=oneshot --property=TimeoutStartSec=30min \
  /bin/bash /home/gmax/mwnet-gameplay/enable-gameplay.sh
journalctl -u mwnet-gameplay-setup -f
```

The helper requires the exact previously accepted image, serializes setup with a lock, installs checksum-verified content below a root-private `/srv/mwnet` parent, stops only the test game, and saves a root-private consistent archive of its complete state under `/var/backups/mwnet-gameplay`. It installs a root-owned Compose overlay under `/etc/mwnet-gameplay`, mounts the three ESM files read-only, sets the native server's canonical content environment, and raises the game cap to two CPUs and 2 GiB. The bridge network, loopback-only UDP host publication, read-only root, UID 10001 and persistent state remain in effect.

Acceptance requires positive canonical spell/actor/recovery counts, CoreScripts startup, the original native fingerprint, a running container without OOM/restarts and the expected limits, then at least 65 seconds of signed discovery observation with fresh heartbeats. A failed transition restores the previous container configuration and keeps the private backup. It does not delete or restore over the current account/world volume automatically.

On success, the unchanged game access password is exported only to `/home/gmax/.mwnet-gameplay-access-password`, owned by the SSH operator with mode `0600`. Transfer it privately to the client machine or view it in the operator's own terminal; never paste it into logs/chat or pass it in command-line arguments. Use the in-game public browser with `Discovery/directoryUrl = https://mwnet.playit.plus`, supply the server password and a separate game account password, and explicitly confirm the advertised fingerprint on first use. Content startup and discovery checks do not certify a successful game-client join, character creation, movement, combat, respawn, graphics/audio or 16-player load.
