#!/usr/bin/env bash

set -e

echo "=========================================="
echo "TES3MP Merged Branch Docker Build"
echo "=========================================="

# Build TES3MP (CrabNet is vendored in extern/crabnet and built automatically by CMake)
echo ""
echo ">> Building TES3MP merged branch..."
mkdir -p /tes3mp/build
cd /tes3mp/build

cmake .. \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_STANDARD=20 \
    -DBUILD_OPENMW=ON \
    -DBUILD_OPENMW_MP=ON \
    -DBUILD_BROWSER=ON \
    -DBUILD_LAUNCHER=ON \
    -DBUILD_WIZARD=OFF \
    -DBUILD_OPENCS=OFF \
    -DUSE_LUAJIT=ON

echo ""
# Cap parallel jobs to avoid OOM - Sol3/template compilation uses ~1-2GB RAM per job
BUILD_JOBS=$(( NPROC > 8 ? 8 : NPROC ))
echo ">> Compiling with ${BUILD_JOBS} cores (capped from ${NPROC} to avoid OOM)..."
make -j${BUILD_JOBS}

echo ""
echo "=========================================="
echo "Build completed successfully!"
echo "=========================================="
echo ""
echo "Executables are in: /tes3mp/build/"
ls -lh /tes3mp/build/tes3mp* 2>/dev/null || true
echo ""
