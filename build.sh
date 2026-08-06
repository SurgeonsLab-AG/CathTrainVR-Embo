#!/usr/bin/env bash
# ── EmboNav build helper ─────────────────────────────────────────────────────
# Usage:
#   ./build.sh                   # full Qt6 GUI + CLI, Release
#   ./build.sh --core-only       # CLI only, no Qt required
#   ./build.sh --debug           # Debug build (GUI + CLI)

set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/build"
PRESET="Release"
GUI_FLAG="-DBUILD_GUI=ON"

for arg in "$@"; do
    case $arg in
        --core-only) GUI_FLAG="-DBUILD_GUI=OFF" ;;
        --debug)     PRESET="Debug"             ;;
    esac
done

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$PRESET" \
    $GUI_FLAG \
    -DBUILD_CLI=ON

cmake --build "$BUILD_DIR" --config "$PRESET" -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo ""
echo "Build complete → $BUILD_DIR"
echo ""
if [ "$GUI_FLAG" != "-DBUILD_GUI=OFF" ]; then
    echo "  GUI:  $BUILD_DIR/EmboNav"
fi
echo "  CLI:  $BUILD_DIR/embonav_cli"
echo ""
echo "Quick test:"
echo "  $BUILD_DIR/embonav_cli Data/3D/simplified_branches_.stl --dump /tmp/cl"
