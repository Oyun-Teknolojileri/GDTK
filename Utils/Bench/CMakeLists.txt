# Bench -- deterministic render benchmark and visual regression host.
# Linux binary: Bin<Config>/Bench. Windows binary: Bin<Config>/Bench.exe.
#
# Single-file host application. It builds a test scene in code, drives a
# deterministic camera path for a fixed number of frames, and writes a per
# frame CSV plus PNG captures that can be compared against a stored baseline.
# See render-cpu-optimization-plan.md in the repository root for the plan this
# tool exists to serve.
#
# Unlike Packer and Import this target needs a real window and a real GL
# context, because the numbers and the images it produces are the whole point.

file(GLOB BENCH_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(Bench ${BENCH_SOURCES})

target_include_directories(Bench PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GDTK_INCLUDE_DIRS}
)

target_compile_definitions(Bench PRIVATE
  TK_DLL_IMPORT
  TK_GL_3_0
  $<$<CONFIG:Debug>:TK_DEBUG>
)

target_compile_features(Bench PRIVATE cxx_std_17)

# Precompiled header. ToolKit's stdafx.h is shared with the engine; inject it
# into every translation unit so the symbols Pass.h, Object.h, etc.
# transitively bring in are always visible.
target_precompile_headers(Bench PRIVATE
  ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
)

# Same GCC 16 workarounds as Import/Launcher/Packer. MSVC rejects these flags.
if(NOT MSVC)
  target_compile_options(Bench PRIVATE
    -Wno-changes-meaning
    -Wno-delete-incomplete
    -fpermissive
  )
endif()

# Bench goes through the whole engine: scene, passes, renderer, backend.
# It does not import 3D assets, so assimp is not linked.
target_link_libraries(Bench PRIVATE
  ToolKit
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

  target_link_libraries(Bench PRIVATE
    OpenGL::GL
    ${GLX_LIBRARIES}
    pthread
    dl
  )
elseif(WIN32)
  target_link_libraries(Bench PRIVATE OpenGL32)
endif()

# ---------------------------------------------------------------------------
# Output: Bin<Config>/
#
# Bench outputs next to Editor, Packer, Import and the ToolKit DLL so it can
# find the same runtime dependencies without any extra staging.
# ---------------------------------------------------------------------------
add_dependencies(Bench ToolKit)
set_target_properties(Bench PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
)

if(WIN32 OR UNIX)
  set_target_properties(Bench PROPERTIES
    BUILD_RPATH   "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
  )
endif()
