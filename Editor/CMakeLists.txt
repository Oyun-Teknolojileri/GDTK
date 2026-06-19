# Editor -- GDTK's main development tool, an ImGui-based desktop application.
# Linux binary: Bin/Editor. Windows binary: Bin/Editor.exe.

file(GLOB EDITOR_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(Editor ${EDITOR_SOURCES})

target_include_directories(Editor PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GDTK_INCLUDE_DIRS}
)

target_compile_definitions(Editor PRIVATE
  TK_EDITOR
  TK_EDITOR_DLL_EXPORT
  TK_GL_3_0
  TK_DLL_IMPORT
  $<$<CONFIG:Debug>:TK_DEBUG>
)

target_compile_features(Editor PRIVATE cxx_std_17)

# Precompiled header disabled (see ToolKit/CMakeLists.txt for the reason).
# MSVC's /FI "stdafx.h" equivalent for GCC -- Editor's sources assume the
# symbols stdafx.h transitively brings in are visible everywhere. The
# engine's stdafx.h is shared: TK_EDITOR is already defined above, so
# the editor-specific include block at the bottom of it gets pulled in.
target_compile_options(Editor PRIVATE
  -Wno-changes-meaning
  -Wno-delete-incomplete
  -include ${CMAKE_SOURCE_DIR}/ToolKit/stdafx.h
)

# Link against the engine (shared), the workspace helper (static), and the
# vendored deps. INTERFACE libraries (glm, glad, tkimgui, ...) only
# contribute include paths; the real libs come from ToolKit itself or
# the IMPORTED targets declared in the root CMakeLists.
target_link_libraries(Editor PRIVATE
  ToolKit
  Workspace
  SDL2
  SDL2main
  minizip
  zstd
  imgui
  glm
  glad
  RapidXml
  stb
  miniaudio
  poolstl
  tkimgui
)

# X11 / Wayland / OpenGL platform support. SDL2's pkg-config on Linux pulls
# in the bulk of these transitively, but listing them explicitly keeps the
# link line obvious.
if(UNIX)
  target_link_libraries(Editor PRIVATE X11 Xext Xrandr Xinerama Xi Xcursor
                                        GL GLX pthread dl)
elseif(WIN32)
  target_link_libraries(Editor PRIVATE OpenGL32)
endif()

# Make sure the editor finds ToolKit.so next to itself.
if(UNIX)
  set_target_properties(Editor PROPERTIES
    BUILD_RPATH   "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
  )
endif()
