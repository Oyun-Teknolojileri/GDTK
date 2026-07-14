# Launcher -- GDTK's project browser / entry point. ImGui-based GUI.
# Linux binary: Bin<Config>/Launcher. Windows binary: Bin<Config>/Launcher.exe.
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

# Precompiled header. ToolKit's stdafx.h is shared with the engine;
# inject it into every translation unit so the symbols Pass.h,
# Object.h, etc. transitively bring in are always visible.
target_precompile_headers(Launcher PRIVATE
  ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
)

# Mirror Editor's GCC 16 warning suppressions. The Launcher sources include
# Pass.h transitively (via Workspace), which has a `Entity* Entity` field
# that GCC 16 promotes to a hard error under -Wchanges-meaning. Also turn
# off -Wdelete-incomplete -- same reason as Editor (Audio.cpp::SafeDel).
# All flags here are GCC/Clang-only; MSVC rejects them with D8021.
if(NOT MSVC)
  target_compile_options(Launcher PRIVATE
    -Wno-changes-meaning
    -Wno-delete-incomplete
    -fpermissive
  )
endif()

# Same link surface as the Editor: ToolKit (shared), Workspace (static),
# and the vendored deps the Launcher actually uses directly:
#   - SDL2 / SDL2main: window + input (Launcher/Source/main.cpp,
#     LauncherBackendBindings.cpp, Launcher.cpp).
#   - imgui: shared lib used everywhere in the launcher's UI.
# minizip / zstd are NOT listed here -- they are PRIVATE static libs
# embedded in libToolKit.so and the Launcher never calls them directly.
# The Launcher's include path through ToolKit exposes the engine API,
# which is all it needs.
target_link_libraries(Launcher PRIVATE
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

# X11 / OpenGL platform support. Same rationale as Editor/CMakeLists.txt:
# SDL2 is built as a shared library whose DT_NEEDED already pulls in
# libX11.so.6, libGL.so.1, libGLX.so.0, libXext, libXrandr, libXinerama,
# libXi and libXcursor at runtime, so we only have to declare the X11 +
# OpenGL surface the build system itself needs (configure-time check
# that the *-devel packages are installed). find_package makes a
# missing -devel package fail-fast with a clear error, instead of the
# old hardcoded-soname hack that silently picked up whatever happened
# to be on the system.
if(UNIX AND NOT APPLE)
  find_package(X11    REQUIRED)
  find_package(OpenGL REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GLX REQUIRED glx)

  target_link_libraries(Launcher PRIVATE
    X11::X11
    OpenGL::GL
    ${GLX_LIBRARIES}
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

# Launcher links against ToolKit and should re-link whenever the engine
# DLL changes so the import library stays in sync with the exports.
add_dependencies(Launcher ToolKit)

# Flat Bin/ output. See ToolKit/CMakeLists.txt for why the $<0:>
# trailer is here: it strips the multi-config generator's
# $<CONFIGURATION> suffix so Launcher (and its .lib / .exp
# side artifacts) land at Bin/ instead of Bin/Debug/.
set_target_properties(Launcher PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
)
