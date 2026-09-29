# September 18 paired soak and local Linux candidate preparation

> Historical pre-rebrand evidence: LegacyMP/legacy-mp are display aliases for the former fork name in identifiers, commands, and artifact paths. They are not renamed artifacts or MWNet validation results. Hashes, revisions, measurements, and exit codes are unchanged; consult Git history for the original labels.

## Completed soak at c978f30275

The paired retry at `c978f30275afb3aacc6c0be24331dcbf64cbc24c` completed
successfully from 2026-09-17 17:44:33 UTC to 2026-09-18 17:44:51 UTC.
Both exit-code files contain zero, both metrics report completed scenarios,
and the metrics and log hashes match the runner manifest.

| Measurement | Native | ASan/LSan/UBSan |
| --- | ---: | ---: |
| Elapsed seconds (runner) | 86406.16 | 86417.65 |
| Completed cycles | 5641 | 5640 |
| Clients | 8 | 8 |
| Simulated latency | 75 ms | 75 ms |
| Simulated unreliable loss | 2% | 2% |
| Dropped snapshots | 897 | 897 |
| Post-warm-up RSS change | +0.164026% | +0.531497% |
| Runner peak RSS | 282394624 bytes | 756850688 bytes |
| Exit code | 0 | 0 |

The native RSS measurement passes the unchanged 1% gate. Sanitizer RSS is
diagnostic; leak and undefined-behavior detection remained enabled and there
were no reported sanitizer defects. All final cgroup memory events were zero.
The container memory limit remained 2 GiB. **Swap-free execution is not
certified:** `memory.swap.max` changed from `0` at startup to `max` at finish.
The cause is unknown. This does not turn the completed workload into an OOM
failure, but the stronger no-swap claim is unsupported.

The [retained manifest and scalar metrics](2026-09-18-paired-soak.json) identify
the executables and full raw artifact hashes. Local raw logs, metrics and sample
journals remain in `build/release-soak-c978f30275-retry1-results/`.
The separate verification helper which requires swap disabled throughout does
not pass; it must not be represented as successful.

A diagnostic performance comparison against the earlier GCC 15 baseline
passed the numeric thresholds: tick p99 7302 -> 7257 microseconds,
serialization p99 16 -> 15 microseconds, final RSS 14561280 -> 13930496 bytes,
and effectively unchanged traffic per client per cycle. The candidate used
Clang 18, so this is not the required like-for-like compiler comparison.

## Linux build preparation

Clean tests-only Debug builds of the original `c978f30275` snapshot passed
2/2 CTests each under GCC, Clang, Clang ASan/UBSan and Clang TSan. The first
ASan/LSan invocation failed because the sandbox prevented LeakSanitizer's
process inspection. The same binaries passed outside that restriction with
`detect_leaks=1:halt_on_error=1`; leak detection was not disabled.
These are protocol/mechanics/persistence checks, not full engine builds.

Building the actual dedicated server exposed two additional blockers:

- Its Lua binding table tried to erase function pointer types in a constant
  expression. Compile-time signature metadata and runtime addresses now use
  separate tables generated from the same ordered binding list.
- Server-only builds excluded the ESM loader and its VFS/BSA dependencies,
  leaving authoritative game-setting and magic-content symbols unresolved.

The fixes are pinned in local commit
`56fcea73f4b4ec0d23a8ebc93ea40da55e206d40`. A Clang 18 RelWithDebInfo dedicated
server build succeeded on Ubuntu 24.04. All three full native CTests passed,
including encrypted headless integration. GCC also passed syntax checks of
`ScriptFunctions.cpp` and `LangLua.cpp`; this is not a full GCC server link.
The Lua API inventory retains all
833 baseline names (893 names total). The older soak is evidence for its exact
commit and is not silently reassigned to this changed runtime candidate.
After server-only validation, the native headless executable was byte-for-byte
identical to the passing soak executable (SHA-256
`2979e734b61e615e1d0c8b6e8896130da005ecfcd55332a3fdae5e9a44c80913`).
That comparison applies to the server-only build profile, before enabling the
client and rebuilding the combined profile. It does not prove coverage for
the newly built server's Lua paths. No replacement day-long soak was launched.

## Server preview artifact

The locally retained archive is:

