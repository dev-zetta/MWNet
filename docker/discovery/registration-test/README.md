# Real game-server registration acceptance

This optional test deploys the validated MWNet 1.0.0-alpha.2 Linux dedicated server (gameplay protocol 13) against `https://mwnet.playit.plus`. It starts the actual server and CoreScripts, signs announcements with its persistent server identity, and relies on the production directory's encrypted, identity-pinned endpoint probe before a listing becomes visible. An independent Python verifier checks the exact signed envelope bytes with libsodium and compares the listed fingerprint to the server's public startup fingerprint. No game account or server private key is sent to the directory or printed by the test harness.

The game uses a separate `mwnet-registration` Compose project and persistent `game-state` volume. It binds UDP 25565 inside its bridge network, publishes only `127.0.0.1:25565/udp` on the NUC, and is reached by the existing host-network playit agent. It runs as UID 10001 with a read-only root filesystem, dropped capabilities, no-new-privileges, one CPU, 512 MiB RAM, 128 PIDs and rotating logs. A random access password and Argon2id hash are generated privately inside `/state`; they and the server identity survive recreation. The image records installed package versions and resolved runtime libraries. No Docker socket is mounted into it.

This is a registration test, not gameplay acceptance. Capacity is advertised as 16, but no populated-server load test runs. It preserves the ordered Morrowind/Tribunal/Bloodmoon requirements from the release defaults and does not include or load canonical game content. Spell mechanics fail closed until that content is supplied for a playable server. The test name explicitly identifies the server as temporary.

## Prepare

Create a separate playit UDP tunnel on agent `mwnet-master` with origin `127.0.0.1:25565`. Use its assigned numeric public address and UDP port, not the HTTPS directory address or the tunnel DNS name. The current operator-assigned endpoint is `209.25.141.17:43133` (`malls-magnum.tun.ply.gg:43133`). Keep PROXY protocol off for this endpoint.

Obtain the already validated release archive and run from the repository root:

```bash
python3 docker/discovery/registration-test/prepare.py \
  --archive build-mwnet-release/assets/linux/MWNet-1.0.0-alpha.2-Linux-x86_64-client-server-4a92d877b4.tar.gz \
  --output build/public-registration-nuc-ready \
  --public-address 209.25.141.17 --public-port 43133
```

The preparer requires the exact known release archive hash and per-file manifest hashes, selects only server files/code/initial moderation defaults, rejects unsafe members and account/world data, and emits a SHA-256 manifest. It does not copy a working server profile, identity or password. Copy the resulting folder to `/home/gmax/mwnet-registration` on the NUC. Verify `sha256sum --check SHA256SUMS` and review `.env`, Compose and the test script there before starting. Existing directory, Caddy and playit projects stay separate.

## Run

The NUC requires interactive sudo for Docker. Start a detached job from your own SSH terminal:

```bash
sudo systemd-run --unit=mwnet-registration-test --collect \
  --property=Type=oneshot --property=TimeoutStartSec=30min \
  /bin/bash /home/gmax/mwnet-registration/run-test.sh
journalctl -u mwnet-registration-test -f
```

The job builds a compatible Ubuntu 24.04 runtime around the verified release binary, checks dynamic libraries, and first exercises CoreScripts startup and clean shutdown with announcements disabled. It then observes a visible, signature-verified listing for at least 190 seconds and three distinct accepted heartbeat nonces. Next it deliberately stops the test game for 195 seconds, checks that its listing disappears and records private directory cleanup metrics. It finally restarts the game and requires the same identity and metadata to become visible again. Visibility of a new identity in the production directory establishes that its encrypted reachability check passed; it does not replace an independent gameplay-client join or first-use trust confirmation test. The directory may hide a stopped endpoint during periodic probing before lease expiry, so the metrics/expiry observation is also recorded.

On success the test game remains running and password-protected. A failed test stops only this game, retaining its state and logs for diagnosis. The original directory deployment and agent are not restarted. Account/world state and credentials remain in the private named volume. To stop it later, use the same project and `.env` file with `docker compose ... stop game`; do not delete its volume to restart it. Operators can inspect the generated access password privately on the NUC when needed; do not paste it into chat or commit it.

The local verifier can also check the public URL from a different machine using the printed fingerprint, omitting `--connect-ip`. The root harness uses `--connect-ip 127.0.0.1` to reach local Caddy while retaining its public hostname and normal CA verification. Public API and public UDP behavior must be recorded separately from local routing. `python3 CI/test_registration_listing.py` tests valid signatures and rejection of tampered bytes, origins and operations. A prepared bundle or successful build is not a registration result; save the job's terminal status, public listing receipts and cleanup outcome.
