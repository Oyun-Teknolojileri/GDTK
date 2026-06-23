# Packer -- GDTK's release / publish tool.
# Linux binary: Utils/Packer/Packer. Windows binary: Utils/Packer/Packer.exe.
# ToolKit is staged next to the binary at build time (see the POST_BUILD
# block at the bottom of this file).
#
# Single-file CLI tool. Bundles a project into a redistributable form
# (Windows zip, Web zip, Android APK, Game/Editor plugin). Most code
# paths are platform-specific (Android NDK, adb, etc.) and the Linux
# build only covers the parts that compile and link on a desktop box.
# Headless: same pattern as Import -- the render system is initialized
# just to satisfy engine resource factories, no window is created.

file(GLOB PACKER_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(Packer ${PACKER_SOURCES})

target_include_directories(Packer PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GDTK_INCLUDE_DIRS}
)

target_compile_definitions(Packer PRIVATE
  TK_DLL_IMPORT
  TK_GL_3_0
  $<$<CONFIG:Debug>:TK_DEBUG>
)

target_compile_features(Packer PRIVATE cxx_std_17)

# Same GCC 16 workarounds as Import/Launcher: Pass.h's `Entity* Entity`
# field trips -Wchanges-meaning, utf8.h trips the C++20 char8_t error.
target_compile_options(Packer PRIVATE
  -Wno-changes-meaning
  -Wno-delete-incomplete
  -include ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
  -fpermissive
)

# Packer bakes/packs projects -- it does NOT import 3D assets, so it
# does not link assimp. (ToolKit itself also does not link assimp;
# only Import does.) Keep this target's link surface minimal.
target_link_libraries(Packer PRIVATE
  ToolKit
  Workspace
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
  target_link_libraries(Packer PRIVATE
    -l:libGL.so.1
    -l:libGLX.so.0
    pthread
    dl
  )
elseif(WIN32)
  target_link_libraries(Packer PRIVATE OpenGL32)
endif()

# ---------------------------------------------------------------------------
# Output: Utils/Packer/
#
# Packer ships as a standalone CLI tool -- users run it from a fresh
# checkout (CI, packagers) without the full Bin/ tree available, so we
# publish it to Utils/Packer/ alongside its runtime dep (ToolKit).
# POST_BUILD below copies ToolKit in. Packer does NOT need assimp --
# see the comment on target_link_libraries above.
#
# - add_dependencies(Packer ToolKit) is the trigger: when any ToolKit
#   source changes, ToolKit is rebuilt AND Packer is relinked (because
#   ToolKit is on Packer's link line). The relink re-runs the POST_BUILD
#   copy, so Utils/Packer/ToolKit.so stays in sync with Bin/ToolKit.so.
# - The POST_BUILD also runs unconditionally on every Packer build,
#   so a clean build always populates Utils/Packer/.
# ---------------------------------------------------------------------------
set_target_properties(Packer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_UTILS_PACKER_DIR}"
)
add_dependencies(Packer ToolKit)

# Generator expression: per-config file path. Works for single-config
# (Ninja, NMake, MinGW) and multi-config (Visual Studio) alike.
add_custom_command(TARGET Packer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:ToolKit>"
    "${GDTK_UTILS_PACKER_DIR}/"
  COMMENT "Packer: staging ToolKit into ${GDTK_UTILS_PACKER_DIR}"
  VERBATIM
)

# RPATH: find ToolKit + vendored shared libs (SDL2, imgui) next to the
# binary (Utils/Packer/) or under Dependency/Intermediate/... without
# LD_LIBRARY_PATH. The POST_BUILD copy of ToolKit lands in $ORIGIN.
if(UNIX)
  set_target_properties(Packer PROPERTIES
    BUILD_RPATH   "$ORIGIN:${TK_DEPS_DIR}"
    INSTALL_RPATH "$ORIGIN:${TK_DEPS_DIR}"
  )
elseif(WIN32)
  # Windows resolves DLLs from the binary's directory first, so the
  # POST_BUILD copy is enough. No PATH plumbing needed.
endif()
