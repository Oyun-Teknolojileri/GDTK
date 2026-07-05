cmake_minimum_required(VERSION 3.16)

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

# Provide platform definitions
set(TK_ANDROID "Android")
set(TK_WINDOWS "Windows")
set(TK_LINUX "Linux")
set(TK_WEB "Web")
set(TK_KNOWN_PLATFORMS ${TK_ANDROID} ${TK_WINDOWS} ${TK_LINUX} ${TK_WEB})

# A plugin/editor build is assumed when no target platform is provided.
set(PLUGIN_BUILD FALSE)
if (NOT DEFINED TK_PLATFORM)
    set(PLUGIN_BUILD TRUE)
    message("Preparing for plugin build.")
endif()

# Auto-detect TK_PLATFORM from the host system when it was not provided.
# Android and Web are cross-compiled and must always be passed explicitly.
if (NOT DEFINED TK_PLATFORM)
    if (CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(TK_PLATFORM "${TK_WINDOWS}")
    elseif (CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(TK_PLATFORM "${TK_LINUX}")
    else()
        message(FATAL_ERROR
            "Could not auto-detect TK_PLATFORM on host '${CMAKE_HOST_SYSTEM_NAME}'. "
            "Set it explicitly to one of: ${TK_KNOWN_PLATFORMS}.")
    endif()
    message("Auto-detected TK_PLATFORM: ${TK_PLATFORM}")
endif()

# Make sure TK_PLATFORM is one of the known platforms.
if (NOT TK_PLATFORM IN_LIST TK_KNOWN_PLATFORMS)
    message(FATAL_ERROR
        "TK_PLATFORM '${TK_PLATFORM}' is not valid. Must be one of: ${TK_KNOWN_PLATFORMS}.")
endif()

# Pin the build configuration. The vendored dependency wrappers select their
# prebuilt artifact by appending a "d" suffix when CMAKE_BUILD_TYPE == Debug,
# and TK_DEPS_DIR embeds ${CMAKE_BUILD_TYPE} as a path component, so an empty
# value resolves to the wrong folder. Multi-config generators (Visual Studio,
# Ninja Multi-Config) select the actual config per --build; this only fixes the
# configure-time path/suffix resolution. Mirrors GDTK/CMakeLists.txt.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()
if(NOT CMAKE_BUILD_TYPE AND CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

if(CMAKE_BUILD_TYPE)
	set(TK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
	set(TK_BUILD_TYPE "$<CONFIG>")
endif()

# Set C++ standard regardless of the compiler.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Turn off all warnings for any non-MSVC compiler (Clang, GCC, ...).
if (NOT MSVC)
    add_compile_options(-w)
endif()

# --------------------------------------------------------------------------- #
# Consume the pre-built GDTK engine as an IMPORTED target.
#
# The engine builds standalone (GDTK/CMakeLists.txt) and ships its shared
# library under ${TOOLKIT_DIR}/Bin (libToolKit.so / libToolKitd.so). We import
# it here instead of add_subdirectory()'ing the engine, so a game project
# builds against a fixed engine snapshot without recompiling the engine.
# Vendored dependencies (SDL2, imgui, ...) are expected to be pre-built under
# ${TOOLKIT_DIR}/Dependency/Intermediate/<Plat>/<Cfg> via build_dependencies.py.
# --------------------------------------------------------------------------- #

# Dependency lookup root. The dep wrappers (staged by build_dependencies.py as
# <dep>/<dep>Config.cmake) probe for the prebuilt .so/.a here.
set(TK_DEPS_DIR "${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")
list(APPEND CMAKE_PREFIX_PATH "${TK_DEPS_DIR}")

# Flat include path for the engine headers. ToolKit's sources live in many
# subfolders (Source, Render, Entities, ...) and its headers include each other
# unqualified (e.g. #include "Types.h"), so every subfolder must be on the path.
file(GLOB_RECURSE _TK_HEADERS "${TOOLKIT_DIR}/ToolKit/*.h")
set(_TK_ENGINE_INCLUDE_DIRS "${TOOLKIT_DIR}" "${TOOLKIT_DIR}/ToolKit")
foreach(_h ${_TK_HEADERS})
    get_filename_component(_d "${_h}" DIRECTORY)
    list(APPEND _TK_ENGINE_INCLUDE_DIRS "${_d}")
endforeach()
list(REMOVE_DUPLICATES _TK_ENGINE_INCLUDE_DIRS)

# Header-only vendored deps referenced by ToolKit's public headers (glm, ...).
list(APPEND _TK_ENGINE_INCLUDE_DIRS
    "${TOOLKIT_DIR}/Dependency"
    "${TOOLKIT_DIR}/Dependency/glm"
    "${TOOLKIT_DIR}/Dependency/glad"
    "${TOOLKIT_DIR}/Dependency/RapidXml"
    "${TOOLKIT_DIR}/Dependency/stb"
    "${TOOLKIT_DIR}/Dependency/miniaudio"
    "${TOOLKIT_DIR}/Dependency/poolSTL/include"
    "${TOOLKIT_DIR}/Dependency/tkimgui"
    "${TOOLKIT_DIR}/Dependency/tkimgui/imgui"
    "${TOOLKIT_DIR}/Dependency/SDL2/include")

# Engine shared library, per configuration.
if(WIN32)
    set(_TK_ENGINE_LIB_DEBUG    "${TOOLKIT_DIR}/Bin/ToolKitd.dll")
    set(_TK_ENGINE_LIB_RELEASE  "${TOOLKIT_DIR}/Bin/ToolKit.dll")
    set(_TK_ENGINE_IMPLIB_DEBUG   "${TOOLKIT_DIR}/Bin/ToolKitd.lib")
    set(_TK_ENGINE_IMPLIB_RELEASE "${TOOLKIT_DIR}/Bin/ToolKit.lib")
else()
    set(_TK_ENGINE_LIB_DEBUG   "${TOOLKIT_DIR}/Bin/libToolKitd.so")
    set(_TK_ENGINE_LIB_RELEASE "${TOOLKIT_DIR}/Bin/libToolKit.so")
endif()

if(NOT EXISTS "${_TK_ENGINE_LIB_DEBUG}" AND NOT EXISTS "${_TK_ENGINE_LIB_RELEASE}")
    message(FATAL_ERROR
        "ToolKit engine library not found under '${TOOLKIT_DIR}/Bin' "
        "(looked for libToolKit[d].so / ToolKit[d].dll).\n"
        "Build the engine first, e.g.:\n"
        "  python3 ${TOOLKIT_DIR}/BuildScripts/build_dependencies.py --configs ${CMAKE_BUILD_TYPE}\n"
        "  cmake -S ${TOOLKIT_DIR} -B ${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}\n"
        "  cmake --build ${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")
endif()

add_library(TK::ToolKit SHARED IMPORTED)
set_target_properties(TK::ToolKit PROPERTIES
    IMPORTED_CONFIGURATIONS "Debug;Release"
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug
    MAP_IMPORTED_CONFIG_MINSIZEREL Release
    INTERFACE_INCLUDE_DIRECTORIES "${_TK_ENGINE_INCLUDE_DIRS}"
    INTERFACE_COMPILE_DEFINITIONS "TK_GL_3_0;$<$<CONFIG:Debug>:TK_DEBUG>;TK_DLL_IMPORT"
)
if(WIN32)
    set_target_properties(TK::ToolKit PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${_TK_ENGINE_LIB_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${_TK_ENGINE_LIB_RELEASE}"
        IMPORTED_IMPLIB_DEBUG     "${_TK_ENGINE_IMPLIB_DEBUG}"
        IMPORTED_IMPLIB_RELEASE   "${_TK_ENGINE_IMPLIB_RELEASE}"
        INTERFACE_LINK_LIBRARIES  "OpenGL32"
    )
else()
    set_target_properties(TK::ToolKit PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${_TK_ENGINE_LIB_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${_TK_ENGINE_LIB_RELEASE}"
        INTERFACE_LINK_LIBRARIES  "pthread;dl"
    )
endif()

# App project
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/Codes ${CMAKE_CURRENT_LIST_DIR}/Codes/Intermediate/${TK_PLATFORM})
