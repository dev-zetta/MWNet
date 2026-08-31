#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build-fuzz"
corpus_root="$repo_root/fuzz-corpus"
artifact_root="$repo_root/fuzz-artifacts"
seconds=86400
release_budget=false

usage() {
    printf '%s\n' \
        'Usage: CI/run_tes3mp_fuzz_campaign.sh [options]' \
        '' \
        'Options:' \
        '  --build-dir DIR       Existing Clang/libFuzzer build directory' \
        '  --corpus-dir DIR      Persistent corpus directory' \
        '  --artifacts-dir DIR   Crash and log output directory' \
        '  --seconds N           Seconds per target (default: 86400)' \
        '  --release-budget      Reject campaigns shorter than 24 hours per target' \
        '  --help                Show this help'
}

while (($#)); do
    case "$1" in
        --build-dir)
            build_dir="$2"
            shift 2
            ;;
        --corpus-dir)
            corpus_root="$2"
            shift 2
            ;;
        --artifacts-dir)
            artifact_root="$2"
            shift 2
            ;;
        --seconds)
            seconds="$2"
            shift 2
            ;;
        --release-budget)
            release_budget=true
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

if [[ ! "$seconds" =~ ^[0-9]+$ ]] || ((seconds < 1)); then
    printf '%s\n' '--seconds must be a positive integer' >&2
    exit 2
fi
if [[ "$release_budget" == true ]] && ((seconds < 86400)); then
    printf '%s\n' 'A release campaign requires at least 86400 seconds per target.' >&2
    exit 2
fi

targets=(
    tes3mp-packet-fuzz
    tes3mp-transport-fuzz
    tes3mp-authentication-fuzz
    tes3mp-handshake-fuzz
)

mkdir -p "$corpus_root" "$artifact_root"
pids=()
names=()
for target in "${targets[@]}"; do
    executable="$build_dir/apps/tes3mp_fuzz/$target"
    if [[ ! -x "$executable" ]]; then
        printf 'Missing fuzzer: %s\n' "$executable" >&2
        printf '%s\n' 'Configure the complete TES3MP build with Clang and BUILD_TES3MP_FUZZERS=ON.' >&2
        exit 1
    fi
    mkdir -p "$corpus_root/$target" "$artifact_root/$target"
    printf 'Starting %s for %s seconds.\n' "$target" "$seconds"
    ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
        UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}" \
        "$executable" \
        -max_total_time="$seconds" \
        -artifact_prefix="$artifact_root/$target/" \
        -print_final_stats=1 \
        "$corpus_root/$target" \
        >"$artifact_root/$target/campaign.log" 2>&1 &
    pids+=("$!")
    names+=("$target")
done

failed=0
for index in "${!pids[@]}"; do
    if wait "${pids[$index]}"; then
        printf 'Completed %s.\n' "${names[$index]}"
    else
        printf 'Failed %s; inspect %s/%s.\n' \
            "${names[$index]}" "$artifact_root" "${names[$index]}" >&2
        failed=1
    fi
done

if ((failed)); then
    exit 1
fi

printf 'All four campaigns completed (%s target-seconds).\n' \
    "$((seconds * ${#targets[@]}))"
