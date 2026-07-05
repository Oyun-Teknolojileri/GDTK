cmake_minimum_required(VERSION 3.16)

# ToolKit editor plugin CMake file.
#
# Produces a SHARED library the editor dlopen()s from
# <project>/Plugins/<name>/Codes/Bin/<name><ext> (see PublishManager) and runs
# inside the editor process. Only the engine is linked; SDL2 / imgui / Editor
# symbols are provided by the host editor at runtime.
#
# NOTE: the editor replaces the __projectname__ token below with the actual
# plugin name when a plugin is created from this template.

set(PLUGIN_PROJECT_NAME "__projectname__" CACHE STRING "What is the project name ?")

set(PLUGIN_CODE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PLUGIN_BIN_DIR "${PLUGIN_CODE_DIR}/Bin")

# --------------------------------------------------------------------------- #
# Get ToolKit directory.
# Resolution order (first match wins):
#   1. TOOLKIT_DIR cache variable  (cmake -DTOOLKIT_DIR=...)
#   2. TOOLKIT_DIR environment variable
#   3. Path.txt under the per-user ToolKit config directory:
#        Windows: %APPDATA%/ToolKit/Config/Path.txt
#        Linux:   $XDG_CONFIG_HOME/ToolKit/Config/Path.txt
#                 (fallback: ~/.config/ToolKit/Config/Path.txt)
# --------------------------------------------------------------------------- #
if(NOT DEFINED TOOLKIT_DIR)
    if(DEFINED ENV{TOOLKIT_DIR})
        set(TOOLKIT_DIR "$ENV{TOOLKIT_DIR}")
    else()
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
                "or provide a Path.txt under your ToolKit config directory.")
            return()
        endif()
    endif()
endif()

if(NOT EXISTS "${TOOLKIT_DIR}/ToolKit/Source/ToolKit.h")
    message(SEND_ERROR "Can't find ToolKit.h in '${TOOLKIT_DIR}'")
    message(STATUS "Checked path: '${TOOLKIT_DIR}/ToolKit/Source/ToolKit.h'")
    return()
endif()

# Create the project.
project(${PLUGIN_PROJECT_NAME})

# --------------------------------------------------------------------------- #
# Platform + configuration.
# --------------------------------------------------------------------------- #
set(TK_WINDOWS "Windows")
set(TK_LINUX "Linux")

if(NOT DEFINED TK_PLATFORM)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(TK_PLATFORM "${TK_WINDOWS}")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(TK_PLATFORM "${TK_LINUX}")
    else()
        message(FATAL_ERROR
            "Could not auto-detect TK_PLATFORM on host '${CMAKE_HOST_SYSTEM_NAME}'. "
            "Set it explicitly to one of: ${TK_WINDOWS}, ${TK_LINUX}.")
    endif()
endif()

# Pin the build configuration (dep wrappers select their artifact by a "d"
# suffix when CMAKE_BUILD_TYPE == Debug, and TK_DEPS_DIR embeds it as a path).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(TK_BUILD_TYPE "$<CONFIG>")
if(CMAKE_BUILD_TYPE)
    set(TK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()

# C++ standard and warnings (mirror the game project).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT MSVC)
    add_compile_options(-w)
endif()

string(TOLOWER ${CMAKE_CXX_COMPILER_ID} TK_COMPILER)
message("Compiling ${PLUGIN_PROJECT_NAME} with ${TK_COMPILER} for platform ${TK_PLATFORM} (${TK_BUILD_TYPE})")

# --------------------------------------------------------------------------- #
# Consume the pre-built GDTK engine as an IMPORTED target.
# --------------------------------------------------------------------------- #
set(TK_DEPS_DIR "${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")
list(APPEND CMAKE_PREFIX_PATH "${TK_DEPS_DIR}")

# Flat include path for the engine headers (subfolders use unqualified includes).
file(GLOB_RECURSE _TK_HEADERS "${TOOLKIT_DIR}/ToolKit/*.h")
set(_TK_ENGINE_INCLUDE_DIRS "${TOOLKIT_DIR}" "${TOOLKIT_DIR}/ToolKit")
foreach(_h ${_TK_HEADERS})
    get_filename_component(_d "${_h}" DIRECTORY)
    list(APPEND _TK_ENGINE_INCLUDE_DIRS "${_d}")
endforeach()
list(REMOVE_DUPLICATES _TK_ENGINE_INCLUDE_DIRS)

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

if(WIN32)
    set(_TK_ENGINE_LIB_DEBUG   "${TOOLKIT_DIR}/Bin/ToolKitd.dll")
    set(_TK_ENGINE_LIB_RELEASE "${TOOLKIT_DIR}/Bin/ToolKit.dll")
    set(_TK_ENGINE_IMPLIB_DEBUG   "${TOOLKIT_DIR}/Bin/ToolKitd.lib")
    set(_TK_ENGINE_IMPLIB_RELEASE "${TOOLKIT_DIR}/Bin/ToolKit.lib")
else()
    set(_TK_ENGINE_LIB_DEBUG   "${TOOLKIT_DIR}/Bin/libToolKitd.so")
    set(_TK_ENGINE_LIB_RELEASE "${TOOLKIT_DIR}/Bin/libToolKit.so")
endif()

if(NOT EXISTS "${_TK_ENGINE_LIB_DEBUG}" AND NOT EXISTS "${_TK_ENGINE_LIB_RELEASE}")
    message(FATAL_ERROR
        "ToolKit engine library not found under '${TOOLKIT_DIR}/Bin' "
        "(looked for libToolKit[d].so / ToolKit[d].dll). Build the engine first.")
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
        INTERFACE_LINK_LIBRARIES  "OpenGL32")
else()
    set_target_properties(TK::ToolKit PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${_TK_ENGINE_LIB_DEBUG}"
        IMPORTED_LOCATION_RELEASE "${_TK_ENGINE_LIB_RELEASE}"
        INTERFACE_LINK_LIBRARIES  "pthread;dl")
endif()

# --------------------------------------------------------------------------- #
# Plugin target.
# --------------------------------------------------------------------------- #
set(HEADERS PluginMain.h)

set(CMAKE_DEBUG_POSTFIX d) # Matches TK_PLUGIN_DEBUG_EXT ("d.so" / "d.dll").
add_library(${PLUGIN_PROJECT_NAME} SHARED PluginMain.cpp ${HEADERS})

# The editor loads the plugin name verbatim -- strip the default "lib" prefix.
set_target_properties(${PLUGIN_PROJECT_NAME} PROPERTIES PREFIX "")

target_link_libraries(${PLUGIN_PROJECT_NAME} PRIVATE TK::ToolKit)

target_include_directories(${PLUGIN_PROJECT_NAME} PRIVATE ${PLUGIN_CODE_DIR})

# Engine's stdafx.h as the shared PCH (single header only).
target_precompile_headers(${PLUGIN_PROJECT_NAME} PRIVATE
    "${TOOLKIT_DIR}/ToolKit/Source/stdafx.h")

# TK_DEBUG gate; TK_GL_3_0 / TK_DLL_IMPORT already flow in from TK::ToolKit.
target_compile_definitions(${PLUGIN_PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:TK_DEBUG>)

# Place artifacts in the plugin bin (where the editor looks for them).
set_target_properties(${PLUGIN_PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PLUGIN_BIN_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_BIN_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${PLUGIN_BIN_DIR})

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(isMultiConfig)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config_upper)
        set_target_properties(${PLUGIN_PROJECT_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
            LIBRARY_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
            PDB_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR})
    endforeach()
endif()

# Hot reload: rename the previous plugin output so the editor can release the
# file handle and reload the freshly built one.
set(DLL $<SHELL_PATH:$<TARGET_FILE:${PLUGIN_PROJECT_NAME}>>)
if(MSVC)
    set(_HOTRELOAD_PDB_ARG -Dpdb=$<SHELL_PATH:$<TARGET_PDB_FILE:${PLUGIN_PROJECT_NAME}>>)
else()
    set(_HOTRELOAD_PDB_ARG)
endif()
add_custom_command(TARGET ${PLUGIN_PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -Ddll=${DLL} ${_HOTRELOAD_PDB_ARG} -Dtmp="${PLUGIN_BIN_DIR}/tmp"
    -P "${PLUGIN_CODE_DIR}/CMakeHotReload.cmake")
