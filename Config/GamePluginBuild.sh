# Game Plugin Build Script for macOS/Linux
# Author: erendgrmnc
#!/bin/bash



SOURCE_DIR="$1"
BUILD_DIR="$2"

BUILD_CONFIG="__ENGINE_CONFIG__"

echo "[INFO] Configuring plugin..."
cmake -S "$SOURCE_DIR" -B "$BUILD_DIR"
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake configure failed!"
    exit $?
fi

echo "[INFO] Building plugin..."
cmake --build "$BUILD_DIR" --config "$BUILD_CONFIG"
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake build failed!"
    exit $?
fi

echo "[SUCCESS] Plugin build finished."
