# Autonomous recovery acceptance

`CI/run_mwnet_recovery.py` tests recovery using disposable state and actual native executables. It consumes a completed accepted rendered-game fixture, the exact packaged server and a source-qualified native build receipt. It verifies the server/package identity and native executable hashes before proceeding. It does not connect to the NUC, mount a live volume or modify the source fixture.

The procedure backs up the complete quiescent fixture, deliberately damages a separate working generation, restores into a new directory, compares every file and byte, then starts the actual server. A temporary acceptance script uses the normal CoreScripts world/player loading paths and compares the loaded data; the native identity fingerprint must remain unchanged. Native credential records are preserved byte-for-byte, but authentication against the restored rendered-game account is not replayed by this procedure.

The actual directory `--backup` command is tested while a SQLite reader prevents checkpointing and a writer has committed additional data to WAL. The standalone backup must contain both generations of committed block records, pass SQLite integrity checks and start a healthy native directory backend. Corrupt databases and unsupported newer schemas must be refused. The existing native discovery regression additionally verifies that restored signed listings remain hidden until endpoint identity verification succeeds. The native six-stage process-termination persistence test and synthetic legacy-credential authentication/migration test are also executed.

With a verified old Linux package, the old native server and its original CoreScripts create a synthetic world/character, including non-default health, attributes, skills, inventory and custom variables. The current packaged runtime must load that persisted data unchanged after replacing only scripts/libraries. Restoring the pre-upgrade snapshot must also allow the old runtime to load it again, with the original identity. This is actual old-runtime persistence compatibility for synthetic data; it does not establish old-version graphical character creation, account registration or a complete gameplay/login upgrade.

```bash
MWNET_DOCKER_CONTEXT=default \
MWNET_RECOVERY_OLD_ARCHIVE=/absolute/path/to/verified-alpha2-package.tar.gz \
MWNET_RECOVERY_OLD_SHA256=VERIFIED_64_CHARACTER_SHA256 \
bash CI/run_mwnet_recovery_container.sh \
  /absolute/path/to/extracted-alpha3-package \
  /absolute/path/to/exact-candidate-native-build \
  /absolute/path/to/native-build-receipt.json \
  /absolute/path/to/completed-gameplay-run/server \
  /persistent-disk/new-recovery-run \
  --expected-commit FULL_RUNTIME_COMMIT
```

The old-package variables are optional; omission records that compatibility path as not run. The gameplay fixture's sibling `gameplay-receipt.json` must report successful acceptance of the same runtime commit. The native receipt is the source-qualified dedicated-build record containing the tested binary hashes. The wrapper uses the already prepared `mwnet-gameplay-builder:automated` environment, no public network, read-only sources/root filesystem, three GiB memory, two CPUs, 256 PIDs and a fifteen-minute external deadline with container cleanup. Native subprocesses have individual timeouts; logs and output inventories have explicit limits. Test state is stored on persistent disk.

The wrapper also rejects RAM-backed output filesystems and creates a private, mode-0700 `.mwnet-tmp.<output-name>.XXXXXX` sibling on the same disk for container `/tmp`. Both host and container `TMPDIR`, `TMP` and `TEMP` use this disk scratch. The path is reported on stderr and retained alongside the evidence; there is no general system `/tmp` cleanup. After container exit and evidence preservation, remove only that specific scratch directory when it is no longer needed. Its temporary contents may contain sensitive fixture data and must remain private.

The snapshot helper is deliberately limited to quiescent isolated or equivalent same-UID state under a private, exclusively used parent directory. It refuses links/special files, traversal, duplicate/unexpected archive entries, payload digest mismatches, incomplete archives and size/count expansion limits. An external archive SHA256 is required for restore; a manifest alone is not authenticated evidence. Existing or dangling-symlink restore destinations are refused. Restore stages and verifies the complete tree before renaming it into place; this relies on the private-parent/no-concurrent-writer condition and is not a general concurrent production-volume replacement tool.

Archives and restored files are mode `0600`, directories are `0700`, and restored objects belong to the current UID. Original Unix ownership and permissions are intentionally not reproduced. A real deployment using UID 10001 needs a separately reviewed ownership/install step before activation. Preserve the previous deployment and stop its writers before any operational backup or cutover; the CI helper does not automate live production cutover.

Run `python3 CI/test_mwnet_recovery_snapshot.py` for adversarial helper regressions. The structured `recovery-receipt.json` records individual outcomes, executable/procedure hashes, source-fixture/native-build proof hashes and explicit migration limitations. Private archives, native identity keys, credentials, detailed expected profile contents and raw logs remain under the mode-0700 run directory and must not be published. A reviewed receipt may be copied into release evidence; a failed or missing verdict is not acceptance.
