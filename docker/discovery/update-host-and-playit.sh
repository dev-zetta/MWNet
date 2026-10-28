#!/usr/bin/env bash
# Run as a detached systemd job: upgrading Tailscale can interrupt SSH.
set -euo pipefail

((EUID == 0)) || { echo 'Run with sudo on the intended server.' >&2; exit 1; }
MWNET_UPDATE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
for name in setup-host.sh update-playit.sh compose.playit-agent.yml compose.playit.yml playit.md; do
    [[ -f $MWNET_UPDATE_DIR/$name && ! -L $MWNET_UPDATE_DIR/$name ]] || {
        printf 'Missing regular update input: %s\n' "$name" >&2; exit 1;
    }
done
# Validate the existing credential/configuration before changing host packages.
/bin/bash -c 'source "$1"; preflight' update "$MWNET_UPDATE_DIR/update-playit.sh"
/bin/bash "$MWNET_UPDATE_DIR/setup-host.sh"
/bin/bash "$MWNET_UPDATE_DIR/update-playit.sh"
systemctl is-active --quiet docker tailscaled ssh
docker --version
docker compose version
tailscale version
echo 'Host and agent update completed. Check the playit dashboard before accepting public ingress.'
