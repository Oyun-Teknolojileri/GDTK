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

# Same GCC 16 workarounds as the rest of the engine. Import pulls in
# Pass.h (via Workspace), so -Wno-changes-meaning is required. utf8.h
# triggers the C++20 `char*` -> `const char8_t*` error -- -fpermissive
# silences it without rewriting the UTF-8 helpers.
target_compile_options(Import PRIVATE
  -Wno-changes-meaning
  -Wno-delete-incomplete
  -include ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
  -fpermissive
)

# Link: ToolKit for the engine, Workspace for the project file helpers,
# assimp for the actual import work. SDL2 + the OpenGL stack are needed
# because the headless render-system init still spins up a GL context.
target_link_libraries(Import PRIVATE
  ToolKit
  Workspace
  assimp
  SDL2
  SDL2main
  minizip
  zstd
  glm
  glad
  RapidXml
  stb
  miniaudio
  poolstl
  tkimgui
)

if(UNIX)
  target_link_libraries(Import PRIVATE
    -l:libGL.so.1
    -l:libGLX.so.0
    pthread
    dl
  )
elseif(WIN32)
  target_link_libraries(Import PRIVATE OpenGL32)
endif()

# ---------------------------------------------------------------------------
# Output: Utils/Import/
#
# Import ships as a standalone CLI tool -- users run it from a fresh
# checkout (CI, packagers) without the full Bin/ tree available, so we
# publish it to Utils/Import/ alongside its runtime deps (ToolKit,
# assimp). POST_BUILD below copies those libs in. The Editor/Launcher
# keep using Bin/ToolKit.so unchanged.
#
# - add_dependencies(Import ToolKit) is the trigger: when any ToolKit
#   source changes, ToolKit is rebuilt AND Import is relinked (because
#   ToolKit is on Import's link line). The relink re-runs the POST_BUILD
#   copy, so Utils/Import/ToolKit.so stays in sync with Bin/ToolKit.so.
# - The POST_BUILD also runs unconditionally on every Import build,
#   so a clean build always populates Utils/Import/.
# ---------------------------------------------------------------------------
set_target_properties(Import PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_UTILS_IMPORT_DIR}"
)
add_dependencies(Import ToolKit)

# Generator expressions evaluate at build time, so single-config (Ninja,
# Unix Makefiles, NMake, MinGW) and multi-config (Visual Studio) both
# get the right per-config file -- no CMAKE_BUILD_TYPE branching needed.
#   $<TARGET_FILE:ToolKit>       -> the actual libToolKitd.so / ToolKitd.dll
#                                  path in the current build config.
#   $<TARGET_FILE:assimp>        -> the IMPORTED_LOCATION of the assimp
#                                  target, resolved per-config.
add_custom_command(TARGET Import POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:ToolKit>"
    "${GDTK_UTILS_IMPORT_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:assimp>"
    "${GDTK_UTILS_IMPORT_DIR}/"
  COMMENT "Import: staging ToolKit + assimp into ${GDTK_UTILS_IMPORT_DIR}"
  VERBATIM
)

# RPATH: find ToolKit + the vendored shared libs (SDL2, imgui) next to
# the binary (Utils/Import/) or under Dependency/Intermediate/...
# without LD_LIBRARY_PATH. On Linux assimp also lives next to the
# binary once the POST_BUILD copy runs, so $ORIGIN picks it up first.
if(UNIX)
  set_target_properties(Import PROPERTIES
    BUILD_RPATH   "$ORIGIN:${TK_DEPS_DIR}"
    INSTALL_RPATH "$ORIGIN:${TK_DEPS_DIR}"
  )
elseif(WIN32)
  # Windows resolves DLLs from the binary's directory first, so the
  # POST_BUILD copies are enough. No PATH plumbing needed.
endif()
