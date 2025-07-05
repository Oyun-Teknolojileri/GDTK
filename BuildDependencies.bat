@echo off
echo [%date% %time%]
echo =====================================
echo    Dependency Builder Started
echo    Platform: x64
echo =====================================
echo.
echo Updating git submodules...
git submodule init
git submodule update
echo.
echo =====================================
echo      WINDOWS BUILD (x64)
echo =====================================

echo Configuring Windows project...
echo -------------------------------------
cmake -S ./Dependency -B ./Dependency/Intermediate/Windows -A x64 -DSKIP_ASSIMP=FALSE -DSKIP_IMGUI=FALSE -DTK_PLATFORM=Windows -DTK_WINDOWS=Windows -DTOOLKIT_DIR=%~dp0
if %ERRORLEVEL% neq 0 goto error

echo.
echo [1/3] Building Debug configuration...
echo -------------------------------------
cmake --build ./Dependency/Intermediate/Windows --config Debug --target CopyDependencies
if %ERRORLEVEL% neq 0 goto error

echo.
echo [2/3] Building RelWithDebInfo configuration...
echo -------------------------------------
cmake --build ./Dependency/Intermediate/Windows --config RelWithDebInfo --target CopyDependencies
if %ERRORLEVEL% neq 0 goto error

echo.
echo [3/3] Building Release configuration...
echo -------------------------------------
cmake --build ./Dependency/Intermediate/Windows --config Release --target CopyDependencies
if %ERRORLEVEL% neq 0 goto error

echo.
echo =====================================
echo           WEB BUILD
echo =====================================
rem Check if Emscripten environment is available
where emcmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Emscripten environment not found. Please ensure emsdk is activated.
    pause
    exit /b 1
)

echo [1/3] Building Debug configuration...
echo -------------------------------------
call emcmake cmake -S ./Dependency -B ./Dependency/Intermediate/Web/Debug -DSKIP_ASSIMP=TRUE -DSKIP_IMGUI=FALSE -DTK_PLATFORM=Web -DCMAKE_BUILD_TYPE=Debug -DTOOLKIT_DIR=%~dp0
if %ERRORLEVEL% neq 0 goto error
call emmake cmake --build ./Dependency/Intermediate/Web/Debug
if %ERRORLEVEL% neq 0 goto error

echo.
echo [2/3] Building RelWithDebInfo configuration...
echo -------------------------------------
call emcmake cmake -S ./Dependency -B ./Dependency/Intermediate/Web/RelWithDebInfo -DSKIP_ASSIMP=TRUE -DSKIP_IMGUI=FALSE -DTK_PLATFORM=Web -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTOOLKIT_DIR=%~dp0
if %ERRORLEVEL% neq 0 goto error
call emmake cmake --build ./Dependency/Intermediate/Web/RelWithDebInfo
if %ERRORLEVEL% neq 0 goto error

echo.
echo [3/3] Building Release configuration...
echo -------------------------------------
call emcmake cmake -S ./Dependency -B ./Dependency/Intermediate/Web/Release -DSKIP_ASSIMP=TRUE -DSKIP_IMGUI=FALSE -DTK_PLATFORM=Web -DCMAKE_BUILD_TYPE=Release -DTOOLKIT_DIR=%~dp0
if %ERRORLEVEL% neq 0 goto error
call emmake cmake --build ./Dependency/Intermediate/Web/Release
if %ERRORLEVEL% neq 0 goto error

echo.
echo =====================================
echo       Build finished successfully
echo =====================================
goto end

:error
echo =====================================
echo    Build failed with error %ERRORLEVEL%
echo =====================================
pause
exit /b %ERRORLEVEL%

:end
pause