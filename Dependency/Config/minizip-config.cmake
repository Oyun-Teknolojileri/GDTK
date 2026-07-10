# Wrapper for the prebuilt minizip-ng static library.
#
# Source: Dependency/Config/minizip-config.cmake
# Staged: <deps>/<Config>/minizip/minizipConfig.cmake by build_dependencies.py
#
# Declares the `minizip` IMPORTED target with per-config IMPORTED_LOCATION_<CFG>
# entries for every config that is actually present under
# <deps>/<Platform>/<Config>/. On Linux the artifact is libminizip<d>.a; on
# Windows it is minizip<d>.lib.

get_filename_component(_TK_DEP_PLAT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)

add_library(minizip UNKNOWN IMPORTED)
set(_TK_CONFIGS "")
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
  if(_cfg STREQUAL "Debug")
    set(_sfx "d")
  else()
    set(_sfx "")
  endif()
  if(UNIX)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/libminizip${_sfx}.a")
  elseif(WIN32)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/minizip${_sfx}.lib")
  endif()
  if(EXISTS "${_f}")
    string(TOUPPER "${_cfg}" _cfgu)
    set_target_properties(minizip PROPERTIES IMPORTED_LOCATION_${_cfgu} "${_f}")
    list(APPEND _TK_CONFIGS "${_cfg}")
  endif()
endforeach()

if(NOT _TK_CONFIGS)
  message(FATAL_ERROR
    "minizip prebuild artifact not found under '${_TK_DEP_PLAT_DIR}/<Config>/'.\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>")
endif()

list(JOIN _TK_CONFIGS ";" _TK_CONFIGS_STR)
set_target_properties(minizip PROPERTIES IMPORTED_CONFIGURATIONS "${_TK_CONFIGS_STR}")

set(minizip_FOUND TRUE)
