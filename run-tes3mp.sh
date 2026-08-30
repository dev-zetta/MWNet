#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${TES3MP_BUILD_DIR:-$SCRIPT_DIR/build}"
export LD_LIBRARY_PATH="$BUILD_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export OSG_LIBRARY_PATH="$BUILD_DIR/osgPlugins-3.6.5"
export OPENMW_DISABLE_CRASH_CATCHER=1
exec "$BUILD_DIR/tes3mp" "$@"
