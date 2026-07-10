# Wrapper for the prebuilt tkimgui / imgui shared library.
#
# Source: Dependency/Config/imgui-config.cmake
# Staged: <deps>/<Config>/imgui/imguiConfig.cmake by build_dependencies.py
#
# Declares the `imgui` IMPORTED target with per-config IMPORTED_LOCATION_<CFG>
# entries for every config that is actually present under
# <deps>/<Platform>/<Config>/. Dependency/CMakeLists.txt forces
# BUILD_SHARED_LIBS=ON for tkimgui, so on Linux the artifact is libimgui<d>.so
# and on Windows it is imgui<d>.lib (+ imgui<d>.dll runtime staged by
# Dependency/CMakeLists.txt's CopyDependencies).
#
# The include-path is provided by the `tkimgui` INTERFACE library in the
# root CMakeLists; this config file only declares the link artifact.

get_filename_component(_TK_DEP_PLAT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)

add_library(imgui UNKNOWN IMPORTED)
set(_TK_CONFIGS "")
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
  if(_cfg STREQUAL "Debug")
    set(_sfx "d")
  else()
    set(_sfx "")
  endif()
  if(UNIX)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/libimgui${_sfx}.so")
  elseif(WIN32)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/imgui${_sfx}.lib")
  endif()
  if(EXISTS "${_f}")
    string(TOUPPER "${_cfg}" _cfgu)
    set_target_properties(imgui PROPERTIES IMPORTED_LOCATION_${_cfgu} "${_f}")
    list(APPEND _TK_CONFIGS "${_cfg}")
  endif()
endforeach()

if(NOT _TK_CONFIGS)
  message(FATAL_ERROR
    "imgui prebuild artifact not found under '${_TK_DEP_PLAT_DIR}/<Config>/'.\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>\n"
    "(do NOT pass --skip-imgui if you want to build the Editor / Launcher)")
endif()

list(JOIN _TK_CONFIGS ";" _TK_CONFIGS_STR)
set_target_properties(imgui PROPERTIES IMPORTED_CONFIGURATIONS "${_TK_CONFIGS_STR}")

set(imgui_FOUND TRUE)
