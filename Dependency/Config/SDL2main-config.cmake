# Wrapper for the prebuilt SDL2main static library.
#
# Source: Dependency/Config/SDL2main-config.cmake
# Staged: <deps>/cmake/SDL2main-config.cmake by build_dependencies.py
#
# Declares the `SDL2main` IMPORTED target. SDL2main is the entry-point
# shim that SDL2 ships for Windows-style `int main(int, char**)`
# dispatching. On Linux it lives in libSDL2main<d>.a; on Windows in
# SDL2main<d>.lib (no "lib" prefix on Windows static libs by default).

get_filename_component(_TK_DEPS_DIR
  "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEPS_DIR
  "${_TK_DEPS_DIR}" DIRECTORY)

set(_TK_SUFFIX "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_TK_SUFFIX "d")
endif()

if(UNIX)
  set(_TK_FILE "${_TK_DEPS_DIR}/libSDL2main${_TK_SUFFIX}.a")
elseif(WIN32)
  set(_TK_FILE "${_TK_DEPS_DIR}/SDL2main${_TK_SUFFIX}.lib")
endif()

if(NOT EXISTS "${_TK_FILE}")
  message(FATAL_ERROR
    "SDL2main prebuild artifact not found: ${_TK_FILE}\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>")
endif()

add_library(SDL2main UNKNOWN IMPORTED)
set_target_properties(SDL2main PROPERTIES
  IMPORTED_LOCATION "${_TK_FILE}"
)

set(SDL2main_FOUND TRUE)