# Wrapper for the prebuilt Basis Universal library.
#
# Source: Dependency/Config/basisu-config.cmake
# Staged: <deps>/<Cfg>/basisu/basisuConfig.cmake by build_dependencies.py
#
# Desktop builds one archive, `basisu_encoder`, which already contains
# transcoder/basisu_transcoder.cpp (upstream basis_universal/CMakeLists.txt:259).
# The engine therefore links this single target for both the transcoder (KTX2
# reading) and the encoder (KTX2 writing) and cannot end up with two copies of
# the basist symbols. Web/Android never import this: their transcoder-only
# target is created directly in Dependency/CMakeLists.txt.
#
# BASISD_SUPPORT_KTX2_ZSTD=0 is part of the INTERFACE definitions on purpose.
# The macro is read by transcoder/basisu_transcoder.h, so a consumer translation
# unit that saw the header default (1) while the archive was compiled with 0
# would disagree with the library about the KTX2 class layout. The zstd copy
# itself is left out because ToolKit already links zstd through minizip (see the
# desktop basisu block in Dependency/CMakeLists.txt).
#
# The include directories point back into the submodule: basisu ships no
# install rule, and the headers are what make the archive usable at all.

get_filename_component(_TK_DEP_PLAT_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)
get_filename_component(_TK_DEP_PLAT_DIR "${_TK_DEP_PLAT_DIR}" DIRECTORY)

if(NOT _TK_BASISU_SRC)
  set(_TK_BASISU_SRC "${_TK_DEP_PLAT_DIR}/../../basis_universal")
endif()

add_library(basisu_encoder UNKNOWN IMPORTED)

set(_TK_CONFIGS "")
foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
  if(_cfg STREQUAL "Debug")
    set(_sfx "d")
  else()
    set(_sfx "")
  endif()
  if(UNIX)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/libbasisu_encoder${_sfx}.a")
  elseif(WIN32)
    set(_f "${_TK_DEP_PLAT_DIR}/${_cfg}/basisu_encoder${_sfx}.lib")
  endif()
  if(EXISTS "${_f}")
    string(TOUPPER "${_cfg}" _cfgu)
    set_target_properties(basisu_encoder PROPERTIES IMPORTED_LOCATION_${_cfgu} "${_f}")
    list(APPEND _TK_CONFIGS "${_cfg}")
  endif()
endforeach()

if(NOT _TK_CONFIGS)
  message(FATAL_ERROR
    "basisu prebuild artifact not found under '${_TK_DEP_PLAT_DIR}/<Config>/'.\n"
    "Run: python3 BuildScripts/build_dependencies.py --configs <Debug|Release>")
endif()

list(JOIN _TK_CONFIGS ";" _TK_CONFIGS_STR)
set_target_properties(basisu_encoder PROPERTIES
  IMPORTED_CONFIGURATIONS "${_TK_CONFIGS_STR}"
  # encoder/ is on the include path because TextureCompression.cpp includes basisu_enc.h; the
  # transcoder directory is what Ktx2Texture.cpp needs.
  INTERFACE_INCLUDE_DIRECTORIES "${_TK_BASISU_SRC};${_TK_BASISU_SRC}/transcoder;${_TK_BASISU_SRC}/encoder"
  INTERFACE_COMPILE_DEFINITIONS "BASISD_SUPPORT_KTX2=1;BASISD_SUPPORT_KTX2_ZSTD=0"
)

if(NOT EXISTS "${_TK_BASISU_SRC}/transcoder/basisu_transcoder.h")
  message(FATAL_ERROR
    "basisu headers not found at '${_TK_BASISU_SRC}'.\n"
    "The submodule is missing: git submodule update --init Dependency/basis_universal")
endif()

set(basisu_FOUND TRUE)
