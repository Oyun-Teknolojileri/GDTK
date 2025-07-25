cmake_minimum_required(VERSION 3.6)

# ToolKit App/Game CMake file.
set(GAME_PROJECT_NAME "__projectname__" CACHE STRING "What is the project name ?")

set(GAME_CODE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(GAME_BIN_DIR "${GAME_CODE_DIR}/Bin")

# Create the project.
project (${GAME_PROJECT_NAME})

# Log build details.
string(TOLOWER ${CMAKE_CXX_COMPILER_ID} TK_COMPILER)
message("Compiling ${GAME_PROJECT_NAME} with ${TK_COMPILER} for platform ${TK_PLATFORM} with configuration ${TK_BUILD_TYPE}")

# Add include directories for ToolKit.
include_directories(
    "${TOOLKIT_DIR}"
    "${TOOLKIT_DIR}/ToolKit"
    "${TOOLKIT_DIR}/Dependency"
    "${TOOLKIT_DIR}/Dependency/glm"
    "${TOOLKIT_DIR}/Dependency/glad"
    "${TOOLKIT_DIR}/Dependency/SDL2/include"
    "${TOOLKIT_DIR}/Dependency/RapidXml"
    "${TOOLKIT_DIR}/Dependency/stb"
    "${TOOLKIT_DIR}/Dependency/minizip-ng/dist/include"
    "${TOOLKIT_DIR}/Dependency/tkimgui"
    "${GAME_CODE_DIR}")

###############################
# Project Source Files Begin. #
###############################
set(SOURCE 
    Game.cpp)

set(HEADERS 
    Game.h)
###############################
# Project Source Files End.   #
###############################

# If the build is for publishing to a platform, main file is needed.
if (NOT ${PLUGIN_BUILD})
    set(SOURCE ${SOURCE} ${GAME_CODE_DIR}/Main.cpp)
endif()

# GLM definitions.
add_definitions(-DTK_GLM)
add_definitions(-DGLM_FORCE_QUAT_DATA_XYZW)
add_definitions(-DGLM_FORCE_XYZW_ONLY)
add_definitions(-DGLM_FORCE_CTOR_INIT)
add_definitions(-DGLM_ENABLE_EXPERIMENTAL)
add_definitions(-DGLM_FORCE_ALIGNED_GENTYPES)
add_definitions(-DGLM_FORCE_INTRINSICS)

# If extra c++ flags are provided merge them.
if (NOT TK_CXX_EXTRA STREQUAL "")
    set(TK_CXX_FLAGS "${TK_CXX_FLAGS} ${TK_CXX_EXTRA}")
endif()

# Set lib names.
set(toolkit "ToolKit$<$<CONFIG:Debug>:d>")
set(minizip "minizip$<$<CONFIG:Debug>:d>")
set(sdl2 "SDL2$<$<CONFIG:Debug>:d>")
set(sdl2main "SDL2main$<$<CONFIG:Debug>:d>")

if (NOT "${TK_PLATFORM}" STREQUAL "${TK_WINDOWS}")
    set(zstd "zstd$<$<CONFIG:Debug>:d>")
else()
    set(zstd "zstd_static$<$<CONFIG:Debug>:d>")
endif()

# Dependency library directories.
set(TK_DEPENDECY_OUT_DIR "${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${TK_BUILD_TYPE}")
set(TK_OUT_DIR "${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit/ToolKit/${TK_BUILD_TYPE}")

message("Using dependency output directory: ${TK_DEPENDECY_OUT_DIR}")
message("Using toolkit output directory: ${TK_OUT_DIR}")

# Web build project.
if ("${TK_PLATFORM}" STREQUAL "${TK_WEB}")
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    add_executable(${GAME_PROJECT_NAME} ${SOURCE} ${HEADERS})

    target_link_directories(${GAME_PROJECT_NAME} PRIVATE ${TK_DEPENDECY_OUT_DIR})
    target_link_directories(${GAME_PROJECT_NAME} PRIVATE ${TK_OUT_DIR})
    target_link_libraries(${GAME_PROJECT_NAME} PRIVATE ${minizip} ${zstd} ToolKit) 

    # Use sdl2 from emscripten.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TK_CXX_FLAGS} -s USE_SDL=2")

    set(SHELL_FILE_FLAG "--shell-file ${GAME_BIN_DIR}/shell_minimal.html")
    set(PRELOAD_DMP_FLAG "--preload-file ${GAME_CODE_DIR}/../Config/Engine.settings@Resources/Dmp/Engine.settings") # This is to make sure Resource folder created for web build.
    set(PRELOAD_MINRES_FLAG "--preload-file ${GAME_CODE_DIR}/../MinResources.pak@MinResources.pak")
    set(PRELOAD_CONFIG_FLAG "--preload-file ${GAME_CODE_DIR}/../Config@Config")
    set(WEBGL_FLAGS "-sALLOW_MEMORY_GROWTH=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3 -sSTACK_SIZE=131072")
    set(OUTPUT_FLAG "-o ${PROJECT_NAME}.html")

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OUTPUT_FLAG} ${SHELL_FILE_FLAG} ${PRELOAD_DMP_FLAG} ${PRELOAD_MINRES_FLAG} ${PRELOAD_CONFIG_FLAG} ${WEBGL_FLAGS}")
endif()

