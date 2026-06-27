# Wrapper for the prebuilt SDL2 shared library.
#
# Source: Dependency/Config/SDL2-config.cmake
# Staged: <deps>/cmake/SDL2-config.cmake by build_dependencies.py
#
# Declares the `SDL2` IMPORTED target. On Linux the SONAME-encoded
# filename is libSDL2-2.0<d>.so; on Windows the import library is
# SDL2<d>.lib (the runtime SDL2<d>..dll is staged next to the binary
# by Dependency/CMakeLists.txt's CopyDependencies target).

get_filename_component(_TK_DEPS_DIR
  "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEPS_DIR
  "${_TK_DEPS_DIR}" DIRECTORY)

set(_TK_SUFFIX "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_TK_SUFFIX "d")
endif()

if(UNIX)
  set(_TK_FILE "${_TK_DEPS_DIR}/libSDL2-2.0${_TK_SUFFIX}.so")
elseif(WIN32)
  set(_TK_FILE "${_TK_DEPS_DIR}/SDL2${_TK_SUFFIX}.lib")
endif()

if(NOT EXISTS "${_TK_FILE}")
  message(FATAL_ERROR
    "SDL2 prebuild artifact not found: ${_TK_FILE}\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>")
endif()

add_library(SDL2 UNKNOWN IMPORTED)
set_target_properties(SDL2 PROPERTIES
  IMPORTED_LOCATION "${_TK_FILE}"
)

set(SDL2_FOUND TRUE)