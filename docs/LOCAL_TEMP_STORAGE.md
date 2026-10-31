# Disk-backed local temporary storage

This development machine mounts system `/tmp` as tmpfs. Use the checkout's ignored `tmp/` directory for builds, tests, extraction and other scratch work. The shared helper creates it with mode 0700 and verifies its visible filesystem is not RAM-backed on Linux. It rejects a shared or foreign-owned scratch directory instead of changing its permissions. `TMPDIR`, `TMP` and `TEMP` are exported to subprocesses, and Python's cached temporary-directory setting is reset in the entrypoints that configure it directly.

Run an arbitrary command through the launcher, including native CTest runs and Python tests:

```bash
bash CI/with_disk_tmp.sh ctest --test-dir build --output-on-failure
bash CI/with_disk_tmp.sh python3 CI/test_mwnet_package_notices.py
```

To keep the variables in an interactive shell, run this from the checkout:

```bash
export TMPDIR="$(python3 CI/mwnet_temp.py --print-root)"
export TMP="$TMPDIR" TEMP="$TMPDIR" MWNET_TMPDIR="$TMPDIR"
```

`MWNET_TMPDIR` selects a different explicitly chosen private disk directory. The Linux candidate builder defaults to `OUTPUT/tmp`, so its source tree can remain read-only; the Docker build entrypoint defaults to `/mwnet/build/tmp`. Boost SDK extraction and cross-platform package smoke checks configure disk scratch directly. Sanitized gameplay control environments use each private profile's `tmp/` directory. Other ad hoc commands need the launcher or exported variables; this does not change another already-running application's environment or remount the operating system's `/tmp`.

The rendered-gameplay, recovery and overnight container wrappers replace their RAM-backed `/tmp` mount with a private disk bind. Each allocates `.mwnet-tmp.<output-name>.<suffix>` alongside the output and prints its path to stderr. This also covers programs that use a literal container `/tmp`, such as X11 socket/lock creation. The visible `/tmp` path inside those containers is on the artifact disk. The overnight supervisor includes sibling scratch in its aggregate 512 MiB artifact bound, 32 MiB individual-log check and disk-headroom checks, and records output, temporary and combined sizes. This is a sampled size limit, not a filesystem quota. Memory/CPU/PID limits remain separate; rendered tests still have their bounded 256 MiB shared-memory allocation.

Retain per-run scratch while any corresponding container is running, including detached runs. After the container exits, preserve needed diagnostics and remove only that run's named scratch directory when it is no longer needed. The shared local `tmp/` directory is also reusable; remove only known inactive entries. Do not delete unrelated system `/tmp` data, active socket/lock directories, saved accounts or original acceptance artifacts. Inspect both disk bytes and inodes when diagnosing pressure.

Disk-backed scratch prevents temporary files from consuming tmpfs capacity. It does not impose a process-heap limit, establish the cause of a host OOM, or make an interrupted acceptance run pass. The [October 31 terminal record](release-evidence/2026-10-31-post-security-overnight.md) and [storage-change evidence](release-evidence/2026-10-31-disk-temp.md) distinguish the reported host memory incident from the test container's resource observations.