`build/release-candidate-c978f30275/packages/LegacyMP-1.0.0-alpha.1-Linux-x86_64-server-56fcea73f4.tar.gz`

SHA-256: `4a86ad333108cdb154c7cdd8a6114881d315d56e01d9a6cf180db1e40d58ce43`.

It includes the dedicated server, default configuration, launcher, CoreScripts,
project license, source SPDX inventory, runtime library listing and per-file
checksums. It excludes generated server identities/accounts and game content.
It requires compatible system libraries; it is not a self-contained universal
Linux binary. The SPDX document is a source inventory, not a complete inventory
of the host's runtime libraries.

A fresh isolated container with the original source/build tree hidden verified
all archive/file hashes, initialized CoreScripts, and exited with code zero
after SIGINT with `Quitting peacefully.`. This proves package startup, not
playable multiplayer acceptance. Startup still reports optional Lua CJSON
fallback, unset game content, and three `log formatting failed` warnings.
Those warnings are retained in the startup log and have not been represented
as a warning-free test. The binary's startup banner reports commit unavailable
because its source snapshot has no Git metadata; the manifest records the
externally verified runtime commit and binary hash. All seven source changes
in the build container were checked against the pinned commit.

`CI/build_legacy-mp_linux.sh`, `CI/package_legacy-mp_linux.py` and
`CI/smoke_legacy-mp_linux.py` provide build, package and extracted-startup
checks. The manual GitHub workflow `legacy-mp-linux-candidate.yml` uploads local
build artifacts; it does not publish a release. Its constituent packaging and
smoke steps were tested locally; the GitHub workflow itself has not run.
Negative checks also confirmed that a corrupt archive is rejected before
startup and a missing server executable is rejected before packaging.

## Combined Linux client/server preview

The Clang 18 RelWithDebInfo game client built successfully at the same runtime
commit. Enabling the combined build profile rebuilt the affected targets;
all three full native CTests then passed again (12.94 seconds total).
The initial four-job client compilation was deliberately interrupted and
resumed with eight jobs; the resumed build completed successfully.

The combined archive is retained at:

`build/release-candidate-c978f30275/combined-packages/LegacyMP-1.0.0-alpha.1-Linux-x86_64-client-server-56fcea73f4.tar.gz`

SHA-256: `b39c100c179fe1082ba92618ea37dd2b135f1c4335f8912228ec382e703c1603`.
It includes both executables, CoreScripts, generated client resources and
configuration, local-session launchers, source SBOM, licenses and checksums.
It is approximately 181 MiB compressed and requires compatible Ubuntu 24.04
system libraries, including the graphics/OSG runtime. No game data is included.
A fresh container with `/legacy-mp` hidden and networking disabled verified all
checksums, ran the included client version command successfully, initialized
CoreScripts and shut the included server down with exit zero and a false
script-error state. Logs are retained in
`build/release-candidate-c978f30275/combined-package-smoke/`.

The client version command exits zero but still reports the underlying
`OpenMW version 0.52.0`. Client branding cleanup and graphical gameplay
acceptance remain distinct from a successful compile/version check.

## Subsequent gameplay result

The [graphical gameplay test](2026-09-18-gameplay.md) exposed launcher setup,
secure-message sequence, disconnect/crash and saved-player loading problems.
The preview is blocked for gameplay acceptance; the successful package startup
checks above do not close those issues.

## Open release work

The private origin remains a backup and has not been pushed to or reconfigured.
GitHub publication is deferred as requested. The Linux game client also built successfully and its version command exited
zero. Content-backed graphical multiplayer acceptance remains pending.
Windows/MSVC and macOS builds remain pending. Exact updated-candidate sanitizer,
fuzz, soak and comparable performance evidence still require release review.
Independent security review, specialist legal review and the opt-in public beta
remain open. Neither this preview nor the completed earlier soak authorizes a
stable 1.0.0 release.

To repeat client/server preparation in an existing Ubuntu 24.04 build environment:

```sh
bash CI/build_legacy-mp_linux.sh "$PWD" "$PWD/build/linux-candidate"
```

Use a clean committed checkout. The build script runs the full native CTests,
generates a source SBOM, packages both executables and resources, and performs fresh-extraction
client-version and server-startup checks. It does not run a new 24-hour soak or independent reviews.
