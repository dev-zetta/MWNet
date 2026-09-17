# Soak disconnect investigation, 2026-09-17

## Failed run and evidence limits

The paired gate for `0ea3f175c78e438672fdf462c245c63da3bc1091` stopped
after 62,163.67 seconds (17 hours 16 minutes), at 2026-09-16 09:22:55 UTC.
Native exited 1 with `message rejected: ContentVerified: connection is not
active`; the runner then terminated sanitizer with SIGTERM. Neither completed
24 hours. No cgroup memory-limit or OOM event occurred under 2 GiB/no swap.
Raw journals retain 4,075 native and 4,063 sanitizer samples, but the old
failure path did not write final JSON metrics or retain the close reason.

The generic error originates in the raw transport when its connection has
already been removed. The higher session layer can still report
`ContentVerified` until it consumes the queued disconnect event. At this
stage the harness sends an account request, performs synchronous password
authentication/persistence, and sends the authentication response. A read
timeout during that work can produce the observed error. The old log cannot
establish that this was the historical close reason.

Docker Desktop's VM kernel recorded these messages at 09:22:54.673 UTC,
less than one second before the failure:

```text
... 52 messages dropped ...
virtio_net virtio0 eth0: NETDEV WATCHDOG: CPU: 27: transmit queue 0 timed out 5120 ms
virtio_net virtio0 eth0: TX timeout on queue: 0
```

Runner heartbeat intervals stayed close to 60.12 seconds, so there is no
evidence of a long whole-container pause. The kernel event establishes a VM
network fault, not a proven causal link to the loopback connection. The
replacement run uses the native Docker engine (`--context default`) to remove
this VM layer. Docker documents the separate VM used by Desktop for Linux in
its [Linux FAQ](https://docs.docker.com/desktop/troubleshoot-and-support/faqs/linuxfaqs/).

Original results remain under `build/release-soak-0ea3f175c7-results/`.
Timestamped runner logs and the relevant kernel records are retained in
`build/soak-disconnect-investigation/failed-container.log` and `vm-watchdog.json`.

## Confirmed correction and failure reporting

`AccountStore` scrubbed and durably rewrote the legacy player file on every
successful login even when its login object was already migrated. It now
skips persistence when the replacement bytes are unchanged. Initial cleanup,
changed records and retries after write failure still use atomic persistence.
This removes unnecessary synchronous filesystem work; it does not prove the
historical disconnect's cause.

The harness now records cycle/client, endpoint roles and IDs, message type,
phase timing, and authentication duration. A failed send inspects a bounded
number of queued events for disconnect reasons. Unexpected disconnects fail
immediately. Exceptions save available samples and metrics with explicit
`incomplete`/`failed` status and `scenariosComplete`; they never print a passed
scenario message. The runner rejects incomplete reports even with exit zero
and hashes partial reports when a child fails. Recording remains bounded.

## Validation

- The account regression failed four assertions against the original code.
  The fix passes native and sanitizer unit tests; it checks zero write attempts
  for migrated data, reported write failure for dirty data, and successful retry.
- All three native and all three sanitizer CTest targets passed. All eleven
  paired-runner tests passed, including partial-report rejection and retention.
- A temporary diagnostic used a two-second read deadline and a three-second
  pause before replying in cycle three. Both builds rejected the reply with
  `ContentVerified`/`connection is not active`, recorded `read deadline exceeded`
  for both endpoints, and saved two completed cycles in an incomplete JSON
  report. This validates timeout diagnostics, not the historical root cause.
  Production timeout values were restored and the normal binaries rebuilt.

The replacement exact-image preflight and full gate use the native Docker
engine with 2 GiB and no swap. The full gate retains eight clients, 75 ms
latency, 2% packet loss, 24 hours, the 1% native RSS limit and full sanitizer
settings. A running replacement remains pending until its terminal result;
cross-platform, independent security, legal and beta gates remain separate.
