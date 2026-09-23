#!/usr/bin/env bash
# Build the client and dedicated server in the Ubuntu 24.04 environment defined by Dockerfile.tes3mp.
set -euo pipefail
source_dir="$(cd "${1:-.}" && pwd)"
output_dir="${2:?Usage: build_tes3mp_linux.sh SOURCE OUTPUT}"
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd)"
commit="$(git -C "$source_dir" rev-parse HEAD)"
if ! git -C "$source_dir" diff --quiet HEAD --; then
    echo 'Candidate builds require a clean tracked source tree' >&2
    exit 1
fi
cmake -S "$source_dir" -B "$output_dir/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DBUILD_OPENMW=ON -DBUILD_OPENMW_MP=ON \
    -DBUILD_OPENCS=OFF -DBUILD_LAUNCHER=OFF -DBUILD_WIZARD=OFF \
    -DBUILD_MWINIIMPORTER=OFF -DBUILD_ESSIMPORTER=OFF \
    -DBUILD_BSATOOL=OFF -DBUILD_ESMTOOL=OFF -DBUILD_NIFTEST=OFF \
    -DBUILD_NAVMESHTOOL=OFF -DBUILD_BULLETOBJECTTOOL=OFF \
    -DBUILD_TES3MP_TESTS=ON -DBUILD_TES3MP_DIRECTORY=ON -DTES3MP_FETCH_DEPS=ON
cmake --build "$output_dir/build" --parallel "${TES3MP_BUILD_JOBS:-4}" \
    --target tes3mp tes3mp-server tes3mp-tests tes3mp-persistence-fault tes3mp-headless-integration \
        tes3mp-actor-packet-tests tes3mp-magic-content-tests tes3mp-directory tes3mp-discovery-tests
ctest --test-dir "$output_dir/build" --output-on-failure
python3 "$source_dir/CI/generate_spdx_sbom.py" --root "$source_dir" \
    --output "$output_dir/tes3mp-source.spdx.json"
python3 "$source_dir/CI/package_tes3mp_linux.py" \
    --source-dir "$source_dir" --build-dir "$output_dir/build" \
    --output-dir "$output_dir/packages" --commit "$commit" \
    --sbom "$output_dir/tes3mp-source.spdx.json"

for archive in "$output_dir"/packages/*.tar.gz; do
    python3 "$source_dir/CI/smoke_tes3mp_linux.py" "$archive" \
        --output-dir "$output_dir/smoke"
done
