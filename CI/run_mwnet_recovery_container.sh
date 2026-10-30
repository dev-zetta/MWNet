#!/usr/bin/env bash
# Native recovery acceptance with no live volume or public network access.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
package=$(realpath -- "${1:?Specify an extracted verified Linux package}")
native=$(realpath -- "${2:?Specify the source-qualified native test build}")
proof=$(realpath -- "${3:?Specify its native build receipt JSON}")
fixture=$(realpath -- "${4:?Specify a completed isolated gameplay server fixture}")
fixture_receipt=$(realpath -- "$fixture/../gameplay-receipt.json")
output=$(realpath -m -- "${5:?Specify a new persistent-disk output directory}")
shift 5
[[ ! -e $output && -f $package/manifest.json && -x $native/mwnet-directory && -d $fixture/server/data ]] || {
    echo 'Expected package/native proof/completed fixture and a new output path.' >&2; exit 1;
}
for path in "$root" "$package" "$native" "$proof" "$fixture" "$output"; do
    [[ $path != *,* ]] || { echo 'Docker bind paths cannot contain commas.' >&2; exit 1; }
done
umask 077
mkdir -p -- "$(dirname -- "$output")"
docker_command=(docker)
if [[ -n ${MWNET_DOCKER_CONTEXT:-} ]]; then docker_command+=(--context "$MWNET_DOCKER_CONTEXT"); fi
old_mount=()
old_argument=()
if [[ -n ${MWNET_RECOVERY_OLD_ARCHIVE:-} ]]; then
    archive=$(realpath -- "$MWNET_RECOVERY_OLD_ARCHIVE")
    [[ $archive != *,* && ${MWNET_RECOVERY_OLD_SHA256:-} =~ ^[0-9a-f]{64}$ ]] || {
        echo 'Old package requires an explicit verified SHA256.' >&2; exit 1;
    }
    old_mount+=(--mount "type=bind,src=$archive,dst=/old.tar.gz,readonly")
    old_argument+=(--old-archive /old.tar.gz --old-archive-sha256 "$MWNET_RECOVERY_OLD_SHA256")
fi
container_name="mwnet-recovery-$(date +%s)-$$"
trap '"${docker_command[@]}" rm -f "$container_name" >/dev/null 2>&1 || true' EXIT
timeout --signal=TERM --kill-after=15s 15m "${docker_command[@]}" run --rm --name "$container_name" \
    --network none --read-only --tmpfs /tmp:rw,nosuid,nodev,size=32m \
    --memory "${MWNET_RECOVERY_MEMORY:-3g}" --cpus "${MWNET_RECOVERY_CPUS:-2}" --pids-limit 256 \
    --cap-drop ALL --security-opt no-new-privileges --user "$(id -u):$(id -g)" \
    --mount "type=bind,src=$root,dst=/src,readonly" \
    --mount "type=bind,src=$package,dst=/package,readonly" \
    --mount "type=bind,src=$native,dst=/native,readonly" \
    --mount "type=bind,src=$proof,dst=/native-proof.json,readonly" \
    --mount "type=bind,src=$fixture,dst=/fixture,readonly" \
    --mount "type=bind,src=$fixture_receipt,dst=/fixture-receipt.json,readonly" \
    --mount "type=bind,src=$(dirname -- "$output"),dst=/artifacts" \
    "${old_mount[@]}" --entrypoint python3 "${MWNET_RECOVERY_IMAGE:-mwnet-gameplay-builder:automated}" \
    /src/CI/run_mwnet_recovery.py --fixture-state /fixture --fixture-receipt /fixture-receipt.json --server /package/build/mwnet-server \
    --directory /native/mwnet-directory --native-build /native --native-receipt /native-proof.json \
    --package-root /package --output "/artifacts/$(basename -- "$output")" "${old_argument[@]}" "$@"
