#!/usr/bin/env bash
# Run rendered gameplay without host package installation or a physical display.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build=${1:?Usage: run_mwnet_gameplay_container.sh BUILD DATA OUTPUT [runner options]}
data=${2:?Specify the installed Morrowind Data Files directory}
output=${3:?Specify a new output directory on persistent disk}
shift 3
build=$(realpath -- "$build")
data=$(realpath -- "$data")
output=$(realpath -m -- "$output")
[[ -d $build && -d $data && -x $build/mwnet && -x $build/mwnet-server ]] || {
    echo 'Missing built Linux client/server or data directory.' >&2; exit 1;
}
[[ ! -e $output ]] || { echo 'Output already exists; preserve it and choose a new directory.' >&2; exit 1; }
for path in "$root" "$build" "$data" "$output"; do
    [[ $path != *,* ]] || { echo 'Docker bind paths cannot contain commas.' >&2; exit 1; }
done
umask 077
source "$root/CI/mwnet_disk_tmp.sh"
mwnet_prepare_container_tmp "$output"
docker_command=(docker)
if [[ -n ${MWNET_DOCKER_CONTEXT:-} ]]; then docker_command+=(--context "$MWNET_DOCKER_CONTEXT"); fi
package_mount=()
package_argument=()
if [[ -n ${MWNET_GAMEPLAY_PACKAGE_ROOT:-} ]]; then
    package_root=$(realpath -- "$MWNET_GAMEPLAY_PACKAGE_ROOT")
    [[ -f $package_root/manifest.json && -x $package_root/run-mwnet-server.sh && $package_root != *,* ]] || {
        echo 'Package root must be a verified extracted Linux package.' >&2; exit 1;
    }
    package_mount+=(--mount "type=bind,src=$package_root,dst=/package,readonly")
    package_argument+=(--package-root /package)
fi
exec "${docker_command[@]}" run --rm --network none --read-only \
    -e MWNET_GAMEPLAY_ISOLATED_CONTAINER=1 \
    --mount "type=bind,src=$MWNET_CONTAINER_TMP,dst=/tmp" \
    -e TMPDIR=/tmp -e TMP=/tmp -e TEMP=/tmp --shm-size 256m \
    --memory "${MWNET_GAMEPLAY_MEMORY:-12g}" --cpus "${MWNET_GAMEPLAY_CPUS:-6}" --pids-limit 512 \
    --cap-drop ALL --security-opt no-new-privileges \
    --user "$(id -u):$(id -g)" \
    --mount "type=bind,src=$root,dst=/src,readonly" \
    --mount "type=bind,src=$build,dst=/build,readonly" \
    --mount "type=bind,src=$(dirname -- "$output"),dst=/artifacts" \
    --mount "type=bind,src=$data,dst=/game-content,readonly" \
    "${package_mount[@]}" \
    --entrypoint python3 "${MWNET_GAMEPLAY_IMAGE:-mwnet-gameplay-builder:automated}" \
    /src/CI/run_mwnet_gameplay.py --client /build/mwnet --server /build/mwnet-server \
    --data /game-content --output "/artifacts/$(basename -- "$output")" "${package_argument[@]}" "$@"
