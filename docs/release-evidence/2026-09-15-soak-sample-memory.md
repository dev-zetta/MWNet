# RSS sample storage correction, 2026-09-15

> Historical pre-rebrand evidence: LegacyMP/legacy-mp are display aliases for the former fork name in identifiers, commands, and artifact paths. They are not renamed artifacts or MWNet validation results. Hashes, revisions, measurements, and exit codes are unchanged; consult Git history for the original labels.

## Completed paired gate

The paired soak for `84c4314a5ce9edad4ffe258db6679148cca7e4e0` finished on
2026-09-11 at 19:59:43 UTC. Both processes completed 24 hours. Native
functional scenarios passed after 5,632 cycles, but its RSS-growth check
failed at 1.04912%. The sanitizer process completed 5,631 cycles and exited
0 with no reported ASan, LeakSanitizer or UBSan error. The paired gate failed.

The native window means were 12,673,096.75 and 12,806,053.06 bytes: an
increase of 132,956.31 bytes (129.84 KiB). Kernel process peaks were
281,501,696 bytes native and 778,178,560 bytes sanitizer. All cgroup
memory-limit and OOM event counters remained zero under the 2 GiB/no-swap
budget. Increasing that budget would not repair the relative-growth failure.

Artifacts are retained in `legacy-mp-alpha1-paired-soak-84c4314a5c` and locally
under `build/release-soak-84c4314a5c-results/`. The analysis and hashes are in
`build/soak-memory-journal-investigation/failed-run-analysis.json`.

## Observer allocation

`Scenario::observeMemory()` appended every RSS sample to a growing
`std::vector<uint64_t>`. Duration-based runs can greatly exceed the requested
minimum cycle count. The retained samples show a 24 KiB RSS step at sample
2,050 and a 64 KiB step at sample 4,098, immediately after vector expansion
at the preceding sample. RSS is read before the append. These steps alone
account for most of the failed run's measured increase.

An isolated control using the same Clang compiler, container and RSS probe
measured only sample collection, with no gameplay or connection churn:

| Recorder | Samples | Late minus early RSS mean | Growth |
| --- | ---: | ---: | ---: |
| Original vector | 5,632 | 79,068 bytes | 2.1986% |
| Buffered journal | 5,632 | 0 bytes | 0% |
| Original vector | 100,000 | 519,995 bytes | 13.3902% |
| Buffered journal | 100,000 | 0 bytes | 0% |

This establishes a measurement defect; it does not attribute every byte of
the full application's RSS increase to that vector or substitute for a new
24-hour run. The control source and log are retained in the investigation
directory as `observer-control.cpp` and `observer-control.log`.

The standard library documents vector reallocation and
[advance reservation](https://gcc.gnu.org/onlinedocs/gcc-13.4.0/libstdc++/api/a08718.html).
An estimated reservation would only defer the problem for longer runs.
The harness now journals every sample to `state/resident-memory-samples.bin`
using bounded stream buffers. Final JSON and window sums stream from that
file without loading all samples into memory. The binary journal is an
internal host-format artifact; `metrics.json` remains the portable report.
Storage/open/read/write failures fail the test. The journal closes before
temporary state cleanup, including on platforms that prohibit deleting an
open file.

The sample cadence, first-quarter warm-up, early/late window lengths and
strict greater-than-1% failure threshold remain unchanged. No samples are
dropped, no memory is subtracted, and sanitizer settings remain unchanged.
Replaying the failed run through the new recorder still reports 1.04912%
and rejects it (`replay.log`).

## Validation and replacement gate

Regression tests compare the original window formula and complete JSON
sample sequence at counts through 10,000, including both implicated vector
boundaries and the failed run's 5,632 samples. They also cover zero samples,
zero RSS, the exact 1% threshold, a failing 2% increase and truncated input.
Both native and ASan/UBSan builds passed all three CTest targets.

The shorter paired preflight and the replacement exact-candidate gate use
2 GiB with no swap. The full gate requires both processes to complete
24 hours successfully; its launch alone is not a pass. Cross-platform,
independent security, legal and beta gates remain separate requirements.
