# GDTK root CMake file.
#
# Builds the engine (ToolKit shared library), Workspace (static helper
# library), and the Editor (executable). Vendored dependencies in
# Dependency/ are expected to be built ahead of time via
# BuildScripts/build_dependencies.py -- this file only consumes them
# through IMPORTED targets so the editor/utils build is fast and
# incremental.
#
# Supported target platforms: Linux (primary), Windows (best-effort).
# Emscripten / Android are out of scope for this phase.

cmake_minimum_required(VERSION 3.16)
project(GDTK LANGUAGES C CXX)

# --------------------------------------------------------------------------- #
# Platform detection                                                          #
# --------------------------------------------------------------------------- #

if(WIN32)
  set(TK_PLATFORM "Windows")
elseif(APPLE)
  set(TK_PLATFORM "Mac")
else()
  set(TK_PLATFORM "Linux")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

message(STATUS "GDTK: building for ${TK_PLATFORM} (config: ${CMAKE_BUILD_TYPE})")

# --------------------------------------------------------------------------- #
# Output layout                                                               #
# --------------------------------------------------------------------------- #

# Everything lands in Bin/. Matches the legacy ToolKit.sln convention so
# downstream tooling (debugger attachments, packaging, etc.) keeps working.
set(GDTK_BIN_DIR "${CMAKE_SOURCE_DIR}/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}")

# Linux: editor/utils find ToolKit.so next to themselves, no LD_LIBRARY_PATH.
if(UNIX)
  set(CMAKE_INSTALL_RPATH "$ORIGIN")
  set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

# --------------------------------------------------------------------------- #
# Dependency lookup                                                            #
# --------------------------------------------------------------------------- #

set(TK_DEPS_DIR "${CMAKE_SOURCE_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")

if(NOT EXISTS "${TK_DEPS_DIR}")
  message(FATAL_ERROR
    "GDTK dependencies not found at:\n"
    "  ${TK_DEPS_DIR}\n"
    "Build them first:\n"
    "  python3 BuildScripts/build_dependencies.py --configs ${CMAKE_BUILD_TYPE}")
endif()

# Helper: import a pre-built shared/static library as an IMPORTED target.
function(tk_add_imported name lib_path)
  if(NOT EXISTS "${lib_path}")
    message(FATAL_ERROR "Expected dependency library not found: ${lib_path}")
  endif()
  add_library(${name} UNKNOWN IMPORTED)
  set_target_properties(${name} PROPERTIES
    IMPORTED_LOCATION "${lib_path}"
  )
endfunction()

# Common include path bundle used by every target.
set(GDTK_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/ToolKit
  ${CMAKE_SOURCE_DIR}/Workspace
  ${CMAKE_SOURCE_DIR}/Editor
  ${CMAKE_SOURCE_DIR}/Dependency
  ${CMAKE_SOURCE_DIR}/Dependency/glm
  ${CMAKE_SOURCE_DIR}/Dependency/glad
  ${CMAKE_SOURCE_DIR}/Dependency/RapidXml
  ${CMAKE_SOURCE_DIR}/Dependency/stb
  ${CMAKE_SOURCE_DIR}/Dependency/miniaudio
  ${CMAKE_SOURCE_DIR}/Dependency/poolSTL/include
  ${CMAKE_SOURCE_DIR}/Dependency/tkimgui
  ${CMAKE_SOURCE_DIR}/Dependency/tkimgui/imgui
  ${CMAKE_SOURCE_DIR}/Dependency/SDL2/include
  ${CMAKE_SOURCE_DIR}/Dependency/minizip-ng
  ${CMAKE_SOURCE_DIR}/Dependency/minizip-ng/compat
)

# ---- Header-only INTERFACE libraries ---- #

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_SOURCE_DIR}/Dependency/glm)

add_library(glad INTERFACE)
target_include_directories(glad INTERFACE ${CMAKE_SOURCE_DIR}/Dependency/glad)

add_library(RapidXml INTERFACE)
target_include_directories(RapidXml INTERFACE ${CMAKE_SOURCE_DIR}/Dependency/RapidXml)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_SOURCE_DIR}/Dependency/stb)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_SOURCE_DIR}/Dependency/miniaudio)

add_library(poolstl INTERFACE)
target_include_directories(poolstl INTERFACE ${CMAKE_SOURCE_DIR}/Dependency/poolSTL/include)

# tkimgui ships imgui + some GDTK-specific bindings in one static lib.
add_library(tkimgui INTERFACE)
target_include_directories(tkimgui INTERFACE
  ${CMAKE_SOURCE_DIR}/Dependency/tkimgui
  ${CMAKE_SOURCE_DIR}/Dependency/tkimgui/imgui
)

# ---- Binary IMPORTED targets ---- #

# Generator expressions are not allowed in IMPORTED_LOCATION, so pick the
# concrete file name per build type. Single-config generators only
# (Debug/Release); multi-config (VS / Ninja Multi-Config) is Phase 3+.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_d "d")
else()
  set(_d "")
endif()

tk_add_imported(SDL2 ${TK_DEPS_DIR}/libSDL2-2.0${_d}.so)
tk_add_imported(SDL2main ${TK_DEPS_DIR}/libSDL2main${_d}.a)
tk_add_imported(minizip ${TK_DEPS_DIR}/libminizip${_d}.a)
if(UNIX)
  tk_add_imported(zstd ${TK_DEPS_DIR}/libzstd${_d}.a)
elseif(WIN32)
  tk_add_imported(zstd ${TK_DEPS_DIR}/zstd_static${_d}.lib)
endif()
if(UNIX)
  tk_add_imported(assimp ${TK_DEPS_DIR}/libassimp${_d}.so)
endif()

# tkimgui is built as a shared library on Linux (Dependency/CMakeLists.txt
# forces BUILD_SHARED_LIBS=ON for it). On Windows the name drops the
# "lib" prefix and uses .lib. We only target Linux in this phase.
tk_add_imported(imgui ${TK_DEPS_DIR}/libimgui${_d}.so)

# --------------------------------------------------------------------------- #
# Engine + tools                                                              #
# --------------------------------------------------------------------------- #

add_subdirectory(ToolKit)
add_subdirectory(Workspace)
add_subdirectory(Editor)
