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

## Account password recovery

`reset_account.py` is an offline administrator tool for the native credential store. It uses the existing dedicated server's Argon2id implementation, verifies the new hash, backs up an existing credential and atomically replaces only the hashed native account record. Character/world data and the separate server access password remain intact. Its account-name canonicalization matches the native byte-wise ASCII rules, including UTF-8 names. Passwords enter through stdin and never appear in command arguments, output or public checksum manifests.

The NUC wrapper `run-account-reset.sh` requires a reviewed private bundle containing that script, `reset_account.py`, `verify_image.py`, a `deployment.json` copied from the accepted content manifest, and checksums for those public inputs. The operator's desired password belongs only in `.new-account-password`, mode `0600`, under the mode-0700 bundle directory. The wrapper pins the existing image, stops only the game, saves a root-private state backup, runs the reset as UID 10001 in a network-isolated container capped at one CPU and 512 MiB, and restarts the game. Restarting clears temporary in-memory authentication lockouts; rate-limit policy is unchanged. Failure restarts the previous game and keeps the private input for diagnosis/retry. Success removes the private input. If the target has no native credential, the wrapper explicitly creates one; normal login initializes or resumes character creation while retaining any existing profile.

After preparing `/home/gmax/mwnet-account-reset`, run from the NUC SSH terminal:

```bash
sudo systemd-run --unit=mwnet-account-reset --collect \
  --property=Type=oneshot --property=TimeoutStartSec=30min \
  /bin/bash /home/gmax/mwnet-account-reset/run-account-reset.sh PublicGameplayTest
```

After a successful reset, choose **Log In** for that account and enter both the new account password and unchanged server access password. The current client clears both password fields after each attempt. `python3 CI/test_gameplay_account_reset.py` checks record/profile preservation, private backups, atomic-write failure, explicit creation, name/path validation, actual libsodium verification and native hash invocation through stdin. The disposable deployment suite also covers reset-wrapper success and failure/restart; native Argon2id execution is validated separately in the isolated runtime.
