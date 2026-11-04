# NUC host crash diagnostics

This host-specific installer targets the Intel NUC8i5BEH operator `gmax` (UID/GID 1000), with the existing armed Ubuntu crash kernel and a stopped acceptance workload. It does not restart acceptance, flash firmware, force a panic or reboot the host. Preserve all interrupted runs and their frozen inputs.

The profile enables panic on detected hard/soft kernel lockups and retains panic-on-oops, with a 30-second panic fallback reboot timeout. These are kernel watchdog settings, separate from a hardware watchdog; no hardware watchdog is armed. Kdump must already be loaded before the settings are applied. Dumps and extracted kernel messages go to root-only `/var/crash/mwnet-host`. The existing dump kernel/initrd and reserved crash memory are preserved. Actual dump capture remains unqualified until an intentional panic or a real captured failure occurs. See the [kernel lockup detector documentation](https://www.kernel.org/doc/html/latest/admin-guide/lockup-watchdogs.html) and [Ubuntu kdump instructions](https://ubuntu.com/server/docs/how-to/software/kernel-crash-dump/).

Journald uses persistent disk storage, a five-second sync interval, a 512 MiB retention limit, 4 GiB free-space reserve and 32 MiB runtime limit. This does not erase the original acceptance artifacts or explicitly vacuum old journals. Critical journal messages are already synchronized immediately by systemd; the shorter interval improves the surviving ordinary event trail. See [journald.conf](https://www.freedesktop.org/software/systemd/man/latest/journald.conf.html).

`mwnet-host-telemetry.service` writes/fsyncs private JSON observations every five seconds, bounded to eight 16 MiB files under `/var/log/mwnet-host`. Each record contains UTC time, boot ID, uptime, host memory/CPU/pressure, temperature validity, NVMe I/O counters, free disk space and resource counters for at most 32 Docker cgroups. It never reads process arguments, environment variables, game accounts, configs, keys or dump contents. The root logger has no capabilities or network access and is limited to 64 MiB RAM, 5% of one CPU and sixteen tasks. Its latest numeric observation is atomically published to `/var/lib/mwnet-host-status/latest.json`, root-owned and readable by group `gmax`. Scratch is explicitly on disk under `/var/lib/mwnet-host-diagnostics/tmp`.

The installer simulates installation of `nvme-cli` and, when the Wi-Fi mitigation is selected, `rfkill`. It refuses removals, upgrades or additional packages. It reads SMART health from the existing `/dev/nvme0`; health warnings are reported, not automatically repaired or erased. No self-test, firmware download/activation or destructive storage operation runs. Diagnostic package installation is retained by rollback.

`--block-unused-wifi` is an optional, reversible mitigation for this NUC. It requires one default route through the live `eno1` Ethernet interface, no addresses on `wlp0s20f3`, and the expected single Intel Wi-Fi radio. After crash logging is active, it soft-blocks only that radio and verifies the runtime and saved systemd-rfkill state. It preserves the original state for rollback. This removes the repeated failed association activity observed immediately before both recorded host freezes; their causal relationship is unproven. Bluetooth, Ethernet, Tailscale, firewall, routing and saved Wi-Fi credentials are not changed. Across-reboot restoration still needs a separate boot check.

Stage a fresh private disk bundle containing the five payload files and generated `manifest.json`, with SHA256 values and the actual source commit. Run the reviewed launcher from the operator's existing SSH/console session with interactive sudo. For example, after verifying the staged manifest and launcher hashes:

```bash
sudo python3 -B /home/gmax/mwnet-host-diagnostics-20261104/bundle/configure.py apply \
  --block-unused-wifi \
  --report /home/gmax/mwnet-host-diagnostics-20261104/install-status.json
```

The apply report must not already exist: failures and earlier reports are retained. Verification requires an armed crash kernel, the private dump destination, merged journal settings, a durable marker retrieved from `/var/log/journal`, an enabled/active restricted telemetry service, and two consecutive fresh observations from the current boot. The report includes the protected backup path and selected storage health. Agent SSH can subsequently read the private operator report, telemetry status and normal systemd state without gaining Docker/root access.

Configuration files, modes, runtime sysctls, prior logger activation and optional Wi-Fi state are backed up under `/var/lib/mwnet-host-diagnostics/backups`. An apply verification failure restores changed files and runtime settings. Explicit rollback refuses altered backups or independent edits before restoring files:

```bash
sudo python3 -B /usr/local/libexec/mwnet-host-diagnostics/configure.py rollback \
  --backup /var/lib/mwnet-host-diagnostics/backups/EXACT_BACKUP_FROM_REPORT
```

Logs, crash dumps, backups and interrupted acceptance evidence are retained. Do not publish raw vmcores or unrestricted crash logs: kernel memory can contain credentials even when user pages were filtered. These controls improve evidence and recovery from detectable lockups; a firmware/power failure or total machine lockup can still prevent any dump. Disk telemetry and an armed kdump configuration alone do not establish that the host freeze is fixed. Assess the collected storage/error evidence and subsequent stability before starting a new full-duration acceptance run.
