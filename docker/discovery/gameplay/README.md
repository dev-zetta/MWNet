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

Image pinning uses the accepted build's configuration, platform-manifest and top-level index digests. Docker's [classic API type](https://github.com/moby/moby/blob/master/api/types/image/image_inspect.go) describes a configuration ID, while its [containerd implementation](https://github.com/moby/moby/blob/master/daemon/containerd/image_inspect.go) returns the target descriptor digest. The verifier accepts only those recorded hashes and additionally requires Linux/amd64 and the accepted runtime source revision. It prints the matched digest type and rejects unrelated images before stopping the game.

Acceptance requires positive canonical spell/actor/recovery counts, CoreScripts startup, the original native fingerprint, a running container without OOM/restarts and the expected limits, then at least 65 seconds of signed discovery observation with fresh heartbeats. A failed transition restores the previous container configuration and keeps the private backup. It does not delete or restore over the current account/world volume automatically.

On success, the unchanged game access password is exported only to `/home/gmax/.mwnet-gameplay-access-password`, owned by the SSH operator with mode `0600`. Transfer it privately to the client machine or view it in the operator's own terminal; never paste it into logs/chat or pass it in command-line arguments. Use the in-game public browser with `Discovery/directoryUrl = https://mwnet.playit.plus`, supply the server password and a separate game account password, and explicitly confirm the advertised fingerprint on first use. Content startup and discovery checks do not certify a successful game-client join, character creation, movement, combat, respawn, graphics/audio or 16-player load.

## Regression checks

Run `python3 CI/test_gameplay_image.py` and `python3 CI/test_gameplay_content_bundle.py` for digest/platform/revision pinning and content-integrity/exclusion checks. `CI/test_gameplay_deployment.py` exercises the real helper with a controlled Docker CLI in a disposable Ubuntu rootfs copy below `build/`, including a containerd index ID, consistent private backup before recreation, credential modes and failure before mutation, and rollback after startup/discovery failure or when previous overlay settings exist. It uses fixture data only and does not certify actual Docker execution or game connectivity.

The disposable rootfs needs Python 3, Bash, coreutils, grep, tar, gzip and util-linux with their dependencies. With subordinate UID/GID mappings configured, run it in a user namespace:

```bash
MWNET_GAMEPLAY_TEST_ROOTFS="$PWD/build/gameplay-helper-check/rootfs" \
  unshare --user --map-root-user --map-auto python3 CI/test_gameplay_deployment.py -v
```
