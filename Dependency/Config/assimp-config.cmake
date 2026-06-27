# Wrapper for the prebuilt assimp shared library.
#
# Source: Dependency/Config/assimp-config.cmake
# Staged: <deps>/cmake/assimp-config.cmake by build_dependencies.py
#
# Declares the `assimp` IMPORTED target. Only built when the
# dependency script runs without --skip-assimp; the Import tool
# links it directly. On Linux the runtime library is libassimp<d>.so
# (with the SONAME chain libassimp<d>.so.6 / .so.6.0.2 alongside);
# on Windows the import library is assimp<d>.lib and the runtime
# assimp<d>.dll is staged next to the Import binary by
# Import/CMakeLists.txt's POST_BUILD.

get_filename_component(_TK_DEPS_DIR
  "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEPS_DIR
  "${_TK_DEPS_DIR}" DIRECTORY)

set(_TK_SUFFIX "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_TK_SUFFIX "d")
endif()

if(UNIX)
  set(_TK_FILE "${_TK_DEPS_DIR}/libassimp${_TK_SUFFIX}.so")
elseif(WIN32)
  set(_TK_FILE "${_TK_DEPS_DIR}/assimp${_TK_SUFFIX}.lib")
endif()

if(NOT EXISTS "${_TK_FILE}")
  message(FATAL_ERROR
    "assimp prebuild artifact not found: ${_TK_FILE}\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>\n"
    "(do NOT pass --skip-assimp if you want to build the Import tool)")
endif()

add_library(assimp UNKNOWN IMPORTED)
set_target_properties(assimp PROPERTIES
  IMPORTED_LOCATION "${_TK_FILE}"
)

set(assimp_FOUND TRUE)