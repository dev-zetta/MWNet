# Lean overnight component validation

`CI/run_mwnet_overnight.py` supervises one eight-hour native transport/lifecycle workload and one simultaneous local trusted-HTTPS directory churn workload. The native harness supports sixteen concurrent synthetic peers with registration, authentication, reconnect, canonical mechanics and death/respawn. The directory workload restarts its backend during traffic and must recover, withdraw listings and expire temporary state. This tests real encrypted transport and directory code, while sixteen rendered game actors, full CoreScripts load and production sixteen-player capacity remain outside its scope.

The new profile leaves the historical `CI/run_mwnet_soak.sh --release-gates` eight-client/24-hour semantics unchanged. Old failures and their strict percentage measurements remain recorded. The new procedure records those native percentage diagnostics alongside its own predeclared criteria; it does not relabel historical runs.

## Prepared tools and source identity

The tools directory contains `bin/mwnet-headless-integration`, `bin/mwnet-directory`, `bin/mwnet-discovery-tests`, a static `caddy` binary and `evidence/compiled-provenance.json`. The provenance pins all executable hashes, verifies compiled production repository inputs against the selected runtime commit and separately identifies the modified test harness file/hash. The harness changes are test-only: sixteen-peer admission, explicit per-cycle empty-ledger checks and final cleanup counters. These are QA binaries, not relabelled downloadable release binaries. Caddy's actual hash is recorded and checked again after shutdown.

For the initial alpha.3 validation, production source is `d871d54894a0774db7dbcaedc3a0955bacbc08b3`; the isolated prepared tools live under `build/overnight-validation-tools/`. The source archive and clean pinned GNS tree were compiled in a separate cache with no network access. All 110 compiled repository inputs were compared with the runtime Git blobs; only `apps/mwnet_tests/headless_integration.cpp` differs. Caddy was extracted from the existing appliance's verified image layers rather than downloaded or installed on the host. No shared full-game build cache or live NUC service is modified.

## Running and observing

First run a fresh short preflight. It cannot satisfy the overnight gate and explicitly records `overnightAccepted: false`. A normal acceptance run requires a clean committed QA procedure revision, at least eight hours, at least 100 completed cycles, nonzero latency/loss and an unused output directory on persistent disk.

```bash
MWNET_DOCKER_CONTEXT=default bash CI/run_mwnet_overnight_container.sh \
  /path/to/overnight-validation-tools /persistent-disk/new-preflight \
  --runtime-commit d871d54894a0774db7dbcaedc3a0955bacbc08b3 \
  --preflight --seconds 30 --clients 16 --latency-ms 75

MWNET_DOCKER_CONTEXT=default \
MWNET_OVERNIGHT_DETACH=1 \
MWNET_OVERNIGHT_CONTAINER_NAME=mwnet-overnight-candidate \
bash CI/run_mwnet_overnight_container.sh \
  /path/to/overnight-validation-tools /persistent-disk/new-overnight \
  --runtime-commit d871d54894a0774db7dbcaedc3a0955bacbc08b3 \
  --procedure-revision "$(git rev-parse HEAD)" \
  --seconds 28800 --clients 16 --latency-ms 75 --packet-loss-percent 2
```

The detached container does not depend on an interactive shell or tool session. It is retained after exit and has `restart=no`: reboot, interruption or failure cannot silently restart the workload and masquerade as continuous acceptance. GNU `timeout` supervises Python from outside the controller, derives the selected duration plus 900 seconds, sends TERM and escalates to KILL after 45 seconds if needed. State, SQLite, credentials, test TLS files, logs, RSS journals and receipts live under the persistent output directory. The wrapper rejects tmpfs/ramfs artifacts and preserves existing evidence. Container defaults request four CPUs, 4 GiB memory, no additional swap, and 512 PIDs, a read-only root, no capabilities, no network beyond its own loopback namespace and the calling user's UID/GID. Record observed cgroup constraints separately: a requested zero-swap budget does not by itself prove kernel enforcement. Docker logs are capped at two 5 MiB files.

