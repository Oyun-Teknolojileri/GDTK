# Import -- GDTK's offline 3D asset converter.
# Linux binary: Utils/Import/Import. Windows binary: Utils/Import/Import.exe.
# ToolKit + assimp are staged next to the binary at build time (see the
# POST_BUILD block at the bottom of this file).
#
# Single-file CLI tool. Takes one or more source files (via the assimp
# importer) and writes them out as ToolKit-native scene / mesh / material
# assets. Headless: the render system is initialized just to satisfy
# EngineSettings' GPU factory hooks, but no window or swap is created.
#
# This target is the only consumer of assimp's headers in the build --
# Dependency/assimp/include (and the generated config.h under
# Intermediate/.../assimp/include) is therefore added PRIVATE on this
# target below, not in the global GDTK_INCLUDE_DIRS. Packer links assimp
# for transitive runtime resolution but does not include its headers.

file(GLOB IMPORT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(Import ${IMPORT_SOURCES})

target_include_directories(Import PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GDTK_INCLUDE_DIRS}
  # assimp headers are only used by Import/import.cpp, not by any other
  # GDTK target. Keep these PRIVATE here instead of in the global
  # GDTK_INCLUDE_DIRS so the other targets don't grow a dependency on
  # assimp's include layout they don't need.
  #
  # The second path is the build-time generated config.h -- assimp's
  # public defs.h (upstream) does `#include <assimp/config.h>` so the
  # generated header from the Intermediate build dir is required, not
  # optional.
  ${CMAKE_SOURCE_DIR}/Dependency/assimp/include
  ${CMAKE_SOURCE_DIR}/Dependency/Intermediate/${TK_PLATFORM}/assimp/include
)

# The tool imports ToolKit symbols from libToolKit.so, so TK_DLL_IMPORT
# is set. It also instantiates a headless OpenGL context (m_renderSys->
# InitGraphics) just to satisfy engine resource factories, so TK_GL_3_0
# must be defined too.
target_compile_definitions(Import PRIVATE
  TK_DLL_IMPORT
  TK_GL_3_0
  $<$<CONFIG:Debug>:TK_DEBUG>
)

target_compile_features(Import PRIVATE cxx_std_17)

# Precompiled header. ToolKit's stdafx.h is shared with the engine;
# inject it into every translation unit so the symbols Pass.h,
# Object.h, etc. transitively bring in are always visible.
target_precompile_headers(Import PRIVATE
  ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
)

# Same GCC 16 workarounds as the rest of the engine. Import pulls in
# Pass.h (via Workspace), so -Wno-changes-meaning is required. utf8.h
# triggers the C++20 `char*` -> `const char8_t*` error -- -fpermissive
# silences it without rewriting the UTF-8 helpers.
# All flags in this block are GCC/Clang-only; MSVC rejects them
# with D8021. Skip the block on MSVC builds.
if(NOT MSVC)
  target_compile_options(Import PRIVATE
    -Wno-changes-meaning
    -Wno-delete-incomplete
    -fpermissive
  )
endif()

# Link: ToolKit for the engine, Workspace for the project file helpers,
# assimp for the actual import work.
#
# - SDL2 / SDL2main: the headless render-system init still spins up a
#   GL context (m_renderSys->InitGraphics), so the SDL2 driver stack
#   is needed; SDL2main is the static entry-point shim.
# - minizip / zstd are NOT listed here -- ToolKit embeds them as
#   PRIVATE static libs and Import only goes through ToolKit's file API.
# - imgui / tkimgui are NOT listed -- Import is a headless CLI tool,
#   no UI surfaces are created.
target_link_libraries(Import PRIVATE
  ToolKit
  Workspace
  assimp
  SDL2
  SDL2main
  glm
  glad
  RapidXml
  stb
  miniaudio
  poolstl
  tkimgui
)

if(UNIX AND NOT APPLE)
  find_package(OpenGL REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLX REQUIRED glx)

  target_link_libraries(Import PRIVATE
    OpenGL::GL
    ${GLX_LIBRARIES}
    pthread
    dl
  )
elseif(WIN32)
  target_link_libraries(Import PRIVATE OpenGL32)
endif()

# ---------------------------------------------------------------------------
# Output: Utils/Import/
#
# Import is a standalone CLI tool, published to Utils/Import/ so users
# can run it from a fresh checkout. It is NOT split per config (its Utils/
# output dir stays flat -- last-built config wins). RPATH
# ($ORIGIN/../../Bin$<CONFIG>) points the binary at the canonical
# libToolKitd.so under the matching Bin<Config>/ on Linux. On Windows the
# DLL is staged next to the executable by ToolKit's own POST_BUILD step
# (ToolKit/CMakeLists.txt), because the Windows loader searches the exe
# directory before RPATH.
#
# add_dependencies ensures Import re-links whenever ToolKit changes,
# so the import library stays in sync with the DLL exports.
#
# Layout after a build:
#   Bin<Config>/          libToolKitd.so / libToolKitd.dll (canonical, per cfg)
#   Utils/Import/         Import + libToolKitd.dll         (Windows, flat)
#   Dependency/.../<cfg>/ libSDL2-2.0d.so, libimguid.so, libassimpd.so
# ---------------------------------------------------------------------------
add_dependencies(Import ToolKit)
set_target_properties(Import PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_UTILS_IMPORT_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_UTILS_IMPORT_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_UTILS_IMPORT_DIR}/$<0:>"
)

# RPATH strategy (Linux + Windows both honour $ORIGIN via CMake 3.16+):
#   - $ORIGIN/../../Bin$<CONFIG> -> Utils/Import/Import finds libToolKitd.so /
#                                ToolKitd.dll under the canonical Bin<Config>/.
#   - ${TK_DEPS_DIR}         -> absolute path to Dependency/Intermediate/
#                                <Plat>/<Cfg>/, where SDL2 / imgui / assimp
#                                live. The Toolkit build links the vendored
#                                static libs (minizip / zstd / SDL2main) into
#                                libToolKit.so, so they do NOT need to be
#                                findable here; only the *shared* deps do.
# On Linux this is encoded into the ELF DT_RUNPATH; on Windows CMake
# converts it into the DLL search path in the PE header. Both platforms
# resolve $ORIGIN to the directory containing the executable at runtime,
# so the binary can be invoked from any cwd without LD_LIBRARY_PATH or
# %PATH% tweaks.
if(UNIX)
  set_target_properties(Import PROPERTIES
    BUILD_RPATH   "$ORIGIN/../../Bin$<CONFIG>:${TK_DEPS_DIR}"
    INSTALL_RPATH "$ORIGIN/../../Bin$<CONFIG>:${TK_DEPS_DIR}"
  )
elseif(WIN32)
  set_target_properties(Import PROPERTIES
    BUILD_RPATH   "$ORIGIN/../../Bin$<CONFIG>;${TK_DEPS_DIR}"
    INSTALL_RPATH "$ORIGIN/../../Bin$<CONFIG>;${TK_DEPS_DIR}"
  )
endif()
