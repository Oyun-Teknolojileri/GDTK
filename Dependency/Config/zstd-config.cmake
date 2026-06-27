# Wrapper for the prebuilt zstd static library.
#
# Source: Dependency/Config/zstd-config.cmake
# Staged: <deps>/cmake/zstd-config.cmake by build_dependencies.py
#
# Declares the `zstd` IMPORTED target. On Linux the artifact is
# libzstd<d>.a, but on Windows zstd's upstream CMake target uses the
# zstd_static<d>.lib naming (the "static" infix is zstd's quirk --
# not a GDTK convention). All of that platform/detail knowledge is
# hidden inside this file.

get_filename_component(_TK_DEPS_DIR
  "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEPS_DIR
  "${_TK_DEPS_DIR}" DIRECTORY)

set(_TK_SUFFIX "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_TK_SUFFIX "d")
endif()

if(UNIX)
  set(_TK_FILE "${_TK_DEPS_DIR}/libzstd${_TK_SUFFIX}.a")
elseif(WIN32)
  # zstd's upstream static target name carries the "static" infix.
  set(_TK_FILE "${_TK_DEPS_DIR}/zstd_static${_TK_SUFFIX}.lib")
endif()

if(NOT EXISTS "${_TK_FILE}")
  message(FATAL_ERROR
    "zstd prebuild artifact not found: ${_TK_FILE}\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>")
endif()

add_library(zstd UNKNOWN IMPORTED)
set_target_properties(zstd PROPERTIES
  IMPORTED_LOCATION "${_TK_FILE}"
)

set(zstd_FOUND TRUE)