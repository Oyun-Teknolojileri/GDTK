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

# Multi-config generators (Visual Studio) leave CMAKE_BUILD_TYPE
# empty at configure time, which makes the dependency lookup
# (TK_DEPS_DIR = ".../<Plat>/<empty>") look in the wrong folder.
# Pin CMAKE_BUILD_TYPE to "Debug" on those generators so the
# configure-time dep import finds the same artifacts the user
# builds with --config Debug. The active configuration is still
# selected per-build via --config; the value below is only used
# for configure-time path resolution, not for compile flags.
if(NOT CMAKE_BUILD_TYPE AND CMAKE_CONFIGURATION_TYPES)
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
# Default output directories for any targets added by add_subdirectory
# below. Multi-config generators (Visual Studio, Ninja Multi-Config)
# append the active configuration as a final path component -- that
# would put Editor.exe at Bin/Debug/Editor.exe instead of Bin/Editor.exe.
# CMAKE_*_OUTPUT_DIRECTORY is plain-text only, so the per-target
# override below is the actual mechanism that strips the suffix; this
# default still acts as a fallback for any target that does not have
# an explicit override.
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
  ${CMAKE_SOURCE_DIR}/ToolKit/Render/OpenGL
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

# Generator expressions are not allowed in IMPORTED_LOCATION, so pick
# the concrete file name per platform and per build type. Windows
# uses CMake's default shared/static naming (foo.lib for static /
# import, foo.dll for runtime), while Linux uses the libfoo.so /
# libfoo.a convention. The Debug suffix (set via
# CMAKE_DEBUG_POSTFIX="d" in Dependency/CMakeLists.txt) lands
# before the extension.
#
# Multi-config generators (Visual Studio) leave CMAKE_BUILD_TYPE
# empty at configure time -- the active config is selected per-build
# via --config. In that case we cannot know which variant to link
# against at configure time, so we look for the Release variant
# first and fall back to Debug if it is missing. The link artifact
# is what we import here; the runtime .dll staging is handled
# separately by CopyDependencies per build configuration.
#
# Note: tk_add_imported() FATAL_ERRORs when the file is missing.
# If a target below is unwanted on a platform, wrap the import in
# an if(UNIX)/if(WIN32) block rather than letting the missing file
# blow up configure.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_d_suffix "d")
  set(_d_suffix_alt "")
else()
  # Multi-config generators (VS) reach this branch with
  # CMAKE_BUILD_TYPE empty; we don't know the active config at
  # configure time, so probe both suffixes and prefer the Release
  # variant if it exists.
  set(_d_suffix "")
  set(_d_suffix_alt "d")
endif()

# Build the candidate path list for a dep, in priority order. The
# caller supplies per-platform file names WITH extensions, in
# Release form first ("libSDL2-2.0.so") and Debug form second
# ("libSDL2-2.0d.so"). We probe both the active-config directory
# (TK_DEPS_DIR) AND a Debug-fallback directory (<deps>/Debug) so
# multi-config generators that leave CMAKE_BUILD_TYPE empty at
# configure time still find the artifacts the dependency script
# produced under <Platform>/<Config>/.
#
# We strip a trailing slash from the joined path components so a
# trailing separator (which CMake can leave behind when
# CMAKE_BUILD_TYPE is empty on multi-config generators) does not
# produce a doubled separator in the joined path. CMake's EXISTS
# check returns FALSE for "<dir-with-trailing-slash>/<file>"
# on Windows, which is a known sharp edge.
function(tk_dep_candidates out_var)
  string(REGEX REPLACE "/+$" "" _tk_deps_dir "${TK_DEPS_DIR}")
  # Strip the last path segment by hand. We cannot use
  # get_filename_component(... DIRECTORY) here -- it returns the
  # directory of a *file* path, dropping the last segment of a
  # directory path. We need to walk the string ourselves so
  # "<deps>/Windows/Debug" becomes "<deps>/Windows".
  string(REGEX REPLACE "[^/]+$" "" _tk_deps_parent "${_tk_deps_dir}")
  set(_candidates "")
  # Priority 1: TK_DEPS_DIR (active config) for each Release then Debug name.
  foreach(_arg IN LISTS ARGN)
    list(APPEND _candidates "${_tk_deps_dir}/${_arg}")
  endforeach()
  # Priority 2: <parent>/Debug/ (Debug fallback for multi-config users
  # whose CMAKE_BUILD_TYPE was empty at configure time). We always
  # probe this so a Debug-only build of the dependency tree still
  # finds its artifacts.
  foreach(_arg IN LISTS ARGN)
    list(APPEND _candidates "${_tk_deps_parent}Debug/${_arg}")
  endforeach()
  set(${out_var} "${_candidates}" PARENT_SCOPE)
