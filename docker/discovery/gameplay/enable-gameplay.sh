#!/usr/bin/env bash
# Add verified canonical content to the existing registration game, preserving state.
set -euo pipefail
umask 077
((EUID == 0)) || { echo 'Run with sudo on the NUC.' >&2; exit 1; }
exec 9>/run/lock/mwnet-gameplay.lock
flock -n 9 || { echo 'Another gameplay setup is running.' >&2; exit 1; }
bundle=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
registration=/home/gmax/mwnet-registration
settings=/etc/mwnet-gameplay
cd "$bundle"
sha256sum --check --status SHA256SUMS
fingerprint=$(python3 -c 'import json; print(json.load(open("content.json"))["fingerprint"])')
base=(docker compose -p mwnet-registration --env-file "$registration/.env" -f "$registration/compose.yml")
"${base[@]}" config --quiet
image=$("${base[@]}" config --images)
python3 "$bundle/verify_image.py" --manifest content.json --image "$image"
owner=$(stat -c %u "$bundle")
group=$(stat -c %g "$bundle")
((owner != 0)) || { echo 'The staged bundle must belong to the intended SSH operator.' >&2; exit 1; }
install -d -m 0700 /srv/mwnet "$settings" /var/backups/mwnet-gameplay
content_id=$(sha256sum content.json | cut -c1-16)
destination=/srv/mwnet/content-"$content_id"
if [[ ! -d $destination ]]; then
    content_tmp=$(mktemp -d /srv/mwnet/.content.XXXXXX)
    for name in Morrowind.esm Tribunal.esm Bloodmoon.esm; do
        install -m 0644 "content/$name" "$content_tmp/$name"
    done
    chmod 0755 "$content_tmp"
    mv -- "$content_tmp" "$destination"
fi
python3 - "$destination" <<'PY'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1])
manifest = json.load(open('content.json'))
assert list(manifest['files']) == ['Morrowind.esm', 'Tribunal.esm', 'Bloodmoon.esm']
for name, expected in manifest['files'].items():
    path = root / name
    with path.open('rb') as stream:
        assert path.stat().st_size == expected['size'] and hashlib.file_digest(stream, 'sha256').hexdigest() == expected['sha256'], name
PY
previous=("${base[@]}")
if [[ -e $settings/compose.content.yml || -e $settings/content.env ]]; then
    [[ -f $settings/compose.content.yml && -f $settings/content.env ]] || { echo 'Incomplete prior gameplay settings.' >&2; exit 1; }
    previous=(docker compose -p mwnet-registration --env-file "$registration/.env" --env-file "$settings/content.env"
              -f "$registration/compose.yml" -f "$settings/compose.content.yml")
fi
"${previous[@]}" config --quiet
stamp=$(date -u +%Y%m%dT%H%M%SZ)
backup=/var/backups/mwnet-gameplay/"$stamp"
mkdir -m 0700 "$backup"
if [[ -f $settings/compose.content.yml ]]; then
    cp -p "$settings/compose.content.yml" "$settings/content.env" "$backup/"
fi
transition=false
cleanup() {
    result=$?
    if ((result != 0)) && "$transition"; then
        echo 'Gameplay setup failed; restoring the previous container configuration.' >&2
        "${base[@]}" logs --tail 50 game || true
        if [[ -f $backup/compose.content.yml ]]; then
            cp -p "$backup/compose.content.yml" "$backup/content.env" "$settings/"
        else
            rm -f "$settings/compose.content.yml" "$settings/content.env"
        fi
        "${previous[@]}" up -d --no-build --pull never game || true
    fi
}
trap cleanup EXIT
transition=true
"${previous[@]}" stop game
# Back up only to a root-private host file. No credentials enter the journal.
docker run --rm --network none --read-only --cap-drop ALL --security-opt no-new-privileges=true \
    --memory 128m --cpus 1 --pids-limit 32 --user 10001:10001 \
    --mount type=volume,source=mwnet-registration_game-state,target=/state,readonly \
    --entrypoint tar "$image" -czf - -C /state . > "$backup/game-state.tar.gz"
tar -tzf "$backup/game-state.tar.gz" > /dev/null
install -m 0600 compose.content.yml "$settings/compose.content.yml.new"
printf 'MWNET_CONTENT_HOST_DIR=%s\n' "$destination" > "$settings/content.env.new"
mv -f "$settings/compose.content.yml.new" "$settings/compose.content.yml"
mv -f "$settings/content.env.new" "$settings/content.env"
active=(docker compose -p mwnet-registration --env-file "$registration/.env" --env-file "$settings/content.env"
        -f "$registration/compose.yml" -f "$settings/compose.content.yml")
"${active[@]}" config --quiet
started=$(date -u +%Y-%m-%dT%H:%M:%SZ)
"${active[@]}" up -d --no-build --pull never game
ready=false
for attempt in {1..45}; do
    logs=$("${active[@]}" logs --no-color --since "$started" game)
    if grep -Eq 'Loaded [1-9][0-9]* canonical spell and enchanted-item definitions' <<< "$logs" \
        && grep -Eq 'Installed [1-9][0-9]* canonical NPC recovery anchors' <<< "$logs" \
        && grep -q 'Called "OnServerPostInit"' <<< "$logs" \
        && grep -Fq "Server identity fingerprint: $fingerprint" <<< "$logs"; then
        ready=true; break
    fi
    sleep 2
done
"$ready" || { echo 'Canonical-content startup or identity validation failed.' >&2; exit 1; }
cid=$("${active[@]}" ps -q game)
[[ $(docker inspect --format '{{.State.Running}} {{.State.OOMKilled}} {{.RestartCount}} {{.HostConfig.Memory}} {{.HostConfig.NanoCpus}}' "$cid") == 'true false 0 2147483648 2000000000' ]] || {
    echo 'Unexpected container state, restarts or resource limits.' >&2; exit 1;
}
docker inspect --format 'Running={{.State.Running}} OOMKilled={{.State.OOMKilled}} MemoryLimit={{.HostConfig.Memory}} NanoCpus={{.HostConfig.NanoCpus}}' "$cid"
grep -E 'Server identity fingerprint:|Loaded .* canonical|Installed .* canonical|Called "OnServerPostInit"' <<< "$logs"
docker run --rm --network host --read-only --cap-drop ALL --security-opt no-new-privileges=true \
    --memory 128m --cpus 1 --pids-limit 32 --entrypoint python3 "$image" \
    /opt/registration/verify_listing.py --origin https://mwnet.playit.plus --host 209.25.141.17 --port 43133 \
    --fingerprint "$fingerprint" --connect-ip 127.0.0.1 --observe 65 --heartbeats 2 --deadline 120
# Export only the game access password, privately, for the operator's client UI.
password_tmp=$(mktemp "$settings/.access.XXXXXX")
docker cp "$cid:/state/access-password.txt" "$password_tmp"
[[ ! -L /home/gmax/.mwnet-gameplay-access-password ]] || { echo 'Refusing a symlink credential destination.' >&2; exit 1; }
install -m 0600 -o "$owner" -g "$group" "$password_tmp" /home/gmax/.mwnet-gameplay-access-password
rm -f "$password_tmp"
docker stats --no-stream --format 'Container={{.Name}} Memory={{.MemUsage}} CPU={{.CPUPerc}} Pids={{.PIDs}}' "$cid"
echo 'PASS: canonical content loaded, identity preserved, private state backed up and signed discovery verified.'
echo 'Operator access password is in /home/gmax/.mwnet-gameplay-access-password (0600); no game client has joined yet.'
echo "Private pre-upgrade state backup: $backup/game-state.tar.gz"
