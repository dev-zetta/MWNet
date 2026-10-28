#!/usr/bin/env bash
# Update the existing mwnet-playit project without printing its credentials.
set -euo pipefail

PLAYIT_SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
PLAYIT_STATE_DIR=/etc/playit

die() { printf 'Error: %s\n' "$*" >&2; exit 1; }
compose() { docker compose -p mwnet-playit -f "$PLAYIT_STATE_DIR/compose.yml" "$@"; }

validate_credentials() {
    # Parse as data, never source the file or put the key on a command line.
    awk '
        /^SECRET_KEY=/ {
            count++; value=substr($0, 12); first=substr(value, 1, 1)
            if (!length(value) || /\r$/ || /REPLACE_WITH_|YOUR_ACTUAL_/ ||
                first == sprintf("%c", 34) || first == sprintf("%c", 39)) bad=1
        }
        /^SECRET_KEY_FILE=./ { bad=1 }
        END { exit !(count == 1 && !bad) }
    ' "$PLAYIT_STATE_DIR/agent.env" || die 'agent.env needs one nonempty, unquoted SECRET_KEY and no SECRET_KEY_FILE.'
}

preflight() {
    ((EUID == 0)) || die 'Run with sudo on the intended server.'
    [[ -d $PLAYIT_STATE_DIR && ! -L $PLAYIT_STATE_DIR ]] || die 'Expected /etc/playit directory is missing or symlinked.'
    local name
    for name in compose.yml agent.env; do
        [[ -f $PLAYIT_STATE_DIR/$name && ! -L $PLAYIT_STATE_DIR/$name ]] || die "Expected regular file: $PLAYIT_STATE_DIR/$name"
    done
    for name in compose.playit-agent.yml compose.playit.yml playit.md; do
        [[ -f $PLAYIT_SOURCE_DIR/$name && ! -L $PLAYIT_SOURCE_DIR/$name ]] || die "Missing update input: $name"
    done
    validate_credentials
    compose config --quiet
    docker info >/dev/null
}

main() {
    (($# == 0)) || die 'Usage: sudo bash update-playit.sh'
    preflight
    local current expected image current_project current_service backup updated status
    current=$(docker ps -aq --filter label=com.docker.compose.project=mwnet-playit \
        --filter label=com.docker.compose.service=playit)
    [[ -n $current && $current != *$'\n'* ]] || die 'Expected exactly one existing mwnet-playit/playit container; review docker compose ls first.'
    current_project=$(docker inspect --format '{{index .Config.Labels "com.docker.compose.project"}}' "$current")
    current_service=$(docker inspect --format '{{index .Config.Labels "com.docker.compose.service"}}' "$current")
    [[ $current_project == mwnet-playit && $current_service == playit ]] || die 'Unexpected existing agent project.'
    printf 'Current agent image: '
    docker inspect --format '{{.Config.Image}}' "$current"

    # Resolve and download the exact new image before changing the running setup.
    PLAYIT_ENV_FILE="$PLAYIT_STATE_DIR/agent.env" docker compose -p mwnet-playit \
        -f "$PLAYIT_SOURCE_DIR/compose.playit-agent.yml" config --quiet
    image=$(PLAYIT_ENV_FILE="$PLAYIT_STATE_DIR/agent.env" docker compose -p mwnet-playit \
        -f "$PLAYIT_SOURCE_DIR/compose.playit-agent.yml" config --images)
    [[ $image == ghcr.io/playit-cloud/playit-agent:1.1.0@sha256:* && $image != *$'\n'* ]] || die 'Unexpected update image.'
    docker pull "$image"
    expected=$(docker image inspect --format '{{.Id}}' "$image")
    backup=$(mktemp -d "$PLAYIT_STATE_DIR/backup.XXXXXXXX")
    chmod 0700 "$backup"
    cp -p -- "$PLAYIT_STATE_DIR/compose.yml" "$backup/compose.yml"
    printf 'Previous configuration saved in %s\n' "$backup"
    chown root:root "$PLAYIT_STATE_DIR/agent.env"
    chmod 0600 "$PLAYIT_STATE_DIR/agent.env"
    install -m 0600 "$PLAYIT_SOURCE_DIR/compose.playit-agent.yml" "$PLAYIT_STATE_DIR/compose.yml"
    if compose up -d --no-build --pull never; then
        sleep 5
        if updated=$(compose ps -q playit) && [[ -n $updated ]]; then
            if status=$(docker inspect --format '{{.State.Running}} {{.State.Restarting}} {{.State.OOMKilled}} {{.RestartCount}} {{.Image}}' "$updated") &&
                    [[ $status == "true false false 0 $expected" ]]; then
                install -m 0600 "$PLAYIT_SOURCE_DIR/compose.playit.yml" "$PLAYIT_STATE_DIR/compose.playit.yml"
                install -m 0600 "$PLAYIT_SOURCE_DIR/playit.md" "$PLAYIT_STATE_DIR/playit.md"
                compose ps
                docker inspect --format 'Network={{.HostConfig.NetworkMode}} Restart={{.HostConfig.RestartPolicy.Name}} Memory={{.HostConfig.Memory}} Pids={{.HostConfig.PidsLimit}} Logs={{json .HostConfig.LogConfig}}' "$updated"
                printf 'Verified running image: %s\nCheck the playit dashboard for agent authentication and tunnel status.\n' "$image"
                return
            fi
        fi
    fi
    # The previous image is retained locally; rollback does not require a pull.
    install -m 0600 "$backup/compose.yml" "$PLAYIT_STATE_DIR/compose.yml"
    compose up -d --no-build --pull never || die "Update and automatic rollback failed; configuration backup: $backup"
    die 'New container did not remain running; restored the previous configuration and image.'
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
    main "$@"
fi
