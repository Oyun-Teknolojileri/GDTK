# Editor -- GDTK's main development tool, an ImGui-based desktop application.
# Linux binary: Bin/Editor. Windows binary: Bin/Editor.exe.
#
# Source layout (mirrors the legacy Editor.vcxproj.filters groupings):
#   Editor/Source/                App, main, EditorScene, ...
#   Editor/Mods/                  Editor modes (Transform, Anchor, BoxEdit)
#   Editor/UI/                    ImGui plumbing (UI, Window, DynamicMenu, ...)
#   Editor/UI/Overlay/            TopBar, StatusBar, LeftBar, ...
#   Editor/UI/Viewport/           EditorViewport, PreviewViewport, ...
#   Editor/UI/Window/             ConsoleWindow, FolderWindow, ...
#   Editor/UI/View/               View, EntityView, MaterialView, ...
#   Editor/Entities/              Grid, Gizmo, EditorCamera, ...
#   Editor/Entities/Light/        EditorLight, LightMeshGenerator
#   Editor/Entities/UI/           EditorCanvas
#   Editor/Render/                EditorRenderer
#   Editor/Render/Pass/           GizmoPass
#   Editor/Components/            EditorEnvironmentComponent

file(GLOB_RECURSE EDITOR_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(Editor ${EDITOR_SOURCES})

# Each subfolder is added to the include path so legacy unqualified
# `#include "X.h"` keeps resolving after the move into subfolders.
foreach(editor_subdir
  Source
  Mods
  UI
  UI/Overlay
  UI/Viewport
  UI/Window
  UI/View
  Entities
  Entities/Light
  Entities/UI
  Render
  Render/Pass
  Components)
  target_include_directories(Editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/${editor_subdir}
  )
endforeach()

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
  -include ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
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
#
# The versioned-soname `-l:libX11.so.6` form is used on purpose: this
# repo is built on Fedora systems where the *-devel RPMs (which provide
# the unversioned `libX11.so` symlinks the linker looks up by default)
# are not installed. The runtime libraries are present, so linking
# directly against the versioned soname gives us a working binary
# without needing sudo to install libX11-devel / mesa-libGL-devel.
if(UNIX)
  target_link_libraries(Editor PRIVATE
    -l:libX11.so.6
    -l:libXext.so.6
    -l:libXrandr.so.2
    -l:libXinerama.so.1
    -l:libXi.so.6
    -l:libXcursor.so.1
    -l:libGL.so.1
    -l:libGLX.so.0
    pthread
    dl
  )
elseif(WIN32)
  target_link_libraries(Editor PRIVATE OpenGL32)
endif()

# Make sure the editor finds ToolKit.so next to itself and the vendored
# shared libs (SDL2, tkimgui) under Dependency/Intermediate/... without
# needing LD_LIBRARY_PATH or a post-build copy. RPATH covers both dirs.
if(UNIX)
  set_target_properties(Editor PROPERTIES
    BUILD_RPATH   "$ORIGIN:${TK_DEPS_DIR}"
    INSTALL_RPATH "$ORIGIN:${TK_DEPS_DIR}"
  )
endif()
