# Launcher -- GDTK's project browser / entry point. ImGui-based GUI.
# Linux binary: Bin/Launcher. Windows binary: Bin/Launcher.exe.
#
# Mirrors Editor/CMakeLists.txt because the two apps share most of the
# engine surface (SDL2 + OpenGL + ImGui + ToolKit). The Launcher pulls
# in a subset of Editor/ because LauncherApp reuses Editor types
# (EditorTypes.h, IconsFontAwesome.h) and the Workspace helper.
#
# The TK_EDITOR macro is set so any code in Editor/ that branches on
# it stays consistent with the Editor build; on Linux the macro does
# not affect TK_EDITOR_API (it is always default ELF visibility here,
# see Editor/EditorTypes.h).
#
# Source layout: Launcher/Source/ holds every launcher .cpp / .h,
# matching the legacy Launcher.vcxproj.filters "Source" grouping.

file(GLOB_RECURSE LAUNCHER_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(Launcher ${LAUNCHER_SOURCES})

# The Launcher reuses headers from Editor/ (EditorTypes.h, IconsFontAwesome.h)
# and from Workspace/ (Workspace.h, WorkspaceTypes.h). The latter is already
# in GDTK_INCLUDE_DIRS; Editor/ is not, so we add it explicitly.
target_include_directories(Launcher PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/Source
  ${CMAKE_SOURCE_DIR}/Editor
  ${GDTK_INCLUDE_DIRS}
)

target_compile_definitions(Launcher PRIVATE
  TK_EDITOR
  TK_DLL_IMPORT
  TK_GL_3_0
  $<$<CONFIG:Debug>:TK_DEBUG>
)

target_compile_features(Launcher PRIVATE cxx_std_17)

# Mirror Editor's GCC 16 warning suppressions. The Launcher sources include
# Pass.h transitively (via Workspace), which has a `Entity* Entity` field
# that GCC 16 promotes to a hard error under -Wchanges-meaning. Also turn
# off -Wdelete-incomplete -- same reason as Editor (Audio.cpp::SafeDel).
target_compile_options(Launcher PRIVATE
  -Wno-changes-meaning
  -Wno-delete-incomplete
  -include ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
)

# -fpermissive is the same workaround as ToolKit itself: GCC 16's hard
# error on `char*` -> `const char8_t*` would otherwise fail utf8.h.
target_compile_options(Launcher PRIVATE -fpermissive)

# Same link surface as the Editor: ToolKit (shared), Workspace (static),
# all the vendored deps (SDL2, minizip, zstd, imgui + the INTERFACE
# header-only libs). The Launcher does not export any symbols, so the
# editor's TK_EDITOR_DLL_EXPORT switch is not needed here.
target_link_libraries(Launcher PRIVATE
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

# SDL2 + OpenGL platform deps. Same versioned-soname form as the Editor:
# the *-devel RPMs aren't installed on the build host, so linking against
# the runtime .so.N files sidesteps the missing unversioned symlinks.
if(UNIX)
  target_link_libraries(Launcher PRIVATE
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
  target_link_libraries(Launcher PRIVATE OpenGL32)
endif()

# RPATH: find ToolKit.so + the vendored shared libs (SDL2, imgui) next to
# the binary / under Dependency/Intermediate/... without LD_LIBRARY_PATH.
if(UNIX)
  set_target_properties(Launcher PROPERTIES
    BUILD_RPATH   "$ORIGIN:${TK_DEPS_DIR}"
    INSTALL_RPATH "$ORIGIN:${TK_DEPS_DIR}"
  )
endif()
