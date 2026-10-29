#!/usr/bin/env bash
set -euo pipefail
umask 077
((EUID == 0)) || { echo 'Run with sudo on the NUC.' >&2; exit 1; }
account=${1:?Specify the game account name}
exec 9>/run/lock/mwnet-gameplay.lock
flock -n 9 || { echo 'Another gameplay operation is running.' >&2; exit 1; }
bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$bundle"
sha256sum --check --status SHA256SUMS
[[ -f .new-account-password && ! -L .new-account-password && $(stat -c %a .new-account-password) == 600 ]] || {
    echo 'Missing private password input.' >&2; exit 1;
}
[[ $(stat -c %s .new-account-password) -le 130 ]] || { echo 'Oversized password input.' >&2; exit 1; }
python3 - "$account" <<'PY'
import sys
from reset_account import canonical
canonical(sys.argv[1])
PY
active=(docker compose -p mwnet-registration --env-file /home/gmax/mwnet-registration/.env
        --env-file /etc/mwnet-gameplay/content.env -f /home/gmax/mwnet-registration/compose.yml
        -f /etc/mwnet-gameplay/compose.content.yml)
"${active[@]}" config --quiet
image=$("${active[@]}" config --images)
python3 verify_image.py --manifest deployment.json --image "$image"
backup=/var/backups/mwnet-account-reset/$(date -u +%Y%m%dT%H%M%SZ)
install -d -m 0700 /var/backups/mwnet-account-reset
mkdir -m 0700 "$backup"
stopped=false
cleanup() {
    result=$?
    if "$stopped"; then
        "${active[@]}" up -d --no-build --pull never game || true
    fi
    return "$result"
}
trap cleanup EXIT
stopped=true
"${active[@]}" stop game
docker run --rm --network none --read-only --cap-drop ALL --security-opt no-new-privileges=true \
    --memory 128m --cpus 1 --pids-limit 32 --user 10001:10001 \
    --mount type=volume,source=mwnet-registration_game-state,target=/state,readonly \
    --entrypoint tar "$image" -czf - -C /state . > "$backup/game-state.tar.gz"
tar -tzf "$backup/game-state.tar.gz" > /dev/null
docker run --rm -i --network none --read-only --cap-drop ALL --security-opt no-new-privileges=true \
    --memory 512m --cpus 1 --pids-limit 64 --user 10001:10001 --workdir /state \
    --mount type=volume,source=mwnet-registration_game-state,target=/state \
    --mount "type=bind,source=$bundle/reset_account.py,target=/opt/registration/reset_account.py,readonly" \
    -e XDG_CONFIG_HOME=/state/xdg-config -e XDG_DATA_HOME=/state/xdg-data \
    -e XDG_CACHE_HOME=/state/xdg-cache -e OPENMW_DISABLE_CRASH_CATCHER=1 \
    --entrypoint python3 "$image" /opt/registration/reset_account.py --account="$account" --create-if-missing \
    < .new-account-password
started=$(date -u +%Y-%m-%dT%H:%M:%SZ)
"${active[@]}" up -d --no-build --pull never game
stopped=false
ready=false
for attempt in {1..30}; do
    logs=$("${active[@]}" logs --no-color --since "$started" game)
    if grep -q 'Called "OnServerPostInit"' <<< "$logs"; then
        ready=true; break
    fi
    sleep 2
done
"$ready" || { echo 'Reset applied, but game startup needs inspection.' >&2; exit 1; }
rm -f .new-account-password
echo "PASS: account reset completed for $account; game restarted and temporary in-memory authentication lockouts cleared."
echo 'Use Log In with the new account password and the unchanged server access password.'
echo "Private pre-reset backup: $backup/game-state.tar.gz"
