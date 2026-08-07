# Wrapper for the prebuilt SDL2 shared library.
#
# Source: Dependency/Config/SDL2-config.cmake
# Staged: <deps>/<Config>/SDL2/SDL2Config.cmake by build_dependencies.py
#
# Declares the `SDL2` IMPORTED target with per-config IMPORTED_LOCATION_<CFG>
# entries for every config that is actually present under
# <deps>/<Platform>/<Config>/. Per-config (rather than one CMAKE_BUILD_TYPE-
# derived location) makes the target resolve correctly under BOTH single-config
# (Make/Ninja) and multi-config (Visual Studio) generators, independent of
# which config's wrapper find_package happened to discover or what SDL2_DIR
# is cached -- a Debug-then-Release reconfigure (or a stale cache) can no
# longer pin a Release build to the debug library.
#
# On Linux the SONAME-encoded link name is libSDL2-2.0<d>.so; on Windows the
# import library is SDL2<d>.lib (the runtime SDL2<d>.dll is staged next to the
# binary by Dependency/CMakeLists.txt's CopyDependencies target).

# This wrapper lives at <deps>/<Platform>/<Config>/SDL2/SDL2Config.cmake.
# Three DIRECTORY pops reach <deps>/<Platform> -- the root under which each
# config has its own <Config>/ subdir holding the prebuilt artifacts.
get_filename_component(_TK_DEP_PLAT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)

add_library(SDL2 UNKNOWN IMPORTED)
set(_TK_CONFIGS "")
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
  if(_cfg STREQUAL "Debug")
    set(_sfx "d")
  else()
    set(_sfx "")
  endif()
  if(UNIX)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/libSDL2-2.0${_sfx}.so")
  elseif(WIN32)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/SDL2${_sfx}.lib")
  endif()
  if(EXISTS "${_f}")
    string(TOUPPER "${_cfg}" _cfgu)
    set_target_properties(SDL2 PROPERTIES IMPORTED_LOCATION_${_cfgu} "${_f}")
    list(APPEND _TK_CONFIGS "${_cfg}")
  endif()
endforeach()

if(NOT _TK_CONFIGS)
  message(FATAL_ERROR
    "SDL2 prebuild artifact not found under '${_TK_DEP_PLAT_DIR}/<Config>/'.\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>")
endif()

list(JOIN _TK_CONFIGS ";" _TK_CONFIGS_STR)
set_target_properties(SDL2 PROPERTIES IMPORTED_CONFIGURATIONS "${_TK_CONFIGS_STR}")

set(SDL2_FOUND TRUE)
