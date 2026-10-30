# Autonomous graphical gameplay validation

`CI/run_mwnet_gameplay.py` drives the real rendered MWNet client against a real encrypted dedicated server. It requires no operator gameplay actions. It uses an explicit, default-off `--gameplay-test-file` control mode in the same client binary that is packaged for release, with normal character-generation, physics, activation, combat and networking paths. The control mode accepts only literal loopback destinations and an explicit Ed25519 server fingerprint. It exposes no control socket or remote server administration endpoint.

The operator supplies an installed, legally obtained Morrowind Data Files directory containing the three canonical ESMs and their graphics/sound archives. The procedure reads these files through a read-only mount; assets are never copied into the repository, receipt or release package. All accounts, passwords, Xauthority cookies, server identity, game saves and user settings belong to a newly created private run directory. Normal player profiles and the public NUC service are not used.

## Running the procedure

Build the Ubuntu 24.04 environment from `Dockerfile.mwnet` and build the client/server and native test targets with the repository's candidate build procedure. The environment includes Xvfb, xauth and FFmpeg, so no host desktop packages or physical display are needed.

```bash
docker build -f Dockerfile.mwnet -t mwnet-gameplay-builder:automated .
MWNET_DOCKER_CONTEXT=default bash CI/run_mwnet_gameplay_container.sh \
  /path/to/build "/path/to/Morrowind/Data Files" \
  /persistent-disk/new-gameplay-run \
  --record-video --expected-commit "$(git rev-parse HEAD)"
```

Set `MWNET_DOCKER_CONTEXT` only when choosing a Docker context explicitly. The wrapper does not change the user's saved Docker context. Its defaults cap the container at six CPUs, 12 GiB memory and 512 PIDs, with a 32 MiB temporary filesystem and 256 MiB shared-memory allocation. Test state, logs and videos are on persistent disk. `MWNET_GAMEPLAY_MEMORY`, `MWNET_GAMEPLAY_CPUS` and `MWNET_GAMEPLAY_IMAGE` can select another explicit budget or already-built environment.

The Python runner can also run directly in a prepared Linux environment using `--client`, `--server`, `--data` and `--output`. Its default whole-run deadline is 900 seconds, each native scenario is bounded, and external process-group cleanup remains effective even if a window stops processing native frames. Existing evidence directories are refused rather than overwritten. Native receipts and logs have explicit size limits; video files are checked with FFprobe and limited to 256 MiB each. `--scenario dialogue-respawn` runs the focused conversation/death regression. The default `all` runs the full suite.

For release acceptance, extract the verified Linux archive, pass its `build` directory as the first wrapper argument, and set `MWNET_GAMEPLAY_PACKAGE_ROOT` to the extracted package root. The runner verifies the complete package file inventory and hashes, source/version/protocol metadata and the actual client/server binary hashes before testing the packaged launcher and CoreScripts. Supply the full source revision using `--expected-commit`; a development build with a dirty source tree is diagnostic evidence only.

## Scenarios and verdicts

- Register an account through the normal encrypted authentication path, complete character creation, compare the native creation result with server-received and saved attributes, skills and resource capacities, move using normal Lua actor controls, open inventory and activate a real canonical teleport door.
- Seed an existing canonical container through the isolated server fixture, assert its actual received inventory, then reconnect with a fresh client process while retaining the same server process and actor state.
- Exercise repeated normal fist attacks after selecting the normal arrest response, observe canonical hit confirmations, knockout and health damage, arrange a timed replicated weakness effect plus a queued effect at resurrection, trigger the standard death/respawn lifecycle while talking to an NPC, and assert native active/queued cleanup, preserved permanent abilities and statistics, closed dialogue and post-respawn movement.
- Connect two rendered clients, require both to observe the initial inventory before a server barrier releases the update, and verify that both receive the new authoritative contents.
- Reject a wrong server fingerprint, an existing saved-key conflict and incompatible content. A saved-key conflict must not overwrite the existing trusted fingerprint.
- Reject unsafe automation inputs, including public destinations, unbounded deadlines and FIFO credentials. The separate `CI/test_mwnet_gameplay_controls.py` binary-boundary suite exercises further file permission, symlink and malformed-control cases without a display or game assets.

Fixture chat handlers exist only under `CI/gameplay_fixture.lua` and are copied into the disposable server profile. They require an explicit fixture environment flag, a loopback client and the generated test account prefix. Fixture setup arranges positions, canonical inventories and combat statistics; it does not provide the gameplay assertions. Client receipts read actual world state. Character creation uses the normal forms and completion callbacks; visible creation panels and blocking menus prevent gameplay controls. Packet errors, falling-into-world-void recovery, server gameplay rejection counters, missing assertions, native failures, source-identity mismatches and unexpected disconnects fail validation. A process exiting zero does not override a failed native receipt. Every planned assertion must appear exactly once in order, followed by one terminal PASS.

## Artifacts and scope

`gameplay-receipt.json` records the binary and procedure hashes, source/version/protocol identity, canonical data hash, scenario results and captured video hashes. The isolated container also records cgroup memory peak/limit and CPU usage when available; these describe this short workload, rather than a capacity or soak claim. Failed cases retain their terminal failure and video hash. Per-client JSONL receipts, server/client logs, private fixture state and optional MP4s remain in the run directory. Only reviewed receipts, logs without credentials and gameplay-only media may be published; control documents, credential files, private identities, cookies, saves and copyrighted game content are excluded.

Video is captured from the isolated virtual display and proves that native rendering occurred. The fixture uses a null audio output; canonical hit-sound identifiers and normal sound calls can be checked in the game log, but this does not certify a physical audio device. Controlled server death injection tests the real death/respawn path and is identified separately from natural NPC killing blows. Two-player tests do not establish sixteen-player capacity. This procedure strengthens development release validation; it does not close independent security, legal, long soak or other stable-release gates.
