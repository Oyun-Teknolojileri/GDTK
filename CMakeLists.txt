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
# C++ standard                                                                 #
# --------------------------------------------------------------------------- #
#
# AGENTS.md declares C++17, and every GDTK target uses
# `target_compile_features(... cxx_std_17)`. That feature requirement alone
# does NOT add `-std=c++17` to the compile line -- it only expresses that
# the target needs C++17 to compile. On GCC 16 (whose default `g++` is
# C++20) the absence of an explicit `-std=` flag means we silently compile
# in C++20 mode, which is what surfaces warnings like:
#   - `char*` -> `const char8_t*` from utf8.h (a 3rd-party header that
#     picks the C++20 `using utf8_int8_t = char8_t;` branch)
#   - `reinterpret_cast` not a constant expression (Hash.h)
#   - implicit `[=]` capture of `this` deprecated (PublishManager, Thumbnail)
#
# Pinning CMAKE_CXX_STANDARD here forces `-std=c++17` on the command line
# for every target, including third-party vendored headers that the engine
# pulls in. This is consistent with the declared C++17 standard and lets
# us drop the `-fpermissive` workaround (see ToolKit/CMakeLists.txt) if
# we choose to in a follow-up.
set(CMAKE_CXX_STANDARD 17 CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Build artifacts split across two roots:
#
#   Bin/                        Final binaries the engine ships with
#                               (Editor, Launcher, libToolKitd.so, ...).
#                               The user-facing runtime CWD expectation is
#                               "Bin/ holds the engine", so the editor's
#                               `../Resources/...` and other relative
#                               paths resolve from there. Removing Bin/
#                               breaks runtime loading.
#
#   Intermediate/<Plat>/<Cfg>/  Everything CMake generates per-build:
#                               CMakeCache.txt, Makefile, *.cmake_install,
#                               per-module build-<module>/CMakeFiles/
#                               with all the *.o / *.d / link.txt state.
#                               The Dependency tree lives under
#                               Intermediate/ as well, so `rm -rf
#                               Intermediate/` gives a clean slate.
#
# Without an explicit binary_dir in add_subdirectory(), CMake drops
# CMakeFiles/ + Makefile + cmake_install.cmake inside the source folder
# (ToolKit/CMakeFiles/, etc.). Those files clutter the source tree and
# are easy to commit by accident, so we always set the binary_dir.
# Per-module subdirs (build-Editor/, build-ToolKit/, ...) keep the object
# files separate, so a clean of one module doesn't churn the timestamps
# of the others.
#
# Final layout:
#   Bin/                              ← binaries the engine ships
#     ├── Editor
#     ├── Launcher
#     ├── libToolKitd.so
#     └── libWorkspace.a
#   Intermediate/Linux/Debug/         ← cmake state, *.o, link.txt, ...
#     ├── CMakeCache.txt, Makefile, CMakeFiles/, ...
#     ├── build-Editor/
#     ├── build-ToolKit/
#     ├── build-Workspace/
#     ├── build-Launcher/
#     ├── build-Import/
#     └── build-Packer/
set(GDTK_BIN_DIR "${CMAKE_SOURCE_DIR}/Bin")
set(GDTK_INTERMEDIATE_DIR "${CMAKE_SOURCE_DIR}/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}")

# Utils/Import/ and Utils/Packer/ are the *published* output locations for
# the Import and Packer CLI tools. Everything else (Editor, Launcher,
# libToolKitd.so, libWorkspace.a) lands in Bin/. Import and Packer
# override RUNTIME_OUTPUT_DIRECTORY to point at Utils/... (see
# Import/CMakeLists.txt, Packer/CMakeLists.txt). Their POST_BUILD steps
# then copy ToolKit (and assimp, for Import) next to the binaries so
# $ORIGIN-based RPATH resolves without LD_LIBRARY_PATH.
#
# add_dependencies(... ToolKit) on those targets forces a relink whenever
# ToolKit changes, which re-runs the POST_BUILD copy. Net effect: editing
# engine code refreshes Utils/Import/ and Utils/Packer/ on every build.
set(GDTK_UTILS_IMPORT_DIR "${CMAKE_SOURCE_DIR}/Utils/Import")
set(GDTK_UTILS_PACKER_DIR "${CMAKE_SOURCE_DIR}/Utils/Packer")

# Create the dirs eagerly so IDEs (VS, etc.) see them before the first
# build. POST_BUILD copy steps also `file(COPY ...)` lazily but having
# the dirs upfront avoids the "binary runs but its deps aren't there
# yet" footgun on a clean checkout.
file(MAKE_DIRECTORY "${GDTK_UTILS_IMPORT_DIR}")
file(MAKE_DIRECTORY "${GDTK_UTILS_PACKER_DIR}")

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
#
# ToolKit's sources now live in subfolders (Source, Resources, UI,
# Entities, Components, Render, Render/RenderPass, Render/PostProcessPass,
# Common) so unqualified `#include "Material.h"` etc. from any
# downstream target needs every subfolder on the include path.
set(GDTK_INCLUDE_DIRS
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/ToolKit
  ${CMAKE_SOURCE_DIR}/ToolKit/Source
  ${CMAKE_SOURCE_DIR}/ToolKit/Resources
  ${CMAKE_SOURCE_DIR}/ToolKit/UI
  ${CMAKE_SOURCE_DIR}/ToolKit/Entities
  ${CMAKE_SOURCE_DIR}/ToolKit/Components
  ${CMAKE_SOURCE_DIR}/ToolKit/Render
  ${CMAKE_SOURCE_DIR}/ToolKit/Render/RenderPass
  ${CMAKE_SOURCE_DIR}/ToolKit/Render/PostProcessPass
  ${CMAKE_SOURCE_DIR}/ToolKit/Common
  ${CMAKE_SOURCE_DIR}/Workspace
  ${CMAKE_SOURCE_DIR}/Workspace/Source
  ${CMAKE_SOURCE_DIR}/Editor
  ${CMAKE_SOURCE_DIR}/Editor/Source
  ${CMAKE_SOURCE_DIR}/Editor/Mods
  ${CMAKE_SOURCE_DIR}/Editor/UI
  ${CMAKE_SOURCE_DIR}/Editor/UI/Overlay
  ${CMAKE_SOURCE_DIR}/Editor/UI/Viewport
  ${CMAKE_SOURCE_DIR}/Editor/UI/Window
  ${CMAKE_SOURCE_DIR}/Editor/UI/View
  ${CMAKE_SOURCE_DIR}/Editor/Entities
  ${CMAKE_SOURCE_DIR}/Editor/Entities/Light
  ${CMAKE_SOURCE_DIR}/Editor/Entities/UI
  ${CMAKE_SOURCE_DIR}/Editor/Render
  ${CMAKE_SOURCE_DIR}/Editor/Render/Pass
  ${CMAKE_SOURCE_DIR}/Editor/Components
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

# Every module's CMakeFiles/, *.o, *.cmake_install, etc. go to a per-
# module subdir under Intermediate/. The actual binaries (Editor,
# Launcher, libToolKitd.so, ...) land directly under Intermediate/
# because of the CMAKE_*_OUTPUT_DIRECTORY settings above.
#
# Specifying the second argument to add_subdirectory() is what stops
# CMake from dropping build artifacts into the source folder. The
# pattern matches Dependency/CMakeLists.txt's use of
# `add_subdirectory(<src> Intermediate/${TK_PLATFORM}/<lib>)`.
#
# The per-module subdirs are named "build-<module>" rather than just
# "<module>" so the build dir doesn't collide with the final binary's
# path: when the binary dir equals the target's output path, CMake
# strips the filename from the link rule and `ld` ends up trying to
# write to "." (a directory). The `build-` prefix is purely cosmetic
# for organizing intermediate state; it does not affect the build.
add_subdirectory(ToolKit   ${GDTK_INTERMEDIATE_DIR}/build-ToolKit)
add_subdirectory(Workspace ${GDTK_INTERMEDIATE_DIR}/build-Workspace)
add_subdirectory(Editor    ${GDTK_INTERMEDIATE_DIR}/build-Editor)
add_subdirectory(Launcher  ${GDTK_INTERMEDIATE_DIR}/build-Launcher)
add_subdirectory(Import    ${GDTK_INTERMEDIATE_DIR}/build-Import)
add_subdirectory(Packer    ${GDTK_INTERMEDIATE_DIR}/build-Packer)
