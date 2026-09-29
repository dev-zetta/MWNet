# Sanitizer soak memory investigation, 2026-09-10

> Historical pre-rebrand evidence: LegacyMP/legacy-mp are display aliases for the former fork name in identifiers, commands, and artifact paths. They are not renamed artifacts or MWNet validation results. Hashes, revisions, measurements, and exit codes are unchanged; consult Git history for the original labels.

## Remaining failure after connection-metrics cleanup

The eight-client sanitizer soak for
`6ff16fc87317913bdcd6bd23729f14be9d5f1bb0` completed 5,661 cycles between
2026-09-09 14:42:43 UTC and 2026-09-10 14:42:55 UTC. It exited 1 because
post-warm-up RSS grew 1.80159%, from 402.30 MiB to 409.55 MiB, over the 1%
limit. Peak sampled RSS was 438,722,560 bytes (418.40 MiB). No connection
metrics assertion, sanitizer diagnostic or OOM kill was reported. The
previous cleanup fixed one retained-state defect but did not close the
long-running memory gate.

Artifacts remain in the stopped container and locally under
`build/release-soak-6ff16fc873-results/`:

| Artifact | SHA-256 |
| --- | --- |
| `metrics.json` | `a9246160237fd62e2276240f849111f8296a3dc9f5aceecbbef507db8d4b6b37` |
| `soak.log` | `976c2451e215bbef33d0d4880a0c8cd621d9a5404e47ba91255c7c701c8c4545` |

## Thread-history retention

The harness recreated all eight client endpoints on every cycle. Each
endpoint starts a transport worker, so this generated more than 45,000
threads over the failed run, even though each worker was joined correctly.

The matching LLVM 18
[ASan implementation](https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/compiler-rt/lib/asan/asan_thread.cpp)
explicitly avoids reusing thread contexts. Its
[thread registry](https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/compiler-rt/lib/sanitizer_common/sanitizer_thread_registry.cpp)
retains finished contexts with a `UINT32_MAX` quarantine bound. This runtime
history is separate from the application's live heap and survives `join()`.

A standalone 50,000-task experiment with the same sanitizer runtime produced:

| Workload | Final RSS | Final live allocated bytes |
| --- | ---: | ---: |
| Create and join a worker per task, default sanitizer settings | 153,784,320 | 78,219 |
| Same churn, diagnostic-only 1 MiB allocation quarantine | 29,638,656 | 78,219 |
| Reuse one worker, default sanitizer settings | 20,230,144 | 78,243 |

The small-quarantine control still accumulated RSS with flat live
allocations. It is a diagnostic only; release quarantine settings remain
unchanged. Sources, raw logs, matching LLVM sources and runtime settings are
retained under `build/soak-memory-thread-investigation/`.

## Harness correction

The reconnect loop now retains eight client endpoints and their workers.
Every cycle still establishes fresh encrypted connections, authenticates,
exercises gameplay/death/respawn and disconnects. Client shutdown and
destruction still run after the loop. The final authentication-lockout and
fingerprint-mismatch scenarios create fresh clients and reload persisted
trust data. A per-cycle assertion rejects accidental client recreation.

Progress logs include resident bytes and, in ASan builds, live allocated
bytes and allocator-reserved heap bytes. Allocator statistics are optional
when the compiler does not ship the allocator-interface header; GCC ASan
syntax validation passes. The paired gate below retains the 1% RSS limit,
full sanitizer settings and duration, and measures application RSS in the
native build.

## Local transport closure

The persistent-client diagnostic also exposed a production transport defect:
`GameNetworkingSocketsTransport::closeConnection()` closed the SDK handle
without removing the local `connections` and `sConnectionOwners` entries or
emitting a terminal event. Remote-close handling did remove these entries.
Destroying each client had hidden this difference by clearing all transport
state on shutdown. Reusing clients exposed read-deadline failures during
new handshakes; one traced failure arrived only 26 milliseconds after the
new peer began connecting, rather than after its 30-second read deadline.

Local close now uses the same state-removal path as remote close, preserves
the requested SDK linger behavior, and emits one local disconnect event.
Repeated disconnects do not emit duplicate events and sends on a retired
connection are rejected immediately. Regression tests exercise both client-
and server-initiated closure, followed by reconnect on the same transports.
The tests failed four assertions against the old implementation and pass
after the fix. The complete three-target sanitizer CTest suite also passes.

The normal `legacy-mp` and `legacy-mp-server` targets also rebuild successfully in
the matching dependency image. Their copied workspace binaries match the
container SHA-256 values recorded in `normal-build-result.json`. The client
version check exits successfully inside that image. A server startup check
cannot complete because this build tree has no `server/scripts/serverCore.lua`;
host execution additionally lacks the matching FFmpeg/LuaJIT libraries. These
are build-environment limits, not successful server runtime validation.

The initial persistent-client runs that exposed the timeout remain failed
diagnostics. They recorded no cgroup OOM or memory-limit events and are not
counted as memory-gate passes.

## Extended memory validation

With both fixes, 500 eight-client cycles completed without a functional or
sanitizer error, retaining eight client instances and eleven live threads.
The kernel process RSS high-water mark was 764,239,872 bytes (728.84 MiB).
The run stayed inside a 1 GiB cgroup limit with no swap allowance, OOM event
or memory-limit event. The old 418.40 MiB figure sampled RSS after teardown
and did not capture the process's high-water mark during active scenarios.

