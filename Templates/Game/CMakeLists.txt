cmake_minimum_required(VERSION 3.16)

# Master CMake for a ToolKit game project.
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
# TOOLKIT_DIR is resolved here (not in the shared helper) because the helper
# itself lives under ${TOOLKIT_DIR}/CMake/, so we need the path to find it.
if(NOT DEFINED TOOLKIT_DIR)
    if(DEFINED ENV{TOOLKIT_DIR})
        set(TOOLKIT_DIR "$ENV{TOOLKIT_DIR}")
    else()
        # Locate the per-user config directory in a platform-agnostic way.
        if(WIN32)
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

        if(NOT DEFINED TOOLKIT_DIR)
            message(SEND_ERROR
                "Can't locate ToolKit. Set the TOOLKIT_DIR cache variable, the TOOLKIT_DIR env var, "
                "or provide a Path.txt under your ToolKit config directory "
                "(Windows: %APPDATA%/ToolKit/Config/Path.txt, "
                "Linux: $XDG_CONFIG_HOME/ToolKit/Config/Path.txt or ~/.config/ToolKit/Config/Path.txt).")
            return()
        endif()
    endif()
endif()

if(NOT EXISTS "${TOOLKIT_DIR}/ToolKit/Source/ToolKit.h")
    message(SEND_ERROR "Can't find ToolKit.h in '${TOOLKIT_DIR}'")
    message(STATUS "Checked path: '${TOOLKIT_DIR}/ToolKit/Source/ToolKit.h'")
    return()
endif()

# A plugin/editor build is assumed when no target platform is provided.
# This MUST be decided before include(ToolKitEngine.cmake), which auto-detects
# TK_PLATFORM when it is unset (and would then hide the "was it provided?" signal).
set(PLUGIN_BUILD FALSE)
if(NOT DEFINED TK_PLATFORM)
    set(PLUGIN_BUILD TRUE)
    message("Preparing for plugin build.")
endif()

# Consume the pre-built GDTK engine. The helper is the single source of truth
# for engine consumption: it sets TK_PLATFORM / TK_DEPS_DIR / TK_BUILD_TYPE,
# pins C++17 + warnings, and defines the IMPORTED target TK::ToolKit. Engine-side
# consumption changes only need to touch ${TOOLKIT_DIR}/CMake/ToolKitEngine.cmake.
include("${TOOLKIT_DIR}/CMake/ToolKitEngine.cmake")

# App project
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/Codes ${CMAKE_CURRENT_LIST_DIR}/Codes/Intermediate/${TK_PLATFORM})
