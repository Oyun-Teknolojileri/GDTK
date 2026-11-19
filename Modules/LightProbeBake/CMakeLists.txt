# LightProbeBake -- CPU path traced light probe baker.
#
# A STATIC library linked into Editor and Packer, never into a game and never into the web
# build: a shipped game only samples the baked coefficients, and the wasm should not carry a
# ray tracer.
#
# Source layout: Source/ holds the module, Tools/ holds the ProbeBake command line front end.
# The bake owns no engine object after its synchronous scene snapshot, so the lifetime rules in
# AGENTS.md are satisfied by construction.

file(GLOB_RECURSE LIGHT_PROBE_BAKE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
)

add_library(LightProbeBake STATIC
  ${LIGHT_PROBE_BAKE_SOURCES}
)

target_include_directories(LightProbeBake PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Source
  ${GDTK_INCLUDE_DIRS}
)

target_compile_definitions(LightProbeBake PRIVATE
  TK_DLL_IMPORT
  TK_GL_3_0
  $<$<CONFIG:Debug>:TK_DEBUG>
)

# Mirror ToolKit's Vulkan gate: the engine headers this module includes expand their Vulkan
# branches too, and both sides have to agree on them.
if(TK_VULKAN)
  target_compile_definitions(LightProbeBake PRIVATE TK_VULKAN)
endif()

target_compile_features(LightProbeBake PUBLIC cxx_std_17)

# ToolKit's stdafx.h is the project's shared "everything everyone needs" header; without it
# being injected into every translation unit, files that omit a transitive include fail.
target_precompile_headers(LightProbeBake PRIVATE
  ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
)

# Same GCC/Clang workarounds the other ToolKit consumers need. MSVC rejects both flags.
if(NOT MSVC)
  target_compile_options(LightProbeBake PRIVATE
    -Wno-changes-meaning
    -fpermissive
  )
endif()

target_link_libraries(LightProbeBake PUBLIC ToolKit)

# Output naming follows the other modules:
#   Windows Debug:   LightProbeBaked.lib      Unix Debug:   libLightProbeBaked.a
#   Windows Release: LightProbeBake.lib       Unix Release: libLightProbeBake.a
set_target_properties(LightProbeBake PROPERTIES
  OUTPUT_NAME "LightProbeBake$<$<CONFIG:Debug>:d>"
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
)

# ---------------------------------------------------------------------------
# ProbeBake -- headless command line front end.
#
# Boots the engine with the null backend (no window, no GPU) so a CI box can refresh a stale
# bake. The editor calls Bake() in process instead of shelling out to this.
# ---------------------------------------------------------------------------
add_executable(ProbeBake
  ${CMAKE_CURRENT_SOURCE_DIR}/Tools/ProbeBakeMain.cpp
)

target_include_directories(ProbeBake PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Source
  ${GDTK_INCLUDE_DIRS}
)

target_compile_definitions(ProbeBake PRIVATE
  TK_DLL_IMPORT
  TK_GL_3_0
  $<$<CONFIG:Debug>:TK_DEBUG>
)

if(TK_VULKAN)
  target_compile_definitions(ProbeBake PRIVATE TK_VULKAN)
endif()

target_compile_features(ProbeBake PRIVATE cxx_std_17)

target_precompile_headers(ProbeBake PRIVATE
  ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
)

if(NOT MSVC)
  target_compile_options(ProbeBake PRIVATE
    -Wno-changes-meaning
    -Wno-delete-incomplete
    -fpermissive
  )
endif()

# Same engine link surface Packer uses; the bake pulls in none of the optional libs itself.
target_link_libraries(ProbeBake PRIVATE
  LightProbeBake
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

  target_link_libraries(ProbeBake PRIVATE
    OpenGL::GL
    ${GLX_LIBRARIES}
    pthread
    dl
  )
elseif(WIN32)
  target_link_libraries(ProbeBake PRIVATE OpenGL32)
endif()

add_dependencies(ProbeBake ToolKit LightProbeBake)

set_target_properties(ProbeBake PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
)

if(UNIX OR WIN32)
  set_target_properties(ProbeBake PROPERTIES
    BUILD_RPATH   "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
  )
endif()
