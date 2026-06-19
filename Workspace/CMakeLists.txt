# Workspace -- small static library that holds project-management helpers
# shared by the Editor and the Launcher. Compiled into both executables.

add_library(Workspace STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Workspace.cpp
)

target_include_directories(Workspace PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
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

# GCC 16 enables -Wchanges-meaning by default. Pass.h has a member named
# 'Entity' whose type is also 'Entity', which trips it. The legacy MSVC
# build was clean; silence the warning here.
target_compile_options(Workspace PRIVATE -Wno-changes-meaning)

# No PCH -- the file count is too small to be worth the bookkeeping.