This 500-cycle run nevertheless **failed** the unchanged relative RSS gate:
its early/late window averages were 455.72/463.33 MiB (+1.67038%). Live
allocations at cycles 300, 400 and 500 were identical at 846,641 bytes.
Successive 50-cycle RSS averages rose substantially less after cycle 200,
consistent with extended allocator warm-up, but this is not a passed gate.
The raw `fixed.json`, `fixed.log`, `process-memory.json` and cgroup counters
are retained in the investigation directory.

The independent 1,000-cycle run also **failed** the raw sanitizer RSS rule:
458.94/468.03 MiB early/late averages (+1.97981%). It completed all scenarios
with 160 dropped snapshots, no sanitizer report, eleven live threads and
no memory-limit/OOM events under the 1 GiB no-swap limit. Kernel peak RSS
was 769,236,992 bytes (733.60 MiB). Live allocations at cycles 600 through
1,000 were identical at 850,705 bytes; the earlier 4 KiB increase came from
expanding the test's RSS sample vector. Raw artifacts are `long.json`,
`long.log`, `long.exit`, `long-process-memory.json` and `long-memory.events`.

## Separate application RSS from sanitizer retention

A further 300-cycle allocator/mapping diagnostic locates substantial memory
in ASan-owned state. At cycle 300 its default allocation quarantine retained
242,527,948 user bytes, while live application allocations remained below
1 MiB. Large allocations alone retained 135,200 KiB across 260 quarantined
chunks. Stack-depot IDs were identical at cycles 100 and 200 (5,865), and
mapped sanitizer heap/shadow pages continued to change. The thread fake-stack
mappings grew during initial warm-up and were stable between these snapshots.
LLVM's [fake-stack implementation](https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/compiler-rt/lib/asan/asan_fake_stack.cpp)
explains these separately mapped use-after-return detection buffers. The
mapping diagnostic did not request an RSS-gate verdict; its exit 0 means
only that its scenarios and sanitizer checks completed.

The native control **passed 300 cycles** with the unchanged RSS failure flag:
12.600/12.665 MiB early/late averages (+0.514633%). It completed eight-client
scenarios with 48 dropped snapshots and eleven live threads. Kernel peak RSS
was 281,788,416 bytes (268.73 MiB), and the cumulative OOM counters were zero.
The container's `memory.events:max` counter was already cumulative across
build/control work and has no per-run starting baseline, so this control alone
does not establish zero memory-limit events. The paired preflight records
fresh start/end counters. Native artifacts are `native.json`, `native.log`,
`native.exit`, `native-process-memory.json` and `native-memory.events`.

The gate now runs two builds of the same candidate concurrently:

- The native build must pass the existing 1% early/late RSS comparison.
- The ASan/LeakSanitizer/UBSan build must finish the same complete workload
  with default quarantine and use-after-return detection. Its raw RSS trend
  remains recorded as diagnostic data.

Both must complete the requested duration/cycles with matching commit and
workload metadata. Either process failing stops its sibling and fails the
combined gate. Build instrumentation is checked before starting. The runner
preserves individual logs, metrics and exit codes, binary/artifact hashes,
kernel process peaks and cgroup memory-limit/OOM counters in a manifest.
Nine runner tests cover success, native growth, sanitizer failure, cancellation,
wrong metadata/build type, missing metrics, short release budgets and artifact
preservation. All pass.

This corrects the measurement method: sanitizer-owned retained allocations
are not a reliable measurement of the native application's RSS trend. The
previous failed results remain failed; a fresh paired exact-candidate run
must complete before the gate can close.

## Paired preflight result

The final native/sanitizer binaries both **passed 100 paired cycles** with
eight clients, 1 ms simulated latency and 2% snapshot loss. Each ran for
about 372 seconds, exiting 0. This is a developer preflight with
`releaseGates: false`, not the 24-hour release verdict.

| Profile | RSS growth after warm-up | Kernel process peak | Verdict |
| --- | ---: | ---: | --- |
| Native | +0.369154% | 281,665,536 bytes (268.62 MiB) | RSS gate passed |
| ASan/LSan/UBSan | +14.3499% (diagnostic only) | 747,593,728 bytes (712.96 MiB) | Sanitizer/scenario checks passed |

The paired container had a 2 GiB memory limit and no swap allowance. All
memory-limit and OOM counters were zero at both start and finish. The
individual process peaks sum to about 0.96 GiB. The runner also correctly
rejected a release invocation in the earlier 1 GiB diagnostic container
before starting any workload. Raw paired artifacts and their hashes are in
`build/soak-memory-thread-investigation/paired-preflight/manifest.json`.

## Runtime memory budget

The ASan workload peaked at about 734 MiB. The native comparison has a
roughly 269 MiB peak during Argon2id verification and about 13 MiB between
operations. Their individual observed peaks sum to about 1 GiB; allow 2 GiB
for the paired runtime, with no swap allowance. The release runner rejects
a lower cgroup memory limit before starting. Building the client/server
binaries needs separate memory and is excluded from this runtime estimate.

The prior run failed a relative RSS-growth assertion, not an OOM kill.
Increasing available RAM alone does not resolve that assertion. The full
24-hour test must still establish that the corrected workload stays stable;
the shorter diagnostic cannot guarantee its eventual result.

The completed failed soak remains failed. A successful exact-candidate
24-hour run is still required; cross-platform and independent review gates
remain open.
