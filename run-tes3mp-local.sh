#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${TES3MP_BUILD_DIR:-$REPO_ROOT/build}"
TEST_ROOT="${TES3MP_TEST_ROOT:-$REPO_ROOT/.tes3mp-test}"
CLIENT_CONFIG="$TEST_ROOT/client-config"
CLIENT_DATA="$TEST_ROOT/client-data"
DEFAULT_CLIENT_PROFILE="${XDG_CONFIG_HOME:-$HOME/.config}/openmw"
SERVER_LOG="$TEST_ROOT/tes3mp-server.log"
SERVER_PID_FILE="$TEST_ROOT/tes3mp-server.pid"
DATA_PATH_FILE="$TEST_ROOT/morrowind-data-path"
SERVER_BIN="$BUILD_DIR/tes3mp-server"
CLIENT_BIN="$BUILD_DIR/tes3mp"

data_dir="${TES3MP_DATA_PATH:-}"
client_profile="${TES3MP_CLIENT_PROFILE:-$DEFAULT_CLIENT_PROFILE}"
server_profile="${TES3MP_SERVER_PROFILE:-}"
keep_server=false
prepare_only=false
client_args=()

usage()
{
    printf '%s\n' \
        "Usage: ./run-tes3mp-local.sh [options] [-- client arguments]" \
        "" \
        "Starts an isolated localhost server and the TES3MP client together." \
        "The Morrowind data path is detected or requested once and remembered." \
        "The server is stopped automatically when the client exits." \
        "" \
        "Options:" \
        "  --data-dir PATH   Morrowind Data Files directory" \
        "  --client-profile PATH" \
        "                    Seed a new test profile from this OpenMW profile" \
        "  --server-profile PATH" \
        "                    Seed new server state from a CoreScripts server root" \
        "  --keep-server     Leave a server started by this script running" \
        "  --prepare-only    Create/update the test environment without launching" \
        "  -h, --help        Show this help" \
        "" \
        "Environment overrides:" \
        "  TES3MP_DATA_PATH  Same as --data-dir" \
        "  TES3MP_CLIENT_PROFILE" \
        "                    Same as --client-profile" \
        "  TES3MP_SERVER_PROFILE" \
        "                    Same as --server-profile" \
        "  TES3MP_BUILD_DIR  Build directory (default: ./build)" \
        "  TES3MP_TEST_ROOT  Persistent test state (default: ./.tes3mp-test)"
}

while (($# > 0)); do
    case "$1" in
        --data-dir)
            if (($# < 2)); then
                printf '%s\n' '--data-dir requires a path' >&2
                exit 2
            fi
            data_dir="$2"
            shift 2
            ;;
        --data-dir=*)
            data_dir="${1#*=}"
            shift
            ;;
        --client-profile)
            if (($# < 2)); then
                printf '%s\n' '--client-profile requires a path' >&2
                exit 2
            fi
            client_profile="$2"
            shift 2
            ;;
        --client-profile=*)
            client_profile="${1#*=}"
            shift
            ;;
        --server-profile)
            if (($# < 2)); then
                printf '%s\n' '--server-profile requires a path' >&2
                exit 2
            fi
            server_profile="$2"
            shift 2
            ;;
        --server-profile=*)
            server_profile="${1#*=}"
            shift
            ;;
        --keep-server)
            keep_server=true
            shift
            ;;
        --prepare-only)
            prepare_only=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            client_args+=("$@")
            break
            ;;
        *)
            client_args+=("$1")
            shift
            ;;
    esac
done

has_morrowind_data()
{
    [[ -n "$1" && ( -f "$1/Morrowind.esm" || -f "$1/morrowind.esm" ) ]]
}

detect_morrowind_data()
{
    local config line candidate
    local -a configs=()

    if [[ -n "${XDG_CONFIG_HOME:-}" ]]; then
        configs+=("$XDG_CONFIG_HOME/openmw/openmw.cfg")
    fi
    configs+=("$HOME/.config/openmw/openmw.cfg")

    for config in "${configs[@]}"; do
        [[ -f "$config" ]] || continue
        while IFS= read -r line; do
            [[ "$line" == data=* ]] || continue
            candidate="${line#data=}"
            candidate="${candidate#\"}"
            candidate="${candidate%\"}"
            if has_morrowind_data "$candidate"; then
                printf '%s\n' "$candidate"
                return 0
            fi
        done < "$config"
    done

    return 1
}

if [[ ! -x "$CLIENT_BIN" || ! -x "$SERVER_BIN" ]]; then
    printf 'TES3MP client/server binaries were not found in %s. Build them first.\n' "$BUILD_DIR" >&2
    exit 1
fi

mkdir -p "$TEST_ROOT" "$CLIENT_CONFIG" "$CLIENT_DATA"

