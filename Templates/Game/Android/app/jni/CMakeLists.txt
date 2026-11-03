cmake_minimum_required(VERSION 3.6)

# Master android cmake for toolkit projects.
project(ToolKitApp)

# Get ToolKit directory.
# Resolution order (first match wins):
#   1. TOOLKIT_DIR cache variable  (cmake -DTOOLKIT_DIR=...)
#   2. TOOLKIT_DIR environment variable
#   3. Path.txt under the per-user ToolKit config directory:
#        Windows: %APPDATA%/ToolKit/Config/Path.txt
#        Linux:   $XDG_CONFIG_HOME/ToolKit/Config/Path.txt
#                 (fallback: ~/.config/ToolKit/Config/Path.txt)
#
# Gradle runs this file as the app's externalNativeBuild script, so the
# config directory is the only handle it has on the engine -- the same
# convention the game project's own CMakeLists.txt uses.
if(NOT DEFINED TOOLKIT_DIR)
    if(DEFINED ENV{TOOLKIT_DIR})
        set(TOOLKIT_DIR "$ENV{TOOLKIT_DIR}")
    else()
        # CMAKE_HOST_WIN32 (not WIN32) because Android cross compiles from the
        # host: under the NDK toolchain WIN32 describes the target, so the
        # Windows %APPDATA% config would never be consulted.
        if(CMAKE_HOST_WIN32)
            if(DEFINED ENV{APPDATA})
                set(TOOLKIT_CONFIG_DIR "$ENV{APPDATA}/ToolKit/Config")
            endif()
        else()
            if(DEFINED ENV{XDG_CONFIG_HOME})
                set(TOOLKIT_CONFIG_DIR "$ENV{XDG_CONFIG_HOME}/ToolKit/Config")
            elseif(DEFINED ENV{HOME})
                set(TOOLKIT_CONFIG_DIR "$ENV{HOME}/.config/ToolKit/Config")
            endif()
        endif()

        if(DEFINED TOOLKIT_CONFIG_DIR)
            set(CFG_FILE_PATH "${TOOLKIT_CONFIG_DIR}/Path.txt")
            if(EXISTS "${CFG_FILE_PATH}")
                file(STRINGS "${CFG_FILE_PATH}" TOOLKIT_DIR_LINES ENCODING UTF-8)
                list(GET TOOLKIT_DIR_LINES 0 TOOLKIT_DIR)
                string(STRIP "${TOOLKIT_DIR}" TOOLKIT_DIR)
                file(TO_CMAKE_PATH "${TOOLKIT_DIR}" TOOLKIT_DIR)
            endif()
        endif()
    endif()
endif()

if(NOT DEFINED TOOLKIT_DIR)
    message(SEND_ERROR
        "Can't locate ToolKit. Set the TOOLKIT_DIR cache variable, the TOOLKIT_DIR env var, "
        "or provide a Path.txt under your ToolKit config directory "
        "(Windows: %APPDATA%/ToolKit/Config/Path.txt, "
        "Linux: $XDG_CONFIG_HOME/ToolKit/Config/Path.txt or ~/.config/ToolKit/Config/Path.txt).")
    return()
endif()

if(NOT EXISTS "${TOOLKIT_DIR}/ToolKit/Source/ToolKit.h")
    message(SEND_ERROR "Can't find ToolKit.h in '${TOOLKIT_DIR}'")
    message(STATUS "Checked path: '${TOOLKIT_DIR}/ToolKit/Source/ToolKit.h'")
    return()
endif()

# Provide platform definitions.
set(TK_ANDROID "Android")
set(TK_WINDOWS "Windows")
set(TK_WEB "Web")

# Set platform-specific build directory.
set(TK_PLATFORM ${TK_ANDROID})

if(CMAKE_BUILD_TYPE)
	set(TK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
	set(TK_BUILD_TYPE "$<CONFIG>")
endif()

# Check if the compiler is Clang.
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set(CLANG_COMPILER TRUE)
	set(CMAKE_CXX_STANDARD 17)
	add_compile_options(-w) # All warnings off for Clang
else()
	# Android can only use clang. If you are here, something is wrong ...
	message(SEND_ERROR "This project requires Clang compiler to build. Please use a compatible compiler.")
	return()
endif()

# The engine root adds Dependency itself (its Android branch builds minizip, zstd and SDL2 from
# source, the same way the web branch does). Adding it here as well created every dependency target
# twice and CMake refused the duplicates.
set(SKIP_ASSIMP TRUE)
set(SKIP_IMGUI TRUE)
add_subdirectory(${TOOLKIT_DIR} ${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit)

# The game CMake links the engine through the same alias every other platform uses.
if(TARGET ToolKit AND NOT TARGET TK::ToolKit)
    add_library(TK::ToolKit ALIAS ToolKit)
endif()

# Android App project.
add_subdirectory(src)
