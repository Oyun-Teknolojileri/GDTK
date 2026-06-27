# Wrapper for the prebuilt minizip-ng static library.
#
# Source: Dependency/Config/minizip-config.cmake
# Staged: <deps>/cmake/minizip-config.cmake by build_dependencies.py
#
# Declares the `minizip` IMPORTED target pointing at the prebuilt
# libminizip<d>.a (Linux) or minizip<d>.lib (Windows) artifact.
# The Debug suffix is decided here from CMAKE_BUILD_TYPE -- callers
# never have to think about per-platform or per-config naming.

# Resolve the prebuild root from this file's location.
#   <deps>/cmake/minizip-config.cmake
#   <deps>/                     <- _TK_DEPS_DIR
get_filename_component(_TK_DEPS_DIR
  "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEPS_DIR
  "${_TK_DEPS_DIR}" DIRECTORY)

# Multi-config generators (VS) leave CMAKE_BUILD_TYPE empty at
# configure time; default to Release in that case. Debug-only trees
# still work because the artifact actually present on disk will be
# the Debug one and the fallback probing in tk_add_imported_resolved
# (legacy) handled this -- but here we commit to one suffix per
# configure. To switch configs, re-configure.
set(_TK_SUFFIX "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_TK_SUFFIX "d")
endif()

# Static library naming.
if(UNIX)
  set(_TK_FILE "${_TK_DEPS_DIR}/libminizip${_TK_SUFFIX}.a")
elseif(WIN32)
  set(_TK_FILE "${_TK_DEPS_DIR}/minizip${_TK_SUFFIX}.lib")
endif()

if(NOT EXISTS "${_TK_FILE}")
  message(FATAL_ERROR
    "minizip prebuild artifact not found: ${_TK_FILE}\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>")
endif()

add_library(minizip UNKNOWN IMPORTED)
set_target_properties(minizip PROPERTIES
  IMPORTED_LOCATION "${_TK_FILE}"
)

set(minizip_FOUND TRUE)