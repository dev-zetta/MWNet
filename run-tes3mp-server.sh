#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${TES3MP_BUILD_DIR:-$REPO_ROOT/build}"
TEST_ROOT="${TES3MP_TEST_ROOT:-$REPO_ROOT/.tes3mp-test}"
SERVER_ROOT="$TEST_ROOT/server"
CONFIG_HOME="$TEST_ROOT/xdg-config"
DATA_HOME="$TEST_ROOT/xdg-data"
CACHE_HOME="$TEST_ROOT/xdg-cache"
SERVER_CONFIG="$CONFIG_HOME/openmw/tes3mp-server.cfg"
SERVER_BIN="$BUILD_DIR/tes3mp-server"
CORE_SCRIPTS="$REPO_ROOT/files/tes3mp/core-scripts"
SERVER_PROFILE="${TES3MP_SERVER_PROFILE:-}"
prepare_only=false

usage()
{
    printf '%s\n' \
        "Usage: ./run-tes3mp-server.sh [--prepare-only] [server arguments]" \
        "" \
        "Synchronizes the bundled CoreScripts and starts an isolated server" \
        "bound to 127.0.0.1:25565." \
        "" \
        "Environment overrides:" \
        "  TES3MP_BUILD_DIR   Build directory (default: ./build)" \
        "  TES3MP_SERVER_PROFILE" \
        "                       Seed new state from this CoreScripts server root" \
        "  TES3MP_TEST_ROOT   Persistent test state (default: ./.tes3mp-test)"
}

case "${1:-}" in
    --prepare-only)
        prepare_only=true
        shift
        ;;
    --help|-h)
        usage
        exit 0
        ;;
esac

if [[ ! -x "$SERVER_BIN" ]]; then
    printf 'TES3MP server binary not found: %s\nBuild it first.\n' "$SERVER_BIN" >&2
    exit 1
fi

mkdir -p "$SERVER_ROOT" "$CONFIG_HOME/openmw" "$DATA_HOME" "$CACHE_HOME"

# Preserve generated world and player data while refreshing executable scripts.
if [[ ! -f "$SERVER_ROOT/scripts/serverCore.lua" ]]; then
    cp -a "$CORE_SCRIPTS/." "$SERVER_ROOT/"
else
    cp -a "$CORE_SCRIPTS/scripts/." "$SERVER_ROOT/scripts/"
    cp -a "$CORE_SCRIPTS/lib/." "$SERVER_ROOT/lib/"
fi

if [[ -n "$SERVER_PROFILE" && ! -f "$SERVER_ROOT/.profile-initialized" ]]; then
    if [[ ! -d "$SERVER_PROFILE/data" ]]; then
        printf 'Server profile does not contain a data directory: %s\n' "$SERVER_PROFILE" >&2
        exit 1
    fi
    cp -a "$SERVER_PROFILE/data/." "$SERVER_ROOT/data/"
    printf '%s\n' "$SERVER_PROFILE" > "$SERVER_ROOT/.profile-initialized"
    printf 'Initialized local server state from %s\n' "$SERVER_PROFILE"
fi

if [[ ! -f "$SERVER_CONFIG" ]]; then
    {
        printf '%s\n' \
            '[General]' \
            'localAddress = 127.0.0.1' \
            'port = 25565' \
            'maximumPlayers = 8' \
            'hostname = TES3MP local test' \
            'logLevel = 0' \
            'password =' \
            '' \
            '[Plugins]' \
            "home = $SERVER_ROOT" \
            'plugins = serverCore.lua' \
            '' \
            '[MasterServer]' \
            'enabled = false' \
            'address = master.tes3mp.com' \
            'port = 25561' \
            'rate = 10000'
    } > "$SERVER_CONFIG"
fi

if "$prepare_only"; then
    printf 'Local server environment prepared in %s\n' "$TEST_ROOT"
    exit 0
fi

export XDG_CONFIG_HOME="$CONFIG_HOME"
export XDG_DATA_HOME="$DATA_HOME"
export XDG_CACHE_HOME="$CACHE_HOME"
export LD_LIBRARY_PATH="$BUILD_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export OSG_LIBRARY_PATH="$BUILD_DIR/osgPlugins-3.6.5"
export OPENMW_DISABLE_CRASH_CATCHER=1

cd "$TEST_ROOT"
exec "$SERVER_BIN" "$@"