endfunction()

# Import the first existing candidate for a dep. Emits a clear
# error listing what we looked for if nothing matches.
function(tk_add_imported_resolved name)
  tk_dep_candidates(_candidates ${ARGN})
  foreach(_path IN LISTS _candidates)
    if(EXISTS "${_path}")
      tk_add_imported(${name} "${_path}")
      return()
    endif()
  endforeach()
  set(_msg "Expected dependency library not found for ${name}. Looked at:")
  foreach(_path IN LISTS _candidates)
    set(_msg "${_msg}\n  ${_path}")
  endforeach()
  message(FATAL_ERROR "${_msg}")
endfunction()

# --- SDL2 (shared) ---
# Linux: SONAME-encoded filename libSDL2-2.0.so (Release) /
#        libSDL2-2.0d.so (Debug).
# Windows: import lib SDL2.lib (Release) / SDL2d.lib (Debug).
#        Runtime DLL lives next to it (SDL2.dll / SDL2d.dll) but
#        is not what we link against.
if(UNIX)
  tk_add_imported_resolved(SDL2
    "libSDL2-2.0.so"   # Release
    "libSDL2-2.0d.so"  # Debug
  )
elseif(WIN32)
  tk_add_imported_resolved(SDL2
    "SDL2.lib"         # Release
    "SDL2d.lib"        # Debug
  )
endif()

# --- SDL2main (static) ---
# Linux: libSDL2main.a / libSDL2maind.a.
# Windows: SDL2main.lib / SDL2maind.lib (no "lib" prefix, .lib vs .a).
if(UNIX)
  tk_add_imported_resolved(SDL2main
    "libSDL2main.a"
    "libSDL2maind.a"
  )
elseif(WIN32)
  tk_add_imported_resolved(SDL2main
    "SDL2main.lib"
    "SDL2maind.lib"
  )
endif()

# --- minizip (static) ---
# Linux: libminizip.a / libminizipd.a.
# Windows: minizip.lib / minizipd.lib.
if(UNIX)
  tk_add_imported_resolved(minizip
    "libminizip.a"
    "libminizipd.a"
  )
elseif(WIN32)
  tk_add_imported_resolved(minizip
    "minizip.lib"
    "minizipd.lib"
  )
endif()

# --- zstd (static) ---
# Linux: libzstd.a / libzstdd.a.
# Windows: zstd's upstream CMake target names the static library
# zstd_static.lib / zstd_staticd.lib (the "static" infix is zstd's
# quirk, not a GDTK convention).
if(UNIX)
  tk_add_imported_resolved(zstd
    "libzstd.a"
    "libzstdd.a"
  )
elseif(WIN32)
  tk_add_imported_resolved(zstd
    "zstd_static.lib"
    "zstd_staticd.lib"
  )
endif()

# --- assimp (shared) ---
# Only built when the dependency script runs without --skip-assimp.
# Linux: libassimp.so / libassimpd.so.
# Windows: assimp.lib / assimpd.lib (import) + assimp.dll / assimpd.dll.
if(UNIX)
  tk_add_imported_resolved(assimp
    "libassimp.so"
    "libassimpd.so"
  )
elseif(WIN32)
  tk_add_imported_resolved(assimp
    "assimp.lib"
    "assimpd.lib"
  )
endif()

# --- imgui / tkimgui (shared) ---
# Dependency/CMakeLists.txt forces BUILD_SHARED_LIBS=ON for tkimgui.
# Linux: libimgui.so / libimguid.so.
# Windows: imgui.lib / imguid.lib + imgui.dll / imguid.dll.
if(UNIX)
  tk_add_imported_resolved(imgui
    "libimgui.so"
    "libimguid.so"
  )
elseif(WIN32)
  tk_add_imported_resolved(imgui
    "imgui.lib"
    "imguid.lib"
  )
endif()

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
