# Import -- GDTK's offline 3D asset converter.
# Linux binary: Bin/Import. Windows binary: Bin/Import.exe.
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
  -include ${CMAKE_SOURCE_DIR}/ToolKit/stdafx.h
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

# RPATH: find ToolKit.so + the vendored shared libs (SDL2, imgui, assimp)
# next to the binary. The build host has the runtime .so.N files but
# not the unversioned dev symlinks.
if(UNIX)
  set_target_properties(Import PROPERTIES
    BUILD_RPATH   "$ORIGIN:${TK_DEPS_DIR}"
    INSTALL_RPATH "$ORIGIN:${TK_DEPS_DIR}"
  )
endif()
