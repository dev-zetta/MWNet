# MWNet 1.0 release gates

MWNet 1.0 milestone acceptance proceeds sequentially. Passing a later implementation milestone does not skip the evidence required by an earlier milestone, and the stable version remains blocked until every gate below has recorded artifacts from the exact release candidate. Development previews can provide binaries for testing without closing these gates.

## Milestones

| Milestone | Required scope | Current status |
| --- | --- | --- |
| `1.0.0-alpha.1` | Clean out-of-tree builds, protocol 12 only, fail-closed codec, unit/fuzz targets, loopback default | Implementation and earlier fuzz campaign present; paired native/sanitizer Linux soak passed at c978f30275; Linux discovery preview built and package-tested at e0b844eafa; cross-platform and remaining candidate evidence pending |
| `1.0.0-alpha.2` | GameNetworkingSockets, encrypted identity handshake, TOFU and Argon2id migration | Implementation present in the development preview; milestone acceptance still requires the preceding and exact-candidate evidence |
| `1.0.0-alpha.3` | Canonical authority, lifecycle gates, owned workers and atomic persistence | Implementation and automated encrypted integration/fault harnesses present; exact-candidate evidence pending |
| `1.0.0-beta.1` | Sanitizer and fuzz gates, limited opt-in public test, independent security review | Blocked |
| `1.0.0` | Cross-platform, soak, migration, public discovery, security, legal and documentation gates | Blocked |

The current source version is `1.0.0-alpha.2`, a cross-platform development preview with protocol 13. Its release notes identify the tested source and package validation results. The version number does not certify milestone acceptance, interactive gameplay on every platform, or suitability for untrusted public hosting; the remaining gates above and below still apply.

## Required evidence

- GCC, Clang and MSVC build and test results for the exact candidate.
- ASan/UBSan and TSan runs with no relevant defects.
- Round-trip coverage for every protocol message and malformed coverage for every truncation point, invalid UTF-8, trailing data and allocation limit.
- At least 24 aggregate CPU-hours of decoder fuzzing under ASan/UBSan, with every finding retained as a regression fixture. Use `CI/run_mwnet_fuzz_campaign.sh --release-budget` with a complete Clang build; it runs the protocol, transport, authentication and encrypted-handshake targets concurrently while retaining their corpora, logs and crash artifacts.
- Headless integration results for first trust, fingerprint mismatch, registration, legacy-account migration, lockout, duplicate initialization, reconnect, chat, movement, inventory, combat, jail, death and respawn. The `mwnet-headless-integration` CTest exercises these over real encrypted loopback connections.
- Fault injection at every persistence stage showing that either the old or new complete record remains recoverable. The `mwnet-persistence-fault` CTest terminates a writer process at every stage and verifies both immediate recovery and the next atomic save.
- One hundred connect/disconnect and death/respawn cycles.
- A paired 24-hour, eight-client soak with latency and loss simulation and no sanitizer defect, deadlock, application RSS growth above 1% after warm-up, or queue-limit violation. Run `CI/run_mwnet_soak.sh --release-gates` against native and ASan/UBSan builds of the exact candidate. The native build supplies the unchanged RSS-growth measurement; sanitizer allocator/stack retention is recorded separately, and full leak/error detection remains enabled. Both processes must finish successfully with matching workload and commit metadata. Use at least a 2 GiB container memory limit. Shorter developer runs are allowed only without that flag.
- Server tick p99, serialization p99, normalized inbound/outbound traffic and resident-memory comparison against the alpha.1 baseline. Compare like-for-like CI artifacts with `CI/compare_mwnet_performance.py`; a regression over 5% fails unless a non-empty reviewed justification is supplied explicitly.
- Public discovery API v1: signed announcement/replay/expiry tests, encrypted endpoint verification, browser acceptance, directory-outage fallback, backup restoration, a two-hour bounded churn/outage run with resource and final-cleanup evidence, and independent review of the new service. Public launch requires a new domain, VPS, DNS and HTTPS staging acceptance.
- Independent security review and remediation of release-blocking findings.
- Specialist review of MWNet's additional GPL terms and third-party notices. The project does not declare those terms compliant before that review.

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

- [September 18 graphical gameplay test](docs/release-evidence/2026-09-18-gameplay.md)
  records real-client failures after authentication. Gameplay acceptance is
  blocked despite successful compilation, package smoke checks and earlier soak.

- [September 23 public discovery acceptance](docs/release-evidence/2026-09-23-discovery.md) records the local directory, graphical browser, transport cleanup and Lua transition checks. Its two-hour churn result and the remaining public deployment and release gates are tracked separately.

- [September 23 committed Linux preview](docs/release-evidence/2026-09-23-linux-preview.md) records six native suites, three launcher regressions, source/SBOM/archive provenance and extracted-package smoke checks at `e0b844eafa`, including the corrected fresh-profile initialization failure.