Container `/tmp` is a disk bind mount from a private, mode-0700 sibling named `.mwnet-tmp.<output-name>.XXXXXX`, created beside the selected output directory. The wrapper prints its path to stderr and exports `TMPDIR`, `TMP` and `TEMP` for both host and container subprocesses, overriding inherited RAM-backed paths. It does not mount a temporary filesystem. The controller verifies private ownership and the shared artifact filesystem, records the scratch path/filesystem, and includes sibling scratch in the sampled 512 MiB aggregate artifact bound, individual log limits and disk-headroom checks. This is sampled accounting, not a separate filesystem quota. Direct controller runs default to a private `tmp/` under their output and reject RAM-backed artifact storage.

Scratch is retained alongside the evidence because a detached container can remain active after the wrapper exits; there is no wrapper-exit cleanup that could erase its files mid-run. After the container has exited and the required evidence is preserved, remove only its reported scratch directory when no longer needed. Treat scratch contents as private because temporary files may contain credentials or keys. The shared `CI/mwnet_disk_tmp.sh` helper is included in new procedure hashes; historical receipts retain their original procedure lists and outcomes.

Inspect `result.json` and `resources.jsonl` on disk while the test runs. The result is refreshed at least once a minute; `status: running` is not acceptance. On completion also inspect `docker --context default inspect mwnet-overnight-candidate --format '{{.State.ExitCode}}'` and the terminal receipt. Both the container exit code and the complete receipt must agree on success. A missing/stale running receipt with an exited container is a failed or interrupted run. `docker --context default logs --tail 30 mwnet-overnight-candidate` provides bounded supervisor progress without exposing private test state.

## Fixed acceptance criteria

- Native and directory workloads must complete the requested duration and source/workload checks with exit code zero. The native workload must complete at least 100 cycles and verify empty combat, inventory, justice, lifecycle and connection-metric state after every cycle, then report zero retained entries at the end. Its observed synthetic queue depth must not exceed the configured peer count.
- Directory traffic must observe the deliberate outage, resume successful churn after restart, finish withdrawal and expiry cleanup, retain zero listing/challenge/rate/pending counters and pass SQLite integrity/empty-listing checks. Its backend samples include CPU, file descriptors, RSS and database bytes; native/proxy resource samples and container CPU/memory constraints are also retained.
- Native, directory and Caddy memory are assessed after discarding the first quarter of samples, so directory startup and its early restart are outside the steady-state interval. Eight equal windows must show no unexplained final-minus-first increase above 8 MiB. A trend above 4 MiB with at least five of seven rises over 64 KiB and a fitted slope above 0.5 MiB/hour also fails. Native assessment uses settled per-lifecycle RSS; directory/proxy assessment uses repeated live RSS samples. Short preflights do not accept long-duration memory stability.
- Absolute RSS limits remain 1 GiB for native, 1 GiB for the directory workload process tree and 256 MiB for Caddy; the existing directory harness additionally caps its backend at 256 MiB and pending verification jobs at two. Any container OOM event fails the run. Sampled output plus disk scratch is capped at 512 MiB, individual logs in either location at 32 MiB, and at least 512 MiB of disk headroom must remain after the initial 2 GiB preflight check. Resource rows distinguish output, scratch and combined bytes without following symlinks; final artifact bounds are checked again after process cleanup.
- All executable/procedure hashes must remain unchanged. Every owned process group must stop gracefully with exit code zero, no forced kill and no remaining live members. The supervisor's own monotonic workload wall time must reach the requested duration, independently of component JSON claims. Nonfinite or nonnumeric duration, cycle and queue fields fail validation. Both the controller deadline and the independent GNU timeout enforce requested duration plus 900 seconds; a forced timeout cannot produce a duration pass.

- New runs also sample host-kernel `MemAvailable` and the global `oom_kill` counter, including the initial and final observations. At least 2 GiB of available host memory and no increase in global OOM kills are required. Missing or invalid observations, lost headroom, counter resets or any host OOM invalidate the environment even if the container's own OOM counters remain zero. The failure observation is preserved and a later recovery cannot erase it. These checks do not cap another application's heap or change historical verdicts.

Private run directories, credentials, identity keys, cookies and test TLS private keys must not be published. Reviewed structured receipts can support the lean release checklist, with their synthetic/component scope intact. A successful overnight result does not replace recovery tests, the recorded security/licensing review, real rendered platform acceptance or an actual game-server capacity test.
