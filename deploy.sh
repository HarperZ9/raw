#!/bin/bash
# RAW Deploy Script — copies built DLLs + shaders to MO2 mod folder
# Usage: ./deploy.sh [modlist_path]

MODLIST="${1:-E:/Modlists/SkyGroundChronicles}"
MOD_DIR="$MODLIST/mods/RAW"
BUILD_DIR="$(dirname "$0")/build/Release"
SHADER_SRC="$(dirname "$0")/Shaders"

echo "=== RAW Deploy ==="
echo "Source:  $BUILD_DIR"
echo "Target:  $MOD_DIR"

# Create directories if needed
mkdir -p "$MOD_DIR/SKSE/plugins/RAW/Shaders"
mkdir -p "$MOD_DIR/SKSE/plugins/RAW/LUTs"
mkdir -p "$MOD_DIR/Root"

# Copy DLLs
cp "$BUILD_DIR/RAW.dll" "$MOD_DIR/SKSE/plugins/RAW.dll" && echo "[OK] RAW.dll"
cp "$BUILD_DIR/d3d11.dll" "$MOD_DIR/Root/d3d11.dll" && echo "[OK] d3d11.dll (proxy)"

# Copy all HLSL shaders (including new SharedRAW.hlsli)
cp "$SHADER_SRC"/*.hlsl "$MOD_DIR/SKSE/plugins/RAW/Shaders/" && echo "[OK] $(ls "$SHADER_SRC"/*.hlsl | wc -l) HLSL shaders"
cp "$SHADER_SRC"/*.hlsli "$MOD_DIR/SKSE/plugins/RAW/Shaders/" 2>/dev/null && echo "[OK] HLSLI headers"

echo "=== Deploy complete ==="
echo ""
echo "In-game: press INSERT to open DebugGUI"
echo "Click 'Recommended' for instant visual effects"
echo "Check SKSE log for 'RAW Pipeline State' diagnostics"
