#!/usr/bin/env bash
# Isolated public registration/heartbeat/expiry/restart acceptance test.
set -euo pipefail
((EUID == 0)) || { echo 'Run with sudo on the NUC.' >&2; exit 1; }
registration_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$registration_dir"
sha256sum --check --status SHA256SUMS
[[ -f .env && ! -L .env ]] || { echo 'Missing test endpoint configuration.' >&2; exit 1; }
compose=(docker compose -p mwnet-registration --env-file "$registration_dir/.env" -f "$registration_dir/compose.yml")
"${compose[@]}" config --quiet
"${compose[@]}" build --pull game
image=$("${compose[@]}" config --images)
"${compose[@]}" run --rm --no-deps --entrypoint /usr/bin/ldd game /opt/mwnet/build/mwnet-server > "$registration_dir/runtime-libraries.txt"
if grep -q 'not found' "$registration_dir/runtime-libraries.txt"; then
    echo 'Game server has unresolved runtime libraries.' >&2; exit 1
fi
cleanup() {
    result=$?
    if ((result != 0)); then
        echo 'Registration test failed; stopping only the isolated test game server.' >&2
        "${compose[@]}" logs --tail 60 game || true
        "${compose[@]}" stop game || true
    fi
}
trap cleanup EXIT
# A startup smoke check with announcements disabled, before public registration.
ANNOUNCE=false "${compose[@]}" up -d --no-build --pull never game
fingerprint=''
for attempt in {1..15}; do
    fingerprint=$("${compose[@]}" logs --no-color game | sed -n 's/.*Server identity fingerprint: \(ed25519\/[A-Za-z0-9_-]*\).*/\1/p' | tail -n 1)
    if [[ -n $fingerprint ]] && "${compose[@]}" logs --no-color game | grep -q 'Called "OnServerPostInit"'; then break; fi
    sleep 2
done
[[ -n $fingerprint ]] || { echo 'Game did not initialize its identity.' >&2; exit 1; }
"${compose[@]}" logs --no-color game | grep -q 'Called "OnServerPostInit"' || { echo 'CoreScripts initialization failed.' >&2; exit 1; }
echo "Game server identity: $fingerprint"
"${compose[@]}" stop game
"${compose[@]}" logs --no-color game | grep -q 'Quitting peacefully.'
"${compose[@]}" up -d --no-build --pull never game
# Obtain only public endpoint values from canonical Compose output.
read -r public_address public_port < <("${compose[@]}" config --format json | python3 -c 'import json,sys; e=json.load(sys.stdin)["services"]["game"]["environment"]; print(e["PUBLIC_ADDRESS"],e["PUBLIC_PORT"])')
verify=(docker run --rm --network host --read-only --cap-drop ALL --security-opt no-new-privileges=true
    --memory 128m --cpus 1 --pids-limit 32 --entrypoint python3 "$image"
    /opt/registration/verify_listing.py --origin https://mwnet.playit.plus
    --host "$public_address" --port "$public_port" --fingerprint "$fingerprint" --connect-ip 127.0.0.1)
echo 'Phase 1: signed registration and at least 190 seconds of heartbeat observation.'
"${verify[@]}" --observe 190 --heartbeats 3 --deadline 120
# Deliberately stop announcements, then allow the complete 180-second lease to expire.
echo 'Phase 2: stopping test game; waiting 195 seconds for lease expiry.'
"${compose[@]}" stop game
sleep 195
"${verify[@]}" --absent --deadline 10
printf 'Directory cleanup metrics: '
docker exec mwnet-directory-directory-1 curl --fail --silent --show-error http://127.0.0.1:8080/metrics
printf '\nPhase 3: restarting game with the same stored identity.\n'
"${compose[@]}" up -d --no-build --pull never game
"${verify[@]}" --deadline 120
"${compose[@]}" ps
docker stats --no-stream --format 'Container={{.Name}} Memory={{.MemUsage}} CPU={{.CPUPerc}} Pids={{.PIDs}}' "$("${compose[@]}" ps -q game)"
echo 'PASS: real signed registration, encrypted directory verification, heartbeat renewal, expiry and identity-preserving re-registration.'
echo 'Test game remains running, password-protected. No canonical game content is loaded; gameplay is not certified.'
