# Wrapper for the prebuilt SDL2main static library.
#
# Source: Dependency/Config/SDL2main-config.cmake
# Staged: <deps>/<Config>/SDL2main/SDL2mainConfig.cmake by build_dependencies.py
#
# Declares the `SDL2main` IMPORTED target with per-config IMPORTED_LOCATION_<CFG>
# entries for every config that is actually present under
# <deps>/<Platform>/<Config>/. SDL2main is the entry-point shim that SDL2 ships
# for Windows-style `int main(int, char**)` dispatching. On Linux the artifact
# is libSDL2main<d>.a; on Windows it is SDL2main<d>.lib.

get_filename_component(_TK_DEP_PLAT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)

add_library(SDL2main UNKNOWN IMPORTED)
set(_TK_CONFIGS "")
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
  if(_cfg STREQUAL "Debug")
    set(_sfx "d")
  else()
    set(_sfx "")
  endif()
  if(UNIX)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/libSDL2main${_sfx}.a")
  elseif(WIN32)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/SDL2main${_sfx}.lib")
  endif()
  if(EXISTS "${_f}")
    string(TOUPPER "${_cfg}" _cfgu)
    set_target_properties(SDL2main PROPERTIES IMPORTED_LOCATION_${_cfgu} "${_f}")
    list(APPEND _TK_CONFIGS "${_cfg}")
  endif()
endforeach()

if(NOT _TK_CONFIGS)
  message(FATAL_ERROR
    "SDL2main prebuild artifact not found under '${_TK_DEP_PLAT_DIR}/<Config>/'.\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>")
endif()

list(JOIN _TK_CONFIGS ";" _TK_CONFIGS_STR)
set_target_properties(SDL2main PROPERTIES IMPORTED_CONFIGURATIONS "${_TK_CONFIGS_STR}")

set(SDL2main_FOUND TRUE)
