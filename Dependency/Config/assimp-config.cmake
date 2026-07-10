# Wrapper for the prebuilt assimp shared library.
#
# Source: Dependency/Config/assimp-config.cmake
# Staged: <deps>/<Config>/assimp/assimpConfig.cmake by build_dependencies.py
#
# Declares the `assimp` IMPORTED target with per-config IMPORTED_LOCATION_<CFG>
# entries for every config that is actually present under
# <deps>/<Platform>/<Config>/. Only built when the dependency script runs
# without --skip-assimp; the Import tool links it directly. On Linux the
# runtime library is libassimp<d>.so (with the SONAME chain alongside); on
# Windows the import library is assimp<d>.lib and the runtime assimp<d>.dll
# is staged next to the Import binary by Import/CMakeLists.txt's POST_BUILD.

get_filename_component(_TK_DEP_PLAT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)

add_library(assimp UNKNOWN IMPORTED)
set(_TK_CONFIGS "")
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
  if(_cfg STREQUAL "Debug")
    set(_sfx "d")
  else()
    set(_sfx "")
  endif()
  if(UNIX)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/libassimp${_sfx}.so")
  elseif(WIN32)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/assimp${_sfx}.lib")
  endif()
  if(EXISTS "${_f}")
    string(TOUPPER "${_cfg}" _cfgu)
    set_target_properties(assimp PROPERTIES IMPORTED_LOCATION_${_cfgu} "${_f}")
    list(APPEND _TK_CONFIGS "${_cfg}")
  endif()
endforeach()

if(NOT _TK_CONFIGS)
  message(FATAL_ERROR
    "assimp prebuild artifact not found under '${_TK_DEP_PLAT_DIR}/<Config>/'.\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>\n"
    "(do NOT pass --skip-assimp if you want to build the Import tool)")
endif()

list(JOIN _TK_CONFIGS ";" _TK_CONFIGS_STR)
set_target_properties(assimp PROPERTIES IMPORTED_CONFIGURATIONS "${_TK_CONFIGS_STR}")

set(assimp_FOUND TRUE)
