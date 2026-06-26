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
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/Source
  ${GDTK_INCLUDE_DIRS}
)

target_compile_definitions(Workspace PUBLIC
  TK_GL_3_0
  $<$<CONFIG:Debug>:TK_DEBUG>
)
target_compile_definitions(Workspace PRIVATE
  TK_DLL_IMPORT
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
  target_compile_options(Workspace PRIVATE -Wno-changes-meaning)
endif()

# Flat Bin/ output. See ToolKit/CMakeLists.txt for why the $<0:>
# trailer is here: it strips the multi-config generator's
# $<CONFIGURATION> suffix so Workspace.lib lands at Bin/ instead
# of Bin/Debug/. RUNTIME_OUTPUT_DIRECTORY is set alongside for
# consistency, even though Workspace is a STATIC library and
# does not produce a runtime artifact.
set_target_properties(Workspace PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  LIBRARY_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
  ARCHIVE_OUTPUT_DIRECTORY "${GDTK_BIN_DIR}/$<0:>"
)
