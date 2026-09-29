#!/usr/bin/env bash

set -e

echo "=========================================="
echo "MWNet Merged Branch Docker Build"
echo "=========================================="

# Build MWNet with its pinned transport dependencies.
echo ""
echo ">> Building MWNet merged branch..."
mkdir -p /mwnet/build
cd /mwnet/build

cmake .. \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_STANDARD=20 \
    -DBUILD_OPENMW=ON \
    -DBUILD_OPENMW_MP=ON \
    -DBUILD_LAUNCHER=ON \
    -DBUILD_WIZARD=OFF \
    -DBUILD_OPENCS=OFF \
    -DUSE_LUAJIT=ON \
    -DMWNET_FETCH_DEPS=ON

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
echo "Executables are in: /mwnet/build/"
ls -lh /mwnet/build/mwnet* 2>/dev/null || true
echo ""
