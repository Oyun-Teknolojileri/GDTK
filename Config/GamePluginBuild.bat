@echo off
setlocal

set SOURCE_DIR=%1
set BUILD_DIR=%2

set BUILD_CONFIG=__ENGINE_CONFIG__

echo [INFO] Configuring plugin...
cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -A x64
if %errorlevel% neq 0 (
    echo [ERROR] CMake configure failed!
    exit /b %errorlevel%
)

echo [INFO] Building plugin...
cmake --build "%BUILD_DIR%" --config %BUILD_CONFIG%
if %errorlevel% neq 0 (
    echo [ERROR] CMake build failed!
    exit /b %errorlevel%
)

echo [SUCCESS] Plugin build finished.
endlocal
