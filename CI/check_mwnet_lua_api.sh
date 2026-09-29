#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# The default inventory is exported from upstream 0.8.1 commit
# f1f584d6d828c04d97414879b7588646a0d56708 so fresh clones need no upstream refs.
baseline="${MWNET_LUA_API_BASELINE:-}"
export LC_ALL=C
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

paths=(
    apps/openmw-mp/Script/Functions
    apps/openmw-mp/Script/ScriptFunctions.hpp
)

if [[ -n "$baseline" ]]; then
    if ! git -C "$repo_root" rev-parse --verify --quiet "$baseline^{commit}" >/dev/null; then
        echo "Lua API baseline '$baseline' is unavailable" >&2
        exit 1
    fi
    git -C "$repo_root" grep -h -o -E '\{"[A-Za-z0-9_]+"' "$baseline" -- "${paths[@]}" \
        | sed 's/.*{"//' | sort -u > "$scratch/baseline"
else
    cp "$repo_root/CI/fixtures/lua-api-0.8.1.txt" "$scratch/baseline"
fi

grep -Rho -E '\{"[A-Za-z0-9_]+"' "${paths[@]/#/$repo_root/}" \
    | sed 's/.*{"//' | sort -u > "$scratch/current"

comm -23 "$scratch/baseline" "$scratch/current" > "$scratch/missing"

if [[ -s "$scratch/missing" ]]; then
    echo "Upstream 0.8.1 Lua bindings were removed:" >&2
    sed 's/^/  /' "$scratch/missing" >&2
    exit 1
fi

baseline_count="$(wc -l < "$scratch/baseline")"
current_count="$(wc -l < "$scratch/current")"
echo "Lua API compatibility: all $baseline_count baseline names are present ($current_count total)"
