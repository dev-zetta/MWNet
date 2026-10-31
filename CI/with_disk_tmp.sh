#!/usr/bin/env bash
# Run any build/test/archive command with private scratch on the repository disk.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if (($# == 0)); then
    echo 'Usage: bash CI/with_disk_tmp.sh COMMAND [ARGUMENT ...]' >&2
    exit 2
fi
TMPDIR=$(python3 "$root/CI/mwnet_temp.py" --print-root)
export TMPDIR TMP="$TMPDIR" TEMP="$TMPDIR" MWNET_TMPDIR="$TMPDIR"
exec "$@"
