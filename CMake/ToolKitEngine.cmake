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
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

if(CMAKE_BUILD_TYPE)
    set(TK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
    set(TK_BUILD_TYPE "$<CONFIG>")
endif()

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
list(APPEND CMAKE_PREFIX_PATH "${TK_DEPS_DIR}")

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
# --------------------------------------------------------------------------- #
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
        "(looked for libToolKit[d].so / ToolKit[d].dll). Build the engine first, e.g.:\n"
        "  python3 ${TOOLKIT_DIR}/BuildScripts/build_dependencies.py --configs ${CMAKE_BUILD_TYPE}\n"
        "  cmake -S ${TOOLKIT_DIR} -B ${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}\n"
        "  cmake --build ${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")
endif()

# --------------------------------------------------------------------------- #
# The IMPORTED engine target. Consumers link TK::ToolKit and inherit its public
# include dirs + compile definitions.
# --------------------------------------------------------------------------- #
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
