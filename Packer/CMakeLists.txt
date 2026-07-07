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

# Precompiled header. ToolKit's stdafx.h is shared with the engine;
# inject it into every translation unit so the symbols Pass.h,
# Object.h, etc. transitively bring in are always visible.
target_precompile_headers(Packer PRIVATE
  ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
)

# Same GCC 16 workarounds as Import/Launcher: Pass.h's `Entity* Entity`
# field trips -Wchanges-meaning, utf8.h trips the C++20 char8_t error.
# All of these flags are GCC/Clang-only; MSVC rejects them with
# D8021 ("invalid numeric argument"). Skip the whole block on MSVC.
if(NOT MSVC)
  target_compile_options(Packer PRIVATE
    -Wno-changes-meaning
    -Wno-delete-incomplete
    -fpermissive
  )
endif()

# Packer bakes/packs projects -- it does NOT import 3D assets, so it
# does not link assimp. (ToolKit itself also does not link assimp;
# only Import does.) Keep this target's link surface minimal.
#
# - SDL2 / SDL2main: Packer/Source/Packer.cpp initializes the engine
#   through ToolKit (m_renderSys), which spins up a GL context; SDL2
#   is therefore required and SDL2main is the static entry-point shim.
# - minizip / zstd are NOT listed here -- ToolKit embeds them as
#   PRIVATE static libs and Packer only goes through ToolKit's API.
# - imgui / tkimgui are NOT listed -- Packer is a headless CLI tool.
target_link_libraries(Packer PRIVATE
  ToolKit
  Workspace
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

  target_link_libraries(Packer PRIVATE
    OpenGL::GL
    ${GLX_LIBRARIES}
    pthread
    dl
  )
elseif(WIN32)
  target_link_libraries(Packer PRIVATE OpenGL32)
endif()

# ---------------------------------------------------------------------------
# Output: Utils/Packer/
#
# Packer is a standalone CLI tool, published to Utils/Packer/ so users
# can run it from a fresh checkout. RPATH ($ORIGIN/../../Bin) points
# the binary at the canonical libToolKitd.so under Bin/ on Linux. On
# Windows the DLL is staged next to the executable by ToolKit's own
# POST_BUILD step (ToolKit/CMakeLists.txt), because the Windows loader
# searches the exe directory before RPATH.
#
# Packer does NOT link assimp (it does not import 3D assets) -- the
# comment on target_link_libraries above documents this. So no assimp
# entry is needed in the RPATH either.
#
# add_dependencies ensures Packer re-links whenever ToolKit changes,
# so the import library stays in sync with the DLL exports.
#
# Layout after a build:
#   Bin/                  libToolKitd.so / libToolKitd.dll (canonical)
#   Utils/Packer/         Packer + libToolKitd.dll         (Windows)
#   Dependency/.../<cfg>/ libSDL2-2.0d.so, libimguid.so
# ---------------------------------------------------------------------------
add_dependencies(Packer ToolKit)
set_target_properties(Packer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_UTILS_PACKER_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_UTILS_PACKER_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_UTILS_PACKER_DIR}/$<0:>"
)

# RPATH strategy (Linux + Windows both honour $ORIGIN via CMake 3.16+):
#   - $ORIGIN/../../Bin      -> Utils/Packer/Packer finds libToolKitd.so /
#                                ToolKitd.dll under the canonical Bin/.
#   - ${TK_DEPS_DIR}         -> absolute path to Dependency/Intermediate/
#                                <Plat>/<Cfg>/, where SDL2 / imgui live.
# On Linux this is encoded into the ELF DT_RUNPATH; on Windows CMake
# converts it into the DLL search path in the PE header. Both platforms
# resolve $ORIGIN to the directory containing the executable at runtime,
# so the binary can be invoked from any cwd without LD_LIBRARY_PATH or
# %PATH% tweaks.
if(UNIX)
  set_target_properties(Packer PROPERTIES
    BUILD_RPATH   "$ORIGIN/../../Bin:${TK_DEPS_DIR}"
    INSTALL_RPATH "$ORIGIN/../../Bin:${TK_DEPS_DIR}"
  )
elseif(WIN32)
  set_target_properties(Packer PROPERTIES
    BUILD_RPATH   "$ORIGIN/../../Bin;${TK_DEPS_DIR}"
    INSTALL_RPATH "$ORIGIN/../../Bin;${TK_DEPS_DIR}"
  )
endif()
