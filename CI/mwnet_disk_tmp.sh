#!/usr/bin/env bash
# Sourced by container wrappers. Keep temporary files on the artifact disk,
# including applications that use a literal /tmp instead of honoring TMPDIR.
mwnet_prepare_container_tmp() {
    local output=$1 parent fstype
    parent=$(dirname -- "$output")
    mkdir -p -- "$parent"
    fstype=$(df --output=fstype "$parent" | tail -n 1 | xargs)
    [[ -n $fstype && $fstype != tmpfs && $fstype != ramfs ]] || {
        echo 'Artifacts and container temporary files must be on persistent disk.' >&2
        return 1
    }
    MWNET_CONTAINER_TMP=$(mktemp -d -- "$parent/.mwnet-tmp.$(basename -- "$output").XXXXXX")
    chmod 700 -- "$MWNET_CONTAINER_TMP"
    export TMPDIR="$MWNET_CONTAINER_TMP" TMP="$MWNET_CONTAINER_TMP" TEMP="$MWNET_CONTAINER_TMP"
    # Retain this private directory alongside the evidence. A detached container
    # may still be using it after the wrapper exits; never remove it on EXIT.
    printf 'Container temporary files: %s\n' "$MWNET_CONTAINER_TMP" >&2
}