# Android build project.
if ("${TK_PLATFORM}" STREQUAL "${TK_ANDROID}")
    # Reset game project name to main if it is android build, sdl expect a libmain.so
    set(GAME_PROJECT_NAME "main")
    add_library(${GAME_PROJECT_NAME} SHARED ${SOURCE} ${HEADERS})
    
    # Add library search directories for Android
    target_link_directories(${GAME_PROJECT_NAME} PRIVATE ${TK_DEPENDECY_OUT_DIR})
    target_link_directories(${GAME_PROJECT_NAME} PRIVATE ${TK_OUT_DIR})
    
    find_library(GLESv3 GLESv3)
    target_link_libraries(${GAME_PROJECT_NAME} PRIVATE GLESv3 jnigraphics android log SDL2 SDL2main minizip libzstd ToolKit)
endif()

# Windows build project.
if ("${TK_PLATFORM}" STREQUAL "${TK_WINDOWS}")
    add_executable(${GAME_PROJECT_NAME} WIN32 ${SOURCE} ${HEADERS})
    
    target_link_directories(${GAME_PROJECT_NAME} PRIVATE ${TK_DEPENDECY_OUT_DIR})
    target_link_directories(${GAME_PROJECT_NAME} PRIVATE ${TK_OUT_DIR})
    target_link_libraries(${GAME_PROJECT_NAME} PRIVATE OpenGL32 ${sdl2} ${sdl2main} ${minizip} ${zstd} ToolKit)
endif()

# Plugin build for editor.
if (${PLUGIN_BUILD})
    set(CMAKE_DEBUG_POSTFIX d) # Only editor needs to know the postfix for loading correct version of the plugin.
    add_library(${GAME_PROJECT_NAME} SHARED ${SOURCE} ${HEADERS}) # Plugin must be a dll.

    target_link_directories(${GAME_PROJECT_NAME} PRIVATE "${TOOLKIT_DIR}/Bin")
    target_link_libraries(${GAME_PROJECT_NAME} PRIVATE ${toolkit})
    target_compile_definitions(${GAME_PROJECT_NAME} PRIVATE TK_WITH_EDITOR TK_DLL_IMPORT)
endif()

# Set precompiled header files for the project.
set(PCH
  "${TOOLKIT_DIR}/ToolKit/stdafx.h"
  "${TOOLKIT_DIR}/ToolKit/Types.h")

  target_precompile_headers(${GAME_PROJECT_NAME} PRIVATE ${PCH})

# Set TK_DEBUG for toolkit.
target_compile_definitions(${GAME_PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:TK_DEBUG>)

# Place artifacts strictly in the bin.
# Packer picks them from here to create publishable packages.
set_target_properties(${GAME_PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GAME_BIN_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${GAME_BIN_DIR}
)

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(isMultiConfig)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config_upper)
        set_target_properties(${GAME_PROJECT_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${GAME_BIN_DIR}
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} ${GAME_BIN_DIR}
            PDB_OUTPUT_DIRECTORY_${config_upper} ${GAME_BIN_DIR}
        )
    endforeach()
endif()

# MSVC specific options.
if (MSVC)
    target_link_options(${GAME_PROJECT_NAME} PRIVATE $<$<NOT:$<CONFIG:DEBUG>>:/INCREMENTAL:NO> $<$<NOT:$<CONFIG:DEBUG>>:/LTCG>)
endif()

#For hot reload rename the previous output.
if (${PLUGIN_BUILD})
    set(DLL $<SHELL_PATH:$<TARGET_FILE:${GAME_PROJECT_NAME}>>)
    set(PDB $<SHELL_PATH:$<TARGET_PDB_FILE:${GAME_PROJECT_NAME}>>)

    add_custom_command(TARGET ${GAME_PROJECT_NAME} PRE_BUILD COMMAND ${CMAKE_COMMAND} -Ddll=${DLL} -Dpdb=${PDB} -Dtmp="${GAME_BIN_DIR}/tmp" -P "${GAME_CODE_DIR}/CMakeHotReload.cmake")
endif()
