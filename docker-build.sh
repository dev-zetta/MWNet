#!/usr/bin/env bash

set -e

echo "=========================================="
echo "TES3MP Merged Branch Docker Build"
echo "=========================================="

# Build CrabNet first
if [ ! -f "/tes3mp/dependencies/crabnet/build/lib/libRakNetLibStatic.a" ]; then
    echo ""
    echo ">> Building CrabNet (RakNet fork)..."
    cd /tes3mp/dependencies/crabnet
    mkdir -p build
    cd build
    
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCRABNET_ENABLE_DLL=OFF \
          -DCRABNET_ENABLE_SAMPLES=OFF \
          -DCRABNET_ENABLE_STATIC=ON \
          ..
    
    make -j${NPROC}
    
    # Create case-insensitive symlink
    ln -sf /tes3mp/dependencies/crabnet/include/RakNet /tes3mp/dependencies/crabnet/include/raknet 2>/dev/null || true
    
    echo "CrabNet built successfully"
else
    echo "CrabNet already built, skipping..."
fi

# Build TES3MP
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
    -DRakNet_INCLUDES=/tes3mp/dependencies/crabnet/include \
    -DRakNet_LIBRARY_DEBUG=/tes3mp/dependencies/crabnet/build/lib/libRakNetLibStatic.a \
    -DRakNet_LIBRARY_RELEASE=/tes3mp/dependencies/crabnet/build/lib/libRakNetLibStatic.a

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
