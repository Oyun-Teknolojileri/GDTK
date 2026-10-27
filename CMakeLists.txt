# GDTK root CMake file.
#
# Builds the engine (ToolKit shared library), Workspace (static helper
# library under Modules/), and the Editor (executable). Vendored dependencies in
# Dependency/ are expected to be built ahead of time via
# BuildScripts/build_dependencies.py -- this file only consumes them
# through IMPORTED targets so the editor/utils build is fast and
# incremental.
#
# Supported target platforms: Linux (primary), Windows (best-effort).
# Emscripten (Web) builds only ToolKit and its runtime dependencies -- see the
# "Web (Emscripten)" blocks below. Android is out of scope for this file.
#
# The root file is consumed in two ways:
#   1. Standalone: `cmake -S . -B Intermediate/<Plat>/<Cfg>` (desktop default).
#   2. As a subproject: a web game project does
#      `add_subdirectory(${TOOLKIT_DIR} <build>/GDTK)` so the engine is compiled
#      by the same emscripten toolchain pass as the game. Every path in this
#      file therefore uses CMAKE_CURRENT_SOURCE_DIR / CMAKE_CURRENT_BINARY_DIR
#      instead of CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR.

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

# Emscripten is checked first: its CMake platform module reports UNIX=1 and
# WIN32=0, so the generic branches below would otherwise classify a web build
# as "Linux".
if(EMSCRIPTEN)
  set(TK_PLATFORM "Web")
elseif(WIN32)
  set(TK_PLATFORM "Windows")
elseif(APPLE)
  set(TK_PLATFORM "Mac")
