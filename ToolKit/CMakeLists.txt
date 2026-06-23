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

# Precompiled header disabled for Phase 2: GCC 16 + the PCH pipeline
# promote a long-standing `changes meaning of 'Viewport'` warning in
# BillboardPass.h to a hard error, even though the legacy ToolKit.sln
# build was happy with it. We can re-enable PCH in a follow-up once
# stdafx.h is cleaned up.

# GCC 16 enables a handful of new warnings by default. Two of them bite
# the existing engine sources even though the MSVC build was clean:
#   - -Wchanges-meaning         in BillboardPass.h (member named Viewport)
#   - -Wdelete-incomplete       in Audio.cpp (SafeDel on void*)
# Both are noise for this phase; the code is otherwise well-behaved.
target_compile_options(ToolKit PRIVATE
  -Wno-changes-meaning
  -Wno-delete-incomplete
)

# stdafx.h in this project is *not* a proper PCH-only header; many .cpp
# files rely on the symbols it transitively pulls in (Platform.h, std::map,
# TK_PLATFORM, etc.). MSVC achieves this with /FI "stdafx.h" -- GCC's
# equivalent is -include. Keep them aligned until the source files are
# fixed to include their own dependencies.
target_compile_options(ToolKit PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/Source/stdafx.h
)

# GCC 16 (and modern Clang) treat `char*` -> `const char8_t*` conversions as
# hard errors by default, but ToolKit's Util.cpp / utf8.h bridge was written
# against the pre-C++20 relaxed rules. -fpermissive keeps the legacy code
# building on a modern compiler without rewriting the UTF-8 helpers.
target_compile_options(ToolKit PRIVATE
  -fpermissive
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
set_target_properties(ToolKit PROPERTIES
  OUTPUT_NAME "ToolKit$<$<CONFIG:Debug>:d>"
  PREFIX "lib"
)
