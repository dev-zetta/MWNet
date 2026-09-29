# Sanitizer soak memory investigation, 2026-09-09

> Historical pre-rebrand evidence: LegacyMP/legacy-mp are display aliases for the former fork name in identifiers, commands, and artifact paths. They are not renamed artifacts or MWNet validation results. Hashes, revisions, measurements, and exit codes are unchanged; consult Git history for the original labels.

## Failed candidate

The ASan/UBSan/LeakSanitizer soak for
`93c1b0496810e2e3b9ccaf4ff3338fa0b3453739` ran from
2026-09-07 21:55:10 UTC to 2026-09-08 21:55:19 UTC. The container
`legacy-mp-alpha1-sanitizer-soak-93c1b04968` exited with status 1 after 5,653
eight-client cycles with 75 ms simulated latency and 2% snapshot loss.
Post-warm-up average RSS grew from 401.05 MiB to 410.71 MiB (+2.40959%),
exceeding the existing 1% limit. The retained log contains no sanitizer
diagnostic, and Docker records no OOM kill.

The raw artifacts are retained locally under
`build/release-soak-93c1b04968-results/` and in the stopped container:

| Artifact | SHA-256 |
| --- | --- |
| `metrics.json` | `db8a67b227bdf7f41b90e9acc72418202aa5372f177efd4185a882a481e9686d` |
| `soak.log` | `f95f234ed1cb1c35cee5b7658b8b053e278392c12063f93ab3d3349d25bb70c9` |

The earlier `3cf3e3a45c` container was stopped after about three and a half
hours with exit 137, without a completed soak verdict. It is not evidence
of another completed memory-gate failure.

## Defect and correction

The headless harness's `transmit()` records traffic under destination-local
connection IDs. Traffic in opposite directions therefore creates records
for both the client ID and the server ID. `disconnect()` removed only the
server ID. Each reconnect left another client record in the scenario's
long-lived `ServerMetrics` map: eight records per cycle, or approximately
45,224 records over the failed run. This is retained live state; destruction
of the scenario eventually frees the map, so an exit-time leak report need
not identify it.

The fix removes both endpoint IDs on disconnect. The existing headless CTest
now checks that the metrics map is empty after every complete cycle and
after the final authentication/fingerprint scenarios. Traffic totals and
the RSS threshold are unchanged. The failure text now accurately describes
the average-window comparison instead of claiming every sample increased.

Production server accounting uses server-local IDs and already removes
those IDs on disconnect. The defect identified here is in the combined
client/server test harness, separate from the engine dynamic-record lifetime
fix in `93c1b04968`.

## Validation and remaining gate

A negative control adds the new per-cycle assertion to the old cleanup.
It fails after one two-client cycle with
`disconnected peers retained connection metrics`. The corrected final source
passes all three sanitizer CTest targets: `legacy-mp-headless`, `legacy-mp-unit`
and `legacy-mp-persistence-fault`, with leak detection enabled.

Diagnostic source variants and configuration are retained under
`build/soak-memory-investigation/`. They use LLVM's
[sanitizer allocator interface](https://github.com/llvm/llvm-project/blob/main/compiler-rt/include/sanitizer/allocator_interface.h)
to distinguish live allocations from allocator-reserved memory, and print
a terminal allocation profile. These accelerated runs omit simulated
latency and the RSS failure flag; they are allocation diagnostics, not
release-gate passes.

Both diagnostic variants completed 100 eight-client cycles and 16 dropped
snapshots. The original retained 800 disconnected records. Its terminal
allocation profile identifies 38,400 bytes in 800 live map nodes allocated
through `ServerMetrics::recordOutbound()` and `Scenario::transmit()`, plus
an 8,872-byte bucket array. The fixed variant retained zero records at every
cycle checkpoint and passed the new per-cycle assertion. Terminal live heap
fell from 592,453 bytes to 545,331 bytes. In the fixed run, sampled live
allocations stayed between 823,663 and 825,671 bytes from cycles 40 to 100.

Both short runs still set the RSS-growth metric (12.0948% before and
12.1534% after): their early comparison window overlaps sanitizer allocator
warm-up. The fixed run's allocator-reserved heap continued changing while
live allocations stayed approximately flat. This distinguishes the proven
connection-record cleanup from the still-unverified 24-hour RSS outcome.

| Diagnostic artifact | SHA-256 |
| --- | --- |
| `before.symbolized.log` | `8dd2833c3c31ea78ac789220034a84a081fe1fce1d99b8a0d6e82b98023be50e` |
| `after.log` | `40835010305f9c691a4b9bccde6069613b20164336963018eed2622aa9a62784` |

The original profile was re-symbolized against the saved `before-binary`
because its build output path had been relinked during the diagnostic.
Raw logs, exact diagnostic binaries, source variants and runtime settings
are retained alongside the symbolized log. These are modified diagnostic
builds derived from `93c1b04968`, not release-candidate artifacts.

The failed 24-hour result remains failed. A fresh exact-commit 24-hour
sanitizer soak must finish successfully before this gate can close. The
cross-platform, independent security, specialist legal and public beta gates
also remain open.
