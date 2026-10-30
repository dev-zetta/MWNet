# Actor and container packet encoding regression

Actor-list and container encoders previously marked a semantically invalid model as invalid without also invalidating the codec. The generic outbound preparation gate checks the codec, so it could accept the partially written prefix. Both encoders now set the sticky `InvalidValue` codec error when their identity validation fails. No gameplay protocol fields or message identifiers change.

The native actor-packet regression reproduced the outbound failure before the fix and passed afterward. Its coverage includes populated cached rosters and containers, mixed existing and generated identities, empty REQUEST packets, decoder reuse, malformed/truncated rollback, and refusal to prepare a semantically invalid outbound model. Local logs are retained under `build/public-gameplay-test/packet-sync-outbound-before-fix.log`, `packet-sync-outbound-after-fix.log` and `packet-sync-reproduction.log`.

Client integrity diagnostics now include the codec error, payload byte count, model validity and decoded count. These changes make future malformed-packet reports actionable without logging packet contents, account credentials or identities. The earlier manual gameplay integrity messages were not reproduced with the current native client; this encoder defect is a proven independent failure, rather than a proven explanation for every historical report.

The new unattended graphical procedure is being validated separately. Its development runs use an explicitly dirty source tree and are not acceptance evidence for a downloadable release. Final release evidence must identify the clean source revision and the exact packaged binaries.
