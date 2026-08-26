# Workspace -- small static library that holds project-management helpers
# shared by the Editor and the Launcher. Compiled into both executables.
#
# Source layout: Workspace/Source/ holds every workspace .cpp / .h,
# matching the legacy Workspace.vcxproj.filters "Source" grouping.

file(GLOB_RECURSE WORKSPACE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_library(Workspace STATIC
  ${WORKSPACE_SOURCES}
)

target_include_directories(Workspace PUBLIC
  # Workspace's headers all live under Source/, so that is the only path
  # consumers need to write `#include "Workspace.h"`. The parent
  # Workspace/ dir has no headers of its own.
  ${CMAKE_CURRENT_SOURCE_DIR}/Source
  ${GDTK_INCLUDE_DIRS}
)

# TK_GL_3_0 and TK_DEBUG are ToolKit-internal switches (desktop GL vs
# GLES, and the assert/DebugNew-macro gate respectively). Workspace
# itself needs them to compile its .cpp files, but consumers do NOT
# need to inherit them from here -- every consumer (Editor, Launcher,
# Import, Packer) already sets them in its own CMakeLists. Putting them
# PUBLIC here would re-broadcast the same flags onto every consumer
# without adding any new info, and would also mask a future consumer
# that forgot to define TK_DEBUG in Debug (Workspace would still work
# silently with assertions off). PRIVATE keeps the intent honest.
target_compile_definitions(Workspace PRIVATE
  TK_DLL_IMPORT
  TK_GL_3_0
  # Export Workspace's API from the host executable so editor plugins can
  # consume it (TK_WORKSPACE_API == dllexport here; consumers see dllimport).
  TK_WORKSPACE_DLL_EXPORT
  $<$<CONFIG:Debug>:TK_DEBUG>
)

target_compile_features(Workspace PUBLIC cxx_std_17)

# Precompiled header. ToolKit's stdafx.h is the project's shared
# "everything everyone needs" header (Platform.h, STL, GLM config,
# Pass.h, ...). Without it being injected into every translation
# unit, files that omit a transitive include fail to compile.
target_precompile_headers(Workspace PRIVATE
  ${CMAKE_SOURCE_DIR}/ToolKit/Source/stdafx.h
)

# GCC 16 enables -Wchanges-meaning by default. Pass.h has a member named
# 'Entity' whose type is also 'Entity', which trips it. The legacy MSVC
# build was clean; silence the warning here.
# -W<...> is a GCC/Clang flag; MSVC rejects it with D8021 ("invalid
# numeric argument"). Only attach it when we are not on MSVC.
if(NOT MSVC)
  target_compile_options(Workspace PRIVATE
    -Wno-changes-meaning
    -fpermissive
  )
endif()

# Output naming (matches ToolKit convention):
#   Unix    Debug:   libWorkspaced.a
#   Unix    Release: libWorkspace.a
#   Windows Debug:   Workspaced.lib
#   Windows Release: Workspace.lib
#
# CMake's default PREFIX is empty on Windows, "lib" on Unix for STATIC
# libs -- no override needed. OUTPUT_NAME adds the Debug 'd' suffix.
# The $<0:> trailer strips multi-config generator's $<CONFIGURATION>
# suffix to keep the output flat in Bin<Config>/.
set_target_properties(Workspace PROPERTIES
  OUTPUT_NAME "Workspace$<$<CONFIG:Debug>:d>"
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
)
