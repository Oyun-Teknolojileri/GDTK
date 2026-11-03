# RenderDoc -- editor glue for the engine capture core, a static library the
# Editor links.
#
# Source layout: RenderDoc/Source/ holds the module .cpp / .h, RenderDoc/Scripts/
# holds the python report tooling (not part of the build - the glob below only
# takes *.cpp).
#
# What is NOT here any more: the capture machinery. Binding RenderDoc, driving the
# frame, writing the .rdc and serving the request file live in
# ToolKit/Source/RenderDocCapture.{h,cpp}, because a game needs the same thing and
# a game links only ToolKit. This module is the editor half: where captures go
# (<workspace>/<project>/Captures), what the sidecar records, and the console
# commands plus Modules menu entries it contributes through the editor API
# (Editor/Source/EditorApi.h).
#
# This is a STATIC library linked INTO Editor.exe, on purpose: the capture hooks
# have to be in place before the graphics device is created, so the module comes
# up with the host rather than being loaded later. Its API is exported with
# TK_RENDERDOC_API so editor plugins can call it too; the editor sets
# TK_RENDERDOC_DLL_EXPORT for its own translation units for the same reason it
# sets TK_WORKSPACE_DLL_EXPORT (see Editor/CMakeLists.txt).

file(GLOB_RECURSE RENDERDOC_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_library(RenderDoc STATIC
  ${RENDERDOC_SOURCES}
)

# Source/ holds RenderDocModule.h, the contract with the editor. The module
# .cpp files are the only other consumers of their own headers.
target_include_directories(RenderDoc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Source
  ${GDTK_INCLUDE_DIRS}
)

target_compile_definitions(RenderDoc PRIVATE
  TK_DLL_IMPORT
  TK_GL_3_0
  # The module is editor code that lives in its own target, so it sees the same
  # switches an editor translation unit sees. TK_EDITOR_DLL_EXPORT is the
  # inverse of what a consumer normally gets: without it TK_EDITOR_API would
  # resolve to dllimport inside a static library that is linked into the very
  # executable defining those symbols, and the calls would never link.
  TK_EDITOR
  TK_EDITOR_DLL_EXPORT
  TK_WORKSPACE_DLL_EXPORT
  # Export the module's own API from Editor.exe (TK_RENDERDOC_API == dllexport
  # here; consumers of the module see dllimport).
  TK_RENDERDOC_DLL_EXPORT
  $<$<CONFIG:Debug>:TK_DEBUG>
)

# Mirror the editor: when the engine is built with Vulkan, the editor headers
# this module includes expand their Vulkan branches too, and both sides have to
# agree on them.
if(TK_VULKAN)
  target_compile_definitions(RenderDoc PRIVATE TK_VULKAN)
endif()

target_compile_features(RenderDoc PUBLIC cxx_std_17)

# Precompiled header. ToolKit's stdafx.h is the project's shared "everything
# everyone needs" header; without it being injected into every translation unit,
# files that omit a transitive include fail to compile.
target_precompile_headers(RenderDoc PRIVATE
  ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
)

# Same GCC/Clang workarounds the editor needs, because the module includes the
# same headers (Pass.h has a member named 'Entity', the UTF-8 helpers predate the
# C++20 char8_t rules). MSVC rejects both flags with D8021.
if(NOT MSVC)
  target_compile_options(RenderDoc PRIVATE
    -Wno-changes-meaning
    -fpermissive
  )
endif()

# Output naming follows the other modules:
#   Windows Debug:   RenderDocd.lib      Unix Debug:   libRenderDocd.a
#   Windows Release: RenderDoc.lib       Unix Release: libRenderDoc.a
set_target_properties(RenderDoc PROPERTIES
  OUTPUT_NAME "RenderDoc$<$<CONFIG:Debug>:d>"
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
)