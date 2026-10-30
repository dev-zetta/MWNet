#!/usr/bin/env bash
# Persistent, bounded, network-isolated overnight validation. Detached runs are
# never restarted automatically: an interruption invalidates elapsed acceptance.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
tools=$(realpath -- "${1:?Specify overnight tools root with bin/, caddy and evidence/compiled-provenance.json}")
output=$(realpath -m -- "${2:?Specify a fresh persistent output directory}")
shift 2
seconds=28800
arguments=("$@")
for ((index=0; index<${#arguments[@]}; index++)); do
  case ${arguments[index]} in
    --seconds)
      ((index+1<${#arguments[@]})) || { echo 'Missing --seconds value.' >&2; exit 1; }
      seconds=${arguments[index+1]}
      ;;
    --seconds=*) seconds=${arguments[index]#--seconds=} ;;
  esac
done
[[ $seconds =~ ^[0-9]+$ && ${#seconds} -le 5 ]] || { echo 'Invalid duration for external watchdog.' >&2; exit 1; }
((10#$seconds>=20 && 10#$seconds<=86400)) || { echo 'Duration outside bounded watchdog range.' >&2; exit 1; }
watchdog_seconds=$((10#$seconds+900))
[[ ! -e $output && -x $tools/bin/mwnet-headless-integration && -x $tools/bin/mwnet-directory && -x $tools/bin/mwnet-discovery-tests && -x $tools/caddy && -f $tools/evidence/compiled-provenance.json ]] || {
  echo 'Missing verified tools or output already exists.' >&2; exit 1;
}
for path in "$root" "$tools" "$output"; do [[ $path != *,* ]] || { echo 'Bind paths cannot contain commas.' >&2; exit 1; }; done
umask 077
mkdir -p -- "$(dirname -- "$output")"
fstype=$(df --output=fstype "$(dirname -- "$output")" | tail -n 1 | xargs)
[[ $fstype != tmpfs && $fstype != ramfs ]] || { echo 'Artifacts must be on persistent disk.' >&2; exit 1; }
docker_command=(docker)
if [[ -n ${MWNET_DOCKER_CONTEXT:-} ]]; then docker_command+=(--context "$MWNET_DOCKER_CONTEXT"); fi
mode=(--rm)
if [[ ${MWNET_OVERNIGHT_DETACH:-0} == 1 ]]; then
  name=${MWNET_OVERNIGHT_CONTAINER_NAME:-mwnet-overnight-$(date -u +%Y%m%dT%H%M%SZ)}
  [[ $name =~ ^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,62}$ ]] || { echo 'Invalid container name.' >&2; exit 1; }
  mode=(--detach --name "$name" --restart=no --stop-timeout 45)
fi
exec "${docker_command[@]}" run "${mode[@]}" --network none --read-only \
  --tmpfs /tmp:rw,nosuid,nodev,size=32m --memory "${MWNET_OVERNIGHT_MEMORY:-4g}" \
  --memory-swap "${MWNET_OVERNIGHT_MEMORY:-4g}" \
  --cpus "${MWNET_OVERNIGHT_CPUS:-4}" --pids-limit 512 --cap-drop ALL \
  --security-opt no-new-privileges --user "$(id -u):$(id -g)" \
  --log-driver local --log-opt max-size=5m --log-opt max-file=2 \
  --mount "type=bind,src=$root,dst=/src,readonly" \
  --mount "type=bind,src=$tools,dst=/tools,readonly" \
  --mount "type=bind,src=$(dirname -- "$output"),dst=/artifacts" \
  --entrypoint /usr/bin/timeout "${MWNET_OVERNIGHT_IMAGE:-mwnet-gameplay-builder:automated}" \
  --signal=TERM --kill-after=45s "${watchdog_seconds}s" \
  python3 /src/CI/run_mwnet_overnight.py --native /tools/bin/mwnet-headless-integration \
  --directory /tools/bin/mwnet-directory --discovery-tests /tools/bin/mwnet-discovery-tests \
  --caddy /tools/caddy --source-proof /tools/evidence/compiled-provenance.json \
  --output "/artifacts/$(basename -- "$output")" "$@"
