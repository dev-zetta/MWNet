# TES3MP 1.0 release gates

TES3MP 1.0 is released sequentially. Passing a later implementation milestone does not skip the evidence required by an earlier release, and the stable version remains blocked until every gate below has recorded artifacts from the exact release candidate.

## Milestones

| Milestone | Required scope | Current status |
| --- | --- | --- |
| `1.0.0-alpha.1` | Clean out-of-tree builds, protocol 11 only, fail-closed codec, unit/fuzz targets, loopback default | Implementation and earlier fuzz campaign present; paired native/sanitizer Linux soak passed at c978f30275; Linux client/server preview built at 56fcea73f4; cross-platform and remaining candidate evidence pending |
| `1.0.0-alpha.2` | GameNetworkingSockets, encrypted identity handshake, TOFU and Argon2id migration | Implementation present on the alpha branch; milestone is not released out of sequence |
| `1.0.0-alpha.3` | Canonical authority, lifecycle gates, owned workers and atomic persistence | Implementation and automated encrypted integration/fault harnesses present; exact-candidate evidence pending |
| `1.0.0-beta.1` | Sanitizer and fuzz gates, limited opt-in public test, independent security review | Blocked |
| `1.0.0` | Cross-platform, soak, migration, security, legal and documentation gates | Blocked |

The source version remains `1.0.0-alpha.1` until the alpha.1 release candidate passes its gates. It must then advance through alpha.2, alpha.3 and beta.1; implemented future-scope work does not change that ordering.

## Required evidence

- GCC, Clang and MSVC build and test results for the exact candidate.
- ASan/UBSan and TSan runs with no relevant defects.
- Round-trip coverage for every protocol message and malformed coverage for every truncation point, invalid UTF-8, trailing data and allocation limit.
- At least 24 aggregate CPU-hours of decoder fuzzing under ASan/UBSan, with every finding retained as a regression fixture. Use `CI/run_tes3mp_fuzz_campaign.sh --release-budget` with a complete Clang build; it runs the protocol, transport, authentication and encrypted-handshake targets concurrently while retaining their corpora, logs and crash artifacts.
- Headless integration results for first trust, fingerprint mismatch, registration, legacy-account migration, lockout, duplicate initialization, reconnect, chat, movement, inventory, combat, jail, death and respawn. The `tes3mp-headless-integration` CTest exercises these over real encrypted loopback connections.
- Fault injection at every persistence stage showing that either the old or new complete record remains recoverable. The `tes3mp-persistence-fault` CTest terminates a writer process at every stage and verifies both immediate recovery and the next atomic save.
- One hundred connect/disconnect and death/respawn cycles.
- A paired 24-hour, eight-client soak with latency and loss simulation and no sanitizer defect, deadlock, application RSS growth above 1% after warm-up, or queue-limit violation. Run `CI/run_tes3mp_soak.sh --release-gates` against native and ASan/UBSan builds of the exact candidate. The native build supplies the unchanged RSS-growth measurement; sanitizer allocator/stack retention is recorded separately, and full leak/error detection remains enabled. Both processes must finish successfully with matching workload and commit metadata. Use at least a 2 GiB container memory limit. Shorter developer runs are allowed only without that flag.
- Server tick p99, serialization p99, normalized inbound/outbound traffic and resident-memory comparison against the alpha.1 baseline. Compare like-for-like CI artifacts with `CI/compare_tes3mp_performance.py`; a regression over 5% fails unless a non-empty reviewed justification is supplied explicitly.
- Independent security review and remediation of release-blocking findings.
- Specialist review of TES3MP's additional GPL terms and third-party notices. The project does not declare those terms compliant before that review.

CI artifacts, fuzz corpora, soak logs, performance reports and review records must identify the tested commit. Human or time-based gates may not be replaced with an unverified checklist entry.

## Recorded evidence

- [`1.0.0-alpha.1` long-running validation](docs/release-evidence/1.0.0-alpha.1.md)
  records the exact-commit four-target sanitizer fuzz campaign, 24-hour
  eight-client release soak, alpha.1 performance baseline and the gates those
  results do not close.

- [September 18 paired soak and Linux preparation](docs/release-evidence/2026-09-18-linux-candidate.md)
  records the successful paired run at `c978f30275`, its resource caveat, and
  the subsequent dedicated-server build fixes. The older soak does not certify
  changes made while preparing the next candidate.
