# Wrapper for the prebuilt tkimgui / imgui shared library.
#
# Source: Dependency/Config/imgui-config.cmake
# Staged: <deps>/cmake/imgui-config.cmake by build_dependencies.py
#
# Declares the `imgui` IMPORTED target. Dependency/CMakeLists.txt
# forces BUILD_SHARED_LIBS=ON for tkimgui, so on Linux the artifact
# is libimgui<d>.so and on Windows it is imgui<d>.lib (+ imgui<d>.dll
# runtime staged by Dependency/CMakeLists.txt's CopyDependencies).
#
# Note: the include path is provided by the `tkimgui` INTERFACE
# library in the root CMakeLists -- this config file only declares
# the link artifact.

get_filename_component(_TK_DEPS_DIR
  "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEPS_DIR
  "${_TK_DEPS_DIR}" DIRECTORY)

set(_TK_SUFFIX "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_TK_SUFFIX "d")
endif()

if(UNIX)
  set(_TK_FILE "${_TK_DEPS_DIR}/libimgui${_TK_SUFFIX}.so")
elseif(WIN32)
  set(_TK_FILE "${_TK_DEPS_DIR}/imgui${_TK_SUFFIX}.lib")
endif()

if(NOT EXISTS "${_TK_FILE}")
  message(FATAL_ERROR
    "imgui prebuild artifact not found: ${_TK_FILE}\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>\n"
    "(do NOT pass --skip-imgui if you want to build the Editor / Launcher)")
endif()

add_library(imgui UNKNOWN IMPORTED)
set_target_properties(imgui PROPERTIES
  IMPORTED_LOCATION "${_TK_FILE}"
)

set(imgui_FOUND TRUE)