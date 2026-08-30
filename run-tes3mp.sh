#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/build/lib:$LD_LIBRARY_PATH"
export OSG_LIBRARY_PATH="$SCRIPT_DIR/build/osgPlugins-3.6.5"
export OPENMW_DISABLE_CRASH_CATCHER=1
exec "$SCRIPT_DIR/build/tes3mp" "$@"
