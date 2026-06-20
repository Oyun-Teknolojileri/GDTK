# Packer -- GDTK's release / publish tool.
# Linux binary: Bin/Packer. Windows binary: Bin/Packer.exe.
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
  -include ${CMAKE_SOURCE_DIR}/ToolKit/stdafx.h
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

if(UNIX)
  set_target_properties(Packer PROPERTIES
    BUILD_RPATH   "$ORIGIN:${TK_DEPS_DIR}"
    INSTALL_RPATH "$ORIGIN:${TK_DEPS_DIR}"
  )
endif()
