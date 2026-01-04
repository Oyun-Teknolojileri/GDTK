# Dependency initialisation and project build sh file.
# Author: erendgrmnc
#!/bin/bash

set -e  # Exit immediately if any command fails

# -----------------------------
# Paths & Setup
# -----------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")"; pwd)"
INTERMEDIATE_DIR="${SCRIPT_DIR}/Intermediate"

echo "===== GDTK Build ====="
echo "Detected platform: $(uname)"

# -----------------------------
# Detect platform
# -----------------------------
if [[ "$OSTYPE" == "darwin"* ]]; then
    PLATFORM="MacOS"
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    PLATFORM="TKNative"
else
    echo "[ERROR] Unsupported platform"
    exit 1
fi

# Optionally detect Emscripten for Web
if command -v emcmake >/dev/null 2>&1; then
    BUILD_WEB=true
else
    BUILD_WEB=false
fi

# -----------------------------
# Common build flags
# -----------------------------
BUILD_TYPE="Release"
GENERATOR="Ninja"

# -----------------------------
# Build Dependencies
# -----------------------------
echo "===== Building Dependencies ($BUILD_TYPE) ====="
mkdir -p "$INTERMEDIATE_DIR/$PLATFORM/Dependency"
cmake -S "$SCRIPT_DIR/Dependency" -B "$INTERMEDIATE_DIR/$PLATFORM/Dependency" \
    -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DTK_PLATFORM=$PLATFORM \
    -DTOOLKIT_DIR="$SCRIPT_DIR" \
    -DSKIP_ASSIMP=FALSE \
    -DSKIP_IMGUI=FALSE
cmake --build "$INTERMEDIATE_DIR/$PLATFORM/Dependency" --target CopyDependencies

# -----------------------------
# Build ToolKit
# -----------------------------
echo "===== Building ToolKit ($BUILD_TYPE) ====="
mkdir -p "$INTERMEDIATE_DIR/$PLATFORM/ToolKit"
cmake -S "$SCRIPT_DIR" -B "$INTERMEDIATE_DIR/$PLATFORM/ToolKit" \
    -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DTK_PLATFORM=$PLATFORM \
    -DTOOLKIT_DIR="$SCRIPT_DIR"
cmake --build "$INTERMEDIATE_DIR/$PLATFORM/ToolKit"

# -----------------------------
# Build Editor
# -----------------------------
echo "===== Building Editor ($BUILD_TYPE) ====="
mkdir -p "$INTERMEDIATE_DIR/$PLATFORM/Editor"
cmake -S "$SCRIPT_DIR/Editor" -B "$INTERMEDIATE_DIR/$PLATFORM/Editor" \
    -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DTK_PLATFORM=$PLATFORM \
    -DTOOLKIT_DIR="$SCRIPT_DIR"
cmake --build "$INTERMEDIATE_DIR/$PLATFORM/Editor"

# -----------------------------
# Optional Web build
# -----------------------------
if [ "$BUILD_WEB" = true ]; then
    echo "===== Web Build (Emscripten) ====="
    mkdir -p "$INTERMEDIATE_DIR/Web"
    emcmake cmake -S "$SCRIPT_DIR" -B "$INTERMEDIATE_DIR/Web" \
        -DTK_PLATFORM=TKWeb \
        -DTOOLKIT_DIR="$SCRIPT_DIR"
    emmake cmake --build "$INTERMEDIATE_DIR/Web"
fi

echo "===== Build finished successfully ====="