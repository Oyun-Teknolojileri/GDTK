# ToolKitEngine.cmake -- engine consumption helper for game / plugin projects.
#
# include()'d by a project's CMakeLists AFTER it has resolved TOOLKIT_DIR (and,
# for game projects, decided PLUGIN_BUILD). This is the single source of truth
# for "how a client consumes the pre-built GDTK engine", so engine-side changes
# (new dependency, library rename, include-folder reshuffle, ...) only need to
# touch this file -- every game/plugin project picks them up automatically.
#
# include("${TOOLKIT_DIR}/CMake/ToolKitEngine.cmake")
#
# Inputs (must be set before include()):
#   TOOLKIT_DIR  -- absolute path to the GDTK engine root.
#   TK_PLATFORM  -- optional; "Windows" | "Linux". Auto-detected from the host
#                   if unset. Game projects read PLUGIN_BUILD from whether the
#                   caller already defined TK_PLATFORM, so they must decide that
#                   before include()'ing this file.
#
# Provides (in the caller's scope):
#   TK_PLATFORM, TK_WINDOWS, TK_LINUX, TK_BUILD_TYPE, TK_DEPS_DIR
#   the IMPORTED target TK::ToolKit (link it; it carries the engine's public
#   include dirs + compile definitions: TK_GL_3_0, TK_DLL_IMPORT, TK_DEBUG).

if(NOT DEFINED TOOLKIT_DIR)
    message(FATAL_ERROR
        "TOOLKIT_DIR must be set before include(ToolKitEngine.cmake). "
        "Resolve it in your project's CMakeLists.txt first.")
endif()

# --------------------------------------------------------------------------- #
# Platform constants + detection.
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
    message("Auto-detected TK_PLATFORM: ${TK_PLATFORM}")
endif()

# --------------------------------------------------------------------------- #
# Build configuration. The vendored dependency wrappers select their prebuilt
# artifact by a "d" suffix when CMAKE_BUILD_TYPE == Debug, and TK_DEPS_DIR
# embeds CMAKE_BUILD_TYPE as a path component, so an empty value resolves to the
# wrong folder. Multi-config generators pick the active config per --build; this
# only fixes configure-time path/suffix resolution.
# --------------------------------------------------------------------------- #
# Pinning CMAKE_BUILD_TYPE is required for configure-time path resolution:
# TK_DEPS_DIR embeds the build type as a path component, and the vendored
# dependency wrappers use a "d" suffix in Debug. An empty value here
# resolves to the wrong folder (e.g. .../Windows/ instead of .../Windows/Debug/).
#
# Multi-config generators (Visual Studio) leave CMAKE_BUILD_TYPE empty at
# configure time; single-config generators (Ninja, Make) may also have it
# unset if the caller didn't pass -DCMAKE_BUILD_TYPE. Handle both.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(TK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")

# C++ standard + warnings (consistent with the engine build).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT MSVC)
    add_compile_options(-w)
endif()

# --------------------------------------------------------------------------- #
# Pre-built vendored dependency lookup root.
# Wrappers staged by build_dependencies.py as <dep>/<dep>Config.cmake probe for
# the prebuilt .so/.a here.
# --------------------------------------------------------------------------- #
set(TK_DEPS_DIR "${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")
set(CMAKE_PREFIX_PATH "${TK_DEPS_DIR}")

# find_package honours a cached <dep>_DIR above CMAKE_PREFIX_PATH, so when the
# build tree is reconfigured for a different CMAKE_BUILD_TYPE the stale
# SDL2_DIR / imgui_DIR entries from the previous config would point find_package
# at the wrong config's wrappers. Unset them so the wrapper for the CURRENT
# config is rediscovered via CMAKE_PREFIX_PATH.
foreach(_dep SDL2 SDL2main imgui assimp)
    unset(${_dep}_DIR CACHE)
endforeach()

# --------------------------------------------------------------------------- #
# Flat include path for the engine headers. ToolKit's sources live in many
# subfolders (Source, Render, Entities, ...) and its headers include each other
# unqualified (e.g. #include "Types.h"), so every subfolder must be on the path.
# --------------------------------------------------------------------------- #
file(GLOB_RECURSE _TK_HEADERS "${TOOLKIT_DIR}/ToolKit/*.h")
set(_TK_ENGINE_INCLUDE_DIRS "${TOOLKIT_DIR}" "${TOOLKIT_DIR}/ToolKit")
foreach(_h ${_TK_HEADERS})
    get_filename_component(_d "${_h}" DIRECTORY)
    list(APPEND _TK_ENGINE_INCLUDE_DIRS "${_d}")
endforeach()
list(REMOVE_DUPLICATES _TK_ENGINE_INCLUDE_DIRS)

# Header-only vendored deps referenced by ToolKit's public headers.
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

# --------------------------------------------------------------------------- #
# Engine shared library, per platform/configuration.
#
# The engine build writes one folder per config via GDTK_BIN_DIR=Bin$<CONFIG>
# (BinDebug/, BinRelease/, BinRelWithDebInfo/, BinMinSizeRel/). Each IMPORTED
# variant points at its own Bin<Config>/ with no cross-config mapping -- the
# consumer's active CMAKE_BUILD_TYPE selects the matching IMPORTED_LOCATION.
# Only Debug carries the "d" postfix (CMAKE_DEBUG_POSTFIX); the other three
# configs use the unsuffixed library name.
# --------------------------------------------------------------------------- #
set(_TK_CONFIGS Debug Release RelWithDebInfo MinSizeRel)
foreach(_cfg IN LISTS _TK_CONFIGS)
    if(_cfg STREQUAL "Debug")
        set(_sfx "d")
    else()
        set(_sfx "")
    endif()
    if(WIN32)
        set(_engine_lib_${_cfg}   "${TOOLKIT_DIR}/Bin${_cfg}/libToolKit${_sfx}.dll")
        set(_engine_implib_${_cfg} "${TOOLKIT_DIR}/Bin${_cfg}/ToolKit${_sfx}.lib")
    else()
        set(_engine_lib_${_cfg} "${TOOLKIT_DIR}/Bin${_cfg}/libToolKit${_sfx}.so")
    endif()
endforeach()

# At minimum Debug or Release MUST exist (the two configurations the engine
# is always expected to be built in). Warn if RelWithDebInfo or MinSizeRel
# are missing -- the consumer simply won't be able to use that config.
set(_tk_lib_missing TRUE)
foreach(_cfg IN LISTS _TK_CONFIGS)
    if(EXISTS "${_engine_lib_${_cfg}}")
        set(_tk_lib_missing FALSE)
        break()
    endif()
endforeach()
if(_tk_lib_missing)
    message(FATAL_ERROR
        "ToolKit engine library not found under '${TOOLKIT_DIR}/Bin<Config>' "
        "(looked in BinDebug/, BinRelease/, BinRelWithDebInfo/, BinMinSizeRel/ "
        "for libToolKit[d].so / libToolKit[d].dll). Build the engine first, e.g.:\n"
        "  python3 ${TOOLKIT_DIR}/BuildScripts/build_dependencies.py --configs ${CMAKE_BUILD_TYPE}\n"
        "  cmake -S ${TOOLKIT_DIR} -B ${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}\n"
        "  cmake --build ${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")
endif()

# --------------------------------------------------------------------------- #
# The IMPORTED engine target. Consumers link TK::ToolKit and inherit its public
# include dirs + compile definitions. Every config has its own
# IMPORTED_LOCATION -- no MAP_IMPORTED_CONFIG fallback.
# --------------------------------------------------------------------------- #
add_library(TK::ToolKit SHARED IMPORTED)
list(JOIN _TK_CONFIGS ";" _TK_CONFIGS_STR)
set_target_properties(TK::ToolKit PROPERTIES
    IMPORTED_CONFIGURATIONS "${_TK_CONFIGS_STR}"
    INTERFACE_INCLUDE_DIRECTORIES "${_TK_ENGINE_INCLUDE_DIRS}"
    INTERFACE_COMPILE_DEFINITIONS "TK_GL_3_0;$<$<CONFIG:Debug>:TK_DEBUG>;TK_DLL_IMPORT")

foreach(_cfg IN LISTS _TK_CONFIGS)
    string(TOUPPER "${_cfg}" _cfgu)
    set_target_properties(TK::ToolKit PROPERTIES
        IMPORTED_LOCATION_${_cfgu} "${_engine_lib_${_cfg}}")
    if(WIN32)
        set_target_properties(TK::ToolKit PROPERTIES
            IMPORTED_IMPLIB_${_cfgu} "${_engine_implib_${_cfg}}")
    endif()
endforeach()

if(WIN32)
    set_target_properties(TK::ToolKit PROPERTIES
        INTERFACE_LINK_LIBRARIES  "OpenGL32")
else()
    set_target_properties(TK::ToolKit PROPERTIES
        INTERFACE_LINK_LIBRARIES  "pthread;dl")
endif()