else()
  set(TK_PLATFORM "Linux")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Vulkan backend -- opt-in. When ON, ToolKit/Render/Vulkan/*.cpp joins
# the build, vulkan-1 + shaderc get linked, and TK_VULKAN is defined
# so the engine's #ifdef-guarded Vulkan branches activate. Requires
# the system Vulkan loader + shaderc dev packages:
#   Fedora:    dnf install vulkan-loader-devel libshaderc-devel mesa-vulkan-drivers
#   Debian:    apt install libvulkan-dev libshaderc-dev mesa-vulkan-drivers
# Default OFF to match the legacy GL-only behaviour and to avoid
# hard-failing on minimal CI images that lack the Vulkan SDK.
option(TK_VULKAN "Build the Vulkan render backend into ToolKit" OFF)

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
#   Bin<Config>/                Final binaries the engine ships with
#                               (Editor, Launcher, libToolKitd.so, ...), one
#                               folder per build configuration -- BinDebug/,
#                               BinRelease/, ... -- so Debug and Release do
#                               not clobber each other. The config is baked
#                               in via $<CONFIG> (see GDTK_BIN_DIR below),
#                               which resolves per-build on both single-config
#                               (Ninja/Make) and multi-config (Visual Studio)
#                               generators. Bin<Config> is a direct child of
#                               the root (same as the old flat Bin/), so the
#                               editor's `../Resources/...` and `../Utils/...`
#                               relative climbs resolve exactly as before.
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
#   BinDebug/                        ← binaries the engine ships (per config)
#     ├── Editor
#     ├── Launcher
#     ├── Import
#     ├── Packer
#     ├── libToolKitd.so
#     └── libWorkspaced.a
#   BinRelease/                      ← (same set, Release-named libs)
#   Intermediate/Linux/Debug/         ← cmake state, *.o, link.txt, ...
#     ├── CMakeCache.txt, Makefile, CMakeFiles/, ...
#     ├── build-Editor/
#     ├── build-ToolKit/
#     ├── build-Workspace/
#     ├── build-Launcher/
#     ├── build-Import/
#     └── build-Packer/
#
# Web (Emscripten) builds are always a subproject of a game project, so their
# artifacts stay inside the caller's build tree instead of touching the engine
# source tree: <build>/GDTK/bin and <build>/GDTK/build-<module>.
if(EMSCRIPTEN)
  set(GDTK_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin")
  set(GDTK_INTERMEDIATE_DIR "${CMAKE_CURRENT_BINARY_DIR}/build")
else()
  set(GDTK_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Bin$<CONFIG>")
  set(GDTK_INTERMEDIATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")
endif()
# Default output directories for any targets added by add_subdirectory
# below. These directory-scope variables are plain text and cannot expand
# the $<CONFIG> generator expression that GDTK_BIN_DIR carries, so they
# are left pointing at a plain genex-free Bin/ fallback. Every real target
# (ToolKit, Editor, Launcher, Workspace) overrides RUNTIME/LIBRARY/
# ARCHIVE_OUTPUT_DIRECTORY per-target with ${GDTK_BIN_DIR} (which DOES
# expand $<CONFIG>), so this default is never actually consulted today --
# it exists only so a future target that forgets the override still lands
# somewhere sensible instead of under a literal "$<CONFIG>" folder.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}")

# All engine binaries and utility tools land in Bin<Config>/ (per-config).
# On Linux, RPATH ($ORIGIN) lets each binary find libToolKitd.so next to
# itself without LD_LIBRARY_PATH. The TK_DEPS_DIR RPATH entry covers the
# vendored shared dependencies (SDL2, imgui, assimp) under Dependency/.
#
# Each consumer declares `add_dependencies(... ToolKit)` in its own
# CMakeLists -- when ToolKit is rebuilt, Import / Packer / Editor /
# Launcher re-link to stay in sync with the new import library.
if(UNIX AND NOT EMSCRIPTEN)
  set(CMAKE_INSTALL_RPATH "$ORIGIN")
  set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

# --------------------------------------------------------------------------- #
# Dependency lookup                                                            #
# --------------------------------------------------------------------------- #
#
# Desktop builds consume dependencies that build_dependencies.py prebuilt into
# Dependency/Intermediate/<Plat>/<Cfg>. The web build has no prebuilt
# dependency tree (emscripten artifacts cannot be shared with the desktop
# ones), so it compiles the two runtime libraries it needs -- minizip and zstd
# -- from source in the same pass; SDL2 comes from emscripten's own port.

set(TK_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")

if(EMSCRIPTEN)
  add_subdirectory(Dependency ${CMAKE_CURRENT_BINARY_DIR}/deps)
elseif(NOT EXISTS "${TK_DEPS_DIR}")
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
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit/Source
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit/Resources
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit/UI
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit/Entities
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit/Components
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit/Render
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit/Render/RenderPass
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit/Render/PostProcessPass
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit/Render/OpenGL
  ${CMAKE_CURRENT_SOURCE_DIR}/ToolKit/Common
  ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Workspace
  ${CMAKE_CURRENT_SOURCE_DIR}/Modules/Workspace/Source
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/Source
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/Mods
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/UI
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/UI/Overlay
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/UI/Viewport
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/UI/Window
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/UI/View
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/Entities
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/Entities/Light
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/Entities/UI
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/Render
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/Render/Pass
  ${CMAKE_CURRENT_SOURCE_DIR}/Editor/Components
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/glm
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/glad
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/RapidXml
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/stb
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/miniaudio
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/poolSTL/include
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/tkimgui
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/tkimgui/imgui
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/SDL2/include
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/minizip-ng
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/minizip-ng/compat
)

# ---- Header-only INTERFACE libraries ---- #

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/glm)

add_library(glad INTERFACE)
target_include_directories(glad INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/glad)

add_library(RapidXml INTERFACE)
target_include_directories(RapidXml INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/RapidXml)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/stb)

add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/miniaudio)

add_library(poolstl INTERFACE)
target_include_directories(poolstl INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/poolSTL/include)

# tkimgui ships imgui + some GDTK-specific bindings in one static lib.
add_library(tkimgui INTERFACE)
target_include_directories(tkimgui INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/tkimgui
  ${CMAKE_CURRENT_SOURCE_DIR}/Dependency/tkimgui/imgui
)

# ---- Prebuild dependency imports ---- #
#
# Each prebuilt library is imported via a tiny per-dep wrapper config
# file (Dependency/Config/<dep>-config.cmake) that build_dependencies.py
# stages under <deps>/<dep>/. find_package's CONFIG mode probes
# <prefix>/<name>/<name>Config.cmake and
# <prefix>/<name>/<name>-config.cmake by default -- adding TK_DEPS_DIR
# to CMAKE_PREFIX_PATH makes those wrappers discoverable without any
# extra CMAKE_FIND_ROOT_PATH gymnastics.
#
# The wrappers declare each dep as an UNKNOWN IMPORTED target
# pointing at the prebuilt artifact. They hide every per-platform /
# per-config naming detail (libminizip<d>.a vs minizip<d>.lib vs
# zstd_static<d>.lib, ...) behind that boundary, so this file no
# longer has to maintain per-dep file-name tables or a Debug-suffix
# probing helper.
set(CMAKE_PREFIX_PATH "${TK_DEPS_DIR}")

# Required at configure time -- if a dep is missing the error message
# comes from the wrapper itself (it knows the per-platform file name
# the project was expecting to find). NO_DEFAULT_PATH keeps us from
# accidentally picking up a system-installed SDL2 / minizip with a
# mismatched version.
#
# The web build skips this block: its only runtime dependencies (minizip +
# zstd) are compiled from source by the Dependency add_subdirectory above and
# SDL2 comes from the emscripten port. The desktop-only shared libraries
# (assimp, imgui) and their consumers (Editor, Workspace, Import, Packer,
# Launcher) are not part of a web build.
if(NOT EMSCRIPTEN)
  find_package(SDL2     REQUIRED CONFIG)
  find_package(SDL2main REQUIRED CONFIG)
  find_package(minizip  REQUIRED CONFIG)
  find_package(zstd     REQUIRED CONFIG)
  find_package(assimp   REQUIRED CONFIG)
  find_package(imgui    REQUIRED CONFIG)
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

# Web builds ship only the engine: the editor, the workspace helper library and
# the command line tools are host executables that do not compile for
# emscripten (they need assimp, imgui and a real window).
if(NOT EMSCRIPTEN)
  add_subdirectory(Modules/Workspace ${GDTK_INTERMEDIATE_DIR}/build-Workspace)
  add_subdirectory(Editor    ${GDTK_INTERMEDIATE_DIR}/build-Editor)
  add_subdirectory(Utils/Launcher ${GDTK_INTERMEDIATE_DIR}/build-Launcher)
  add_subdirectory(Utils/Import   ${GDTK_INTERMEDIATE_DIR}/build-Import)
  add_subdirectory(Utils/Packer   ${GDTK_INTERMEDIATE_DIR}/build-Packer)
endif()

# --------------------------------------------------------------------------- #
# Install rules                                                               #
# --------------------------------------------------------------------------- #
# `cmake --install <build_dir> --prefix <root>` stages everything into
# Bin<Config>/, making it a self-contained runtime directory. After install,
# Bin<Config>/ carries every shared library the binaries need, so RPATH
# only requires $ORIGIN.
#
#   cmake --install <build_dir> --prefix ${CMAKE_CURRENT_SOURCE_DIR}
#
# build_gdtk.py runs this automatically after a successful build.
#
# Web builds skip the whole install block: there is nothing to stage next to a
# host executable, and the targets it references (Editor, Workspace, ...) do
# not exist there. The game project's own packaging step emits the .js/.wasm/
# .data bundle instead.

if(NOT EMSCRIPTEN)
  # GDTK targets -- install alongside the build output (idempotent since
  # RUNTIME_OUTPUT_DIRECTORY already points at Bin<Config>/).
  install(TARGETS ToolKit Editor Launcher Import Packer Workspace
    RUNTIME DESTINATION "Bin$<CONFIG>"
    LIBRARY DESTINATION "Bin$<CONFIG>"
    ARCHIVE DESTINATION "Bin$<CONFIG>"
  )

  # Vendored shared runtime dependencies.
  #
  # install(DIRECTORY ...) and install(FILES ...) both resolve symlinks
  # during install, which breaks the SONAME chain (libSDL2-2.0d.so ->
  # .so.0 -> .so.0.3200.8). install(CODE ...) lets us recreate the
  # symlink tree so the dynamic loader can find libraries by SONAME in
  # the self-contained Bin<Config>/ directory.
  if(UNIX)
    install(CODE "
      set(_SRC  \"${TK_DEPS_DIR}\")
      set(_DEST \"\${CMAKE_INSTALL_PREFIX}/Bin$<CONFIG>\")
      file(MAKE_DIRECTORY \"\${_DEST}\")
      foreach(_pat \"libSDL2-*\" \"libimgui*\" \"libassimp*\")
        file(GLOB _files \"\${_SRC}/\${_pat}\")
        foreach(_f \${_files})
          get_filename_component(_name \"\${_f}\" NAME)
          if(IS_SYMLINK \"\${_f}\")
            file(READ_SYMLINK \"\${_f}\" _target)
            execute_process(COMMAND ln -sf \"\${_target}\" \"\${_DEST}/\${_name}\")
          else()
            execute_process(COMMAND \${CMAKE_COMMAND} -E copy_if_different
              \"\${_f}\" \"\${_DEST}/\")
          endif()
        endforeach()
      endforeach()
    ")
  elseif(WIN32)
    install(CODE "
      set(_SRC  \"${TK_DEPS_DIR}\")
      set(_DEST \"\${CMAKE_INSTALL_PREFIX}/Bin$<CONFIG>\")
      file(MAKE_DIRECTORY \"\${_DEST}\")
      foreach(_pat \"SDL2*.dll\" \"imgui*.dll\" \"assimp*.dll\")
        file(GLOB _files \"\${_SRC}/\${_pat}\")
        foreach(_f \${_files})
          get_filename_component(_name \"\${_f}\" NAME)
          execute_process(COMMAND \${CMAKE_COMMAND} -E copy_if_different
            \"\${_f}\" \"\${_DEST}/\")
        endforeach()
      endforeach()
    ")
  endif()
endif()
