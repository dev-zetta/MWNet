#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build"
artifact_dir="$repo_root/build/tes3mp-soak-$(date -u +%Y%m%dT%H%M%SZ)"
cycles=100
duration_seconds=86400
clients=8
latency_ms=75
packet_loss_percent=2
release_gates=false
commit="${TES3MP_SOAK_COMMIT:-$(git -C "$repo_root" rev-parse HEAD 2>/dev/null || printf unknown)}"

usage() {
    printf '%s\n' \
        'Usage: CI/run_tes3mp_soak.sh [options]' \
        '' \
        'Options:' \
        '  --build-dir DIR              Directory containing tes3mp-headless-integration' \
        '  --artifacts-dir DIR          State, log and metrics output directory' \
        '  --cycles N                   Minimum complete lifecycle cycles (default: 100)' \
        '  --duration-seconds N         Minimum duration (default: 86400)' \
        '  --clients N                  Concurrent clients (default: 8)' \
        '  --latency-ms N               Deterministic message latency (default: 75)' \
        '  --packet-loss-percent N      Unreliable snapshot loss (default: 2)' \
        '  --commit HASH                Exact source commit recorded in metrics' \
        '  --release-gates              Reject values below the stable release gate' \
        '  --help                       Show this help'
}

while (($#)); do
    case "$1" in
        --build-dir)
            build_dir="$2"
            shift 2
            ;;
        --artifacts-dir)
            artifact_dir="$2"
            shift 2
            ;;
        --cycles)
            cycles="$2"
            shift 2
            ;;
        --duration-seconds)
            duration_seconds="$2"
            shift 2
            ;;
        --clients)
            clients="$2"
            shift 2
            ;;
        --latency-ms)
            latency_ms="$2"
            shift 2
            ;;
        --packet-loss-percent)
            packet_loss_percent="$2"
            shift 2
            ;;
        --commit)
            commit="$2"
            shift 2
            ;;
        --release-gates)
            release_gates=true
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

for value in "$cycles" "$duration_seconds" "$clients" "$latency_ms" "$packet_loss_percent"; do
    if [[ ! "$value" =~ ^[0-9]+$ ]]; then
        printf '%s\n' 'All numeric options must be unsigned integers.' >&2
        exit 2
    fi
done

if [[ "$release_gates" == true ]]; then
    if ((cycles < 100 || duration_seconds < 86400 || clients != 8)); then
        printf '%s\n' 'Release soak requires at least 100 cycles, 86400 seconds and exactly 8 clients.' >&2
        exit 2
    fi
    if ((latency_ms == 0 || packet_loss_percent == 0)); then
        printf '%s\n' 'Release soak requires non-zero simulated latency and packet loss.' >&2
        exit 2
    fi
    if [[ ! "$commit" =~ ^[0-9a-fA-F]{40}$ ]]; then
        printf '%s\n' 'Release soak requires an exact 40-character source commit.' >&2
        exit 2
    fi
fi

executable="$build_dir/tes3mp-headless-integration"
if [[ ! -x "$executable" ]]; then
    printf 'Missing headless integration executable: %s\n' "$executable" >&2
    printf '%s\n' 'Configure with BUILD_TES3MP_TESTS=ON and build tes3mp-headless-integration.' >&2
    exit 1
fi

mkdir -p "$artifact_dir"
state_dir="$artifact_dir/state"
metrics_file="$artifact_dir/metrics.json"
log_file="$artifact_dir/soak.log"

printf 'Starting TES3MP soak: %s clients, %s cycles, %s seconds minimum.\n' \
    "$clients" "$cycles" "$duration_seconds"
if ! "$executable" \
    --cycles "$cycles" \
    --clients "$clients" \
    --duration-seconds "$duration_seconds" \
    --latency-ms "$latency_ms" \
    --packet-loss-percent "$packet_loss_percent" \
    --state-dir "$state_dir" \
    --metrics-output "$metrics_file" \
    --commit "$commit" \
    --fail-on-memory-growth \
    >"$log_file" 2>&1; then
    printf 'Soak failed; inspect %s and retained state in %s.\n' \
        "$log_file" "$state_dir" >&2
    tail -n 80 "$log_file" >&2
    exit 1
fi

printf 'Soak passed. Metrics: %s; log: %s\n' "$metrics_file" "$log_file"