seed_client_profile()
{
    local source_file destination_file
    local marker="$CLIENT_CONFIG/.profile-initialized"
    local -a profile_files=(
        settings.cfg
        input_v3.xml
        shaders.yaml
        global_storage.bin
        player_storage.bin
    )

    [[ -f "$marker" ]] && return

    if [[ ! -d "$client_profile" ]]; then
        printf 'Client profile not found: %s\n' "$client_profile" >&2
        printf '%s\n' 'Use --client-profile PATH to select an existing OpenMW profile.' >&2
        exit 1
    fi

    for source_file in "${profile_files[@]}"; do
        destination_file="$CLIENT_CONFIG/$source_file"
        source_file="$client_profile/$source_file"
        if [[ -f "$source_file" ]]; then
            cp -a "$source_file" "$destination_file"
        fi
    done

    printf '%s\n' "$client_profile" > "$marker"
    printf 'Initialized local client profile from %s\n' "$client_profile"
}

seed_client_profile

if [[ -z "$data_dir" && -f "$DATA_PATH_FILE" ]]; then
    IFS= read -r data_dir < "$DATA_PATH_FILE"
fi

if ! has_morrowind_data "$data_dir"; then
    data_dir="$(detect_morrowind_data || true)"
fi

if ! has_morrowind_data "$data_dir"; then
    if [[ -t 0 ]]; then
        printf 'Morrowind Data Files directory: '
        IFS= read -r data_dir
    fi
fi

if ! has_morrowind_data "$data_dir"; then
    printf '%s\n' \
        'Could not find Morrowind.esm.' \
        'Run again with: ./run-tes3mp-local.sh --data-dir "/path/to/Morrowind/Data Files"' >&2
    exit 1
fi

printf '%s\n' "$data_dir" > "$DATA_PATH_FILE"

content_files="Morrowind.esm"
if [[ -f "$data_dir/Tribunal.esm" || -f "$data_dir/tribunal.esm" ]]; then
    content_files+=",Tribunal.esm"
fi
if [[ -f "$data_dir/Bloodmoon.esm" || -f "$data_dir/bloodmoon.esm" ]]; then
    content_files+=",Bloodmoon.esm"
fi

if [[ ! -f "$CLIENT_CONFIG/tes3mp-client.cfg" ]]; then
    {
        printf '%s\n' \
            '[General]' \
            'destinationAddress = 127.0.0.1' \
            'port = 25565' \
            'accountName =' \
            'logLevel = 0' \
            '' \
            '[Chat]' \
            'keySay = Y' \
            'keyChatMode = F2' \
            'x = 0' \
            'y = 0' \
            'w = 390' \
            'h = 250' \
            'delay = 5.0'
    } > "$CLIENT_CONFIG/tes3mp-client.cfg"
fi

if "$prepare_only"; then
    TES3MP_TEST_ROOT="$TEST_ROOT" TES3MP_SERVER_PROFILE="$server_profile" \
        "$REPO_ROOT/run-tes3mp-server.sh" --prepare-only >/dev/null
    printf 'Local test environment prepared in %s\n' "$TEST_ROOT"
    exit 0
fi

server_pid=""
server_started=false

is_server_process()
{
    local process_name
    kill -0 "$1" 2>/dev/null || return 1
    process_name="$(ps -p "$1" -o comm= 2>/dev/null || true)"
    [[ "$process_name" == "tes3mp-server" ]]
}

if [[ -f "$SERVER_PID_FILE" ]]; then
    IFS= read -r server_pid < "$SERVER_PID_FILE"
    if [[ -z "$server_pid" ]] || ! is_server_process "$server_pid"; then
        server_pid=""
        rm -f "$SERVER_PID_FILE"
    fi
fi

if [[ -n "$server_pid" ]]; then
    printf 'Reusing TES3MP server process %s.\n' "$server_pid"
else
    printf 'Starting local TES3MP server...\n'
    TES3MP_TEST_ROOT="$TEST_ROOT" TES3MP_SERVER_PROFILE="$server_profile" \
        TES3MP_CONTENT_DATA_DIR="$data_dir" TES3MP_CONTENT_FILES="$content_files" \
        "$REPO_ROOT/run-tes3mp-server.sh" > "$SERVER_LOG" 2>&1 &
    server_pid=$!
    server_started=true
    printf '%s\n' "$server_pid" > "$SERVER_PID_FILE"

    for ((attempt = 0; attempt < 30; ++attempt)); do
        if ! kill -0 "$server_pid" 2>/dev/null; then
            wait "$server_pid" || true
            rm -f "$SERVER_PID_FILE"
            printf 'The local server failed to start. Log: %s\n' "$SERVER_LOG" >&2
            tail -n 40 "$SERVER_LOG" >&2
            exit 1
        fi
        sleep 0.1
    done
fi

cleanup()
{
    if "$server_started" && ! "$keep_server" && kill -0 "$server_pid" 2>/dev/null; then
        printf 'Stopping local TES3MP server...\n'
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
        rm -f "$SERVER_PID_FILE"
    elif "$server_started" && "$keep_server"; then
        printf 'Local server left running as process %s.\n' "$server_pid"
    fi
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

printf 'Starting TES3MP client against 127.0.0.1:25565...\n'
printf 'Client log: %s\n' "$CLIENT_CONFIG/openmw.log"

"$REPO_ROOT/run-tes3mp.sh" \
    --config "$CLIENT_CONFIG" \
    --user-data "$CLIENT_DATA" \
    --resources "$BUILD_DIR/resources" \
    --data "$data_dir" \
    --connect=127.0.0.1:25565 \
    --skip-menu \
    "${client_args[@]}"
