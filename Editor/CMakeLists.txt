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

add_executable(Editor WIN32 ${EDITOR_SOURCES})

# Windows: embed the app icon (.rc compiled to .res by the MSVC resource
# compiler, linked into the executable). The .rc references the icon at
#   ../Resources/Engine/Textures/Icons/app.ico
# relative to Editor/Editor.rc, which resolves to the engine asset tree.
if(WIN32)
  target_sources(Editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Editor.rc)
endif()

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

# Mirror ToolKit's TK_VULKAN gate. ToolKit only forwards TK_VULKAN
# PRIVATE (see ToolKit/CMakeLists.txt) because Launcher hard-wires
# itself to GL via `#undef TK_VULKAN` and precompiled stdafx.h would
# expand the editor-side Vulkan macros (Convert2ImGuiTexture in
# EditorTypes.h, EditorImGuiTextureCache, EditorBackendBindings)
# before Launcher.h got a chance to undef. We re-declare it here so
# Editor itself sees TK_VULKAN=1 when ToolKit was built with it;
# Import and Packer (the other consumers) do not need this because
# they don't use the editor-side ImGui/texture bridge.
if(TK_VULKAN)
  target_compile_definitions(Editor PRIVATE TK_VULKAN)
endif()

target_compile_features(Editor PRIVATE cxx_std_17)

# Precompiled header. The engine's stdafx.h is shared: TK_EDITOR is
# already defined above, so the editor-specific include block at the
# bottom of it (EditorTypes.h, App.h, EditorRenderer.h) gets pulled
# in here too. CMake's target_precompile_headers works on both MSVC
# (cl.exe's native /Yu+stdafx.cpp) and GCC/Clang (-include), so this
# is the single source of truth for both toolchains.
target_precompile_headers(Editor PRIVATE
  ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
)

# GCC 16 enables a handful of new warnings by default. Two of them bite
# the existing sources even though the MSVC build was clean:
#   - -Wchanges-meaning         in BillboardPass.h (member named Viewport)
#   - -Wdelete-incomplete       in Audio.cpp (SafeDel on void*)
# These flags are GCC/Clang-only; MSVC rejects them with D8021.
if(NOT MSVC)
  target_compile_options(Editor PRIVATE
    -Wno-changes-meaning
    -Wno-delete-incomplete
  )
endif()

# Link against the engine (shared), the workspace helper (static), and the
# vendored deps the Editor actually consumes directly.
#
# - SDL2 / SDL2main: Editor uses SDL2 for window + input
#   (Editor/Source/main.cpp, Editor/Mods/*Mod.cpp, etc.); SDL2main is
#   the static entry-point shim, must be embedded into the executable.
# - imgui / tkimgui: shared lib, used everywhere in the editor's UI.
# - minizip / zstd: NOT listed here. ToolKit embeds them as PRIVATE
#   static libs (libToolKit.so carries the symbols), and Editor does
#   not call any zip / ZSTD API directly -- it only goes through
#   ToolKit's file API. Re-listing them on the Editor link line would
#   shadow ToolKit's symbols and produce duplicate-symbol warnings
#   if anything in Editor ever does #include <mz.h>.
#
# INTERFACE libraries (glm, glad, tkimgui, ...) only contribute include
# paths; the real libs come from ToolKit itself or the IMPORTED targets
# declared in the root CMakeLists.
target_link_libraries(Editor PRIVATE
  ToolKit
  Workspace
  SDL2
  SDL2main
  imgui
  glm
  glad
  RapidXml
  stb
  miniaudio
  poolstl
  tkimgui
)

# X11 / OpenGL platform support.
#
# The Editor / Launcher / Import / Packer all use SDL2 for windowing,
# and SDL2 is built as a shared library whose DT_NEEDED already pulls
# in libX11.so.6, libGL.so.1, libGLX.so.0, libXext, libXrandr,
# libXinerama, libXi and libXcursor at runtime. We don't have to
# forward those ourselves -- listing them here would just duplicate
# SDL2's own dependency declaration and pull the X11 extensions
# twice on link (once via SDL2's DT_NEEDED, once via Editor's).
#
# What we DO need to declare is the bit of X11 / OpenGL surface that
# the build system itself interacts with at configure time: pkg-config
# needs to know which *-devel packages to require, and our find_package
# calls make missing -devel packages fail-fast with a clear message
# instead of producing a binary that mysteriously won't start.
#
# - FindX11  (built into CMake)   -> X11::X11 imported target
# - FindOpenGL (built into CMake) -> OpenGL::GL imported target
# - FindGLX  is NOT shipped with CMake -- pkg-config's `glx.pc` does
#   the same job and is provided by libglvnd-devel / mesa-libGL-devel
#   on the major distros.
#
# If any of these is missing, the configure step aborts with a clear
# "Could NOT find X11 (missing: X11_X11_LIB ...)" message, which tells
# the user to install libX11-devel / mesa-libGL-devel. That is the
# intentional trade-off here: we trade the "build without -devel
# packages" hack (which relied on hardcoded sonames and silently
# picked up whatever X11 happened to be on the system) for explicit,
# reproducible builds.
if(UNIX AND NOT APPLE)
  find_package(X11    REQUIRED)
  find_package(OpenGL REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLX REQUIRED glx)

  target_link_libraries(Editor PRIVATE
    X11::X11
    OpenGL::GL
    ${GLX_LIBRARIES}
    pthread
    dl
  )
elseif(WIN32)
  target_link_libraries(Editor PRIVATE OpenGL32)
endif()

# RPATH: $ORIGIN finds libToolKitd.so and vendored shared libs (SDL2,
# imgui) next to the binary in Bin<Config>/ after `cmake --install`.
if(UNIX)
  set_target_properties(Editor PROPERTIES
    BUILD_RPATH   "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
  )
endif()

# Editor links against ToolKit and should re-link whenever the engine
# DLL changes so the import library stays in sync with the exports.
add_dependencies(Editor ToolKit)

# Flat Bin/ output. See ToolKit/CMakeLists.txt for why the $<0:>
# trailer is here: it strips the multi-config generator's
# $<CONFIGURATION> suffix so Editor.exe (and its .exp / .lib
# import library, which the VS generator still produces for an
# executable that links a shared library) land at Bin/ instead
# of Bin/Debug/.
set_target_properties(Editor PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
)
