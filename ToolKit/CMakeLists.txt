# ToolKit engine -- shared library (ToolKit.so on Linux, ToolKit.dll on Windows).
#
# Source layout (mirrors the legacy ToolKit.vcxproj.filters groupings):
#   ToolKit/Source/                 Engine plumbing (Logger, FileManager, ...)
#   ToolKit/Resources/              Resource types (Material, Mesh, Texture, ...)
#   ToolKit/UI/                     ImGui / 2D UI (Canvas, Dpad, UIManager, ...)
#   ToolKit/Entities/               Game objects (Camera, Entity, Light, ...)
#   ToolKit/Components/             Components (Mesh, Skeleton, Material, ...)
#   ToolKit/Render/                 Renderer core (Pass, Renderer, RenderSystem, ...)
#   ToolKit/Render/RenderPass/      Per-frame render passes
#   ToolKit/Render/PostProcessPass/ Post-process passes
#   ToolKit/Render/Vulkan/          Optional Vulkan backend (Phase 3)
#   ToolKit/Common/                 Cross-platform helpers (untouched)
#   ToolKit/toolkit.natvis          Visual Studio debugger view (untouched)

file(GLOB_RECURSE TOOLKIT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# GLOB_RECURSE picks up the Vulkan backend too, but that backend needs
# the system <vulkan/vulkan.h> headers (only present when the Vulkan
# SDK is installed) and links against vulkan-1. Drop it from the source
# list when TK_VULKAN is OFF, matching the legacy ToolKit.sln behavior
# where the Vulkan .vcxproj was an optional sibling.
if(NOT TK_VULKAN)
  list(FILTER TOOLKIT_SOURCES EXCLUDE REGEX ".*/Render/Vulkan/.*\\.cpp$")
endif()

add_library(ToolKit SHARED ${TOOLKIT_SOURCES})

# Each subfolder is added to the public include path so legacy unqualified
# `#include "X.h"` keeps resolving after the move into subfolders. PUBLIC
# because ToolKit is a shared library and downstream targets (Editor,
# Launcher, Packer, Import) build against its public headers.
foreach(toolkit_subdir
  Source
  Resources
  UI
  Entities
  Components
  Render
  Render/RenderPass
  Render/PostProcessPass
  Common)
  target_include_directories(ToolKit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/${toolkit_subdir}
  )
endforeach()

if(TK_VULKAN)
  target_include_directories(ToolKit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Vulkan
  )
endif()

target_include_directories(ToolKit PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GDTK_INCLUDE_DIRS}
)

# Compile definitions -- the public API uses these to switch between
# import / export and between desktop GL / GLES.
target_compile_definitions(ToolKit PUBLIC
  TK_GL_3_0
  $<$<CONFIG:Debug>:TK_DEBUG>
)
target_compile_definitions(ToolKit PRIVATE
  TK_DLL_EXPORT
)

target_compile_features(ToolKit PUBLIC cxx_std_17)

# Precompiled header. stdafx.h is the project's shared "everything
# everyone needs" header (Platform.h, STL, GLM config, ...); without
# it being injected into every translation unit, files that omit a
# transitive include (e.g. Object.h via ToolKit.h) fail to compile.
# CMake's target_precompile_headers works on both MSVC (cl.exe's
# native /Yu+stdafx.cpp) and GCC/Clang (-include), so this is the
# single source of truth for both toolchains.
target_precompile_headers(ToolKit PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Source/stdafx.h
)

# GCC 16 enables a handful of new warnings by default. Two of them bite
# the existing engine sources even though the MSVC build was clean:
#   - -Wchanges-meaning         in BillboardPass.h (member named Viewport)
#   - -Wdelete-incomplete       in Audio.cpp (SafeDel on void*)
# Both are noise for this phase; the code is otherwise well-behaved.
# These flags are GCC/Clang-only -- MSVC has its own /W<level> family
# and rejects unknown command-line arguments with D8021.
if(NOT MSVC)
  target_compile_options(ToolKit PRIVATE
    -Wno-changes-meaning
    -Wno-delete-incomplete
  )
endif()

# GCC 16 (and modern Clang) treat `char*` -> `const char8_t*` conversions as
# hard errors by default, but ToolKit's Util.cpp / utf8.h bridge was written
# against the pre-C++20 relaxed rules. -fpermissive keeps the legacy code
# building on a modern compiler without rewriting the UTF-8 helpers.
# -fpermissive is GCC/Clang-only; MSVC has /permissive- which is the
# inverse and applied via CMAKE_CXX_EXTENSIONS / CMAKE_CXX_STANDARD
# elsewhere in the tree.
if(NOT MSVC)
  target_compile_options(ToolKit PRIVATE
    -fpermissive
  )
endif()

# Vendored dependency libraries.
#
# Static libs (minizip + zstd) are PRIVATE: ToolKit's FileManager.cpp
# uses the zip API and the ZSTD compression method
# (MZ_COMPRESS_METHOD_ZSTD), so the symbols must be embedded inside
# libToolKit.so -- this is the link edge that requires PIC on the
# dependency side (see Dependency/CMakeLists.txt's
# CMAKE_POSITION_INDEPENDENT_CODE). PRIVATE keeps them out of the
# public INTERFACE so consumers don't accidentally grow an undeclared
# dependency on minizip/zstd; if a future consumer needs them directly,
# it should declare its own link line.
#
# Shared libs (SDL2, SDL2main, imgui) are NOT linked here. ToolKit's
# public headers do not expose SDL2 or ImGui types, and none of
# ToolKit's source files include them -- adding them to ToolKit's link
# line would forward a transitive dependency that ToolKit itself does
# not have, which lies about the library's interface. Editor /
# Launcher / Import / Packer link SDL2 + imgui directly in their own
# CMakeLists.
target_link_libraries(ToolKit PRIVATE
  minizip
  zstd
)

# Platform-specific link libraries.
# Note: GL is intentionally NOT linked here. ToolKit uses glad (vendored)
# which resolves OpenGL entry points dynamically at runtime. Linking
# libGL would force every consumer (Editor) to pull in the system OpenGL
# ABI, which is unnecessary and complicates distributions without
# mesa-libGL-devel installed.
if(UNIX)
  target_link_libraries(ToolKit PUBLIC pthread dl)
elseif(WIN32)
  target_link_libraries(ToolKit PUBLIC OpenGL32)
  if(TK_VULKAN)
    target_link_libraries(ToolKit PUBLIC vulkan-1)
  endif()
endif()

# Output naming: ToolKitd on Debug, ToolKit on Release.
#
# The $<0:> trailer on the *OUTPUT_DIRECTORY properties below is a
# generator expression that always expands to the empty string. On
# multi-config generators (Visual Studio) CMake would otherwise
# append $<CONFIGURATION> as a final path component, putting
# libToolKitd.dll at Bin/Debug/ instead of Bin/. The $<0:> trailer
# neutralises that suffix; on single-config generators (Ninja,
# Unix Makefiles) there is no suffix to strip, so the trailing
# slash is a no-op and the layout is naturally flat.
set_target_properties(ToolKit PROPERTIES
  OUTPUT_NAME "ToolKit$<$<CONFIG:Debug>:d>"
  PREFIX "lib"
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
)
