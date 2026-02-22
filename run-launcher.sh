#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/build/lib:$LD_LIBRARY_PATH"
export QT_QPA_PLATFORM_PLUGIN_PATH="$SCRIPT_DIR/build/platforms"
export OSG_LIBRARY_PATH="$SCRIPT_DIR/build/osgPlugins-3.6.5"
exec "$SCRIPT_DIR/build/openmw-launcher" "$@"
