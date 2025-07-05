cmake_minimum_required(VERSION 3.6)

# ToolKit App/Game CMake file.
set(PLUGIN_PROJECT_NAME "__projectname__" CACHE STRING "What is the project name ?")

set(PLUGIN_CODE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(PLUGIN_BIN_DIR "${PLUGIN_CODE_DIR}/Bin")

# Get ToolKit directory.
if(DEFINED ENV{APPDATA})
    set(CFG_FILE_PATH "$ENV{APPDATA}/ToolKit/Config/Path.txt")

    if(NOT EXISTS ${CFG_FILE_PATH})
        message(SEND_ERROR "Set TOOLKIT_DIR or provide ${CFG_FILE_PATH} containing path to ToolKit.")
        return()
    else()
        file(STRINGS ${CFG_FILE_PATH} TOOLKIT_DIR)
    endif()
endif()

if(NOT EXISTS ${TOOLKIT_DIR}/ToolKit/ToolKit.h)
    message(SEND_ERROR "Please use Forward slashes instead of backslashes. Can't find ToolKit.h in ${TOOLKIT_DIR}")
    return()
endif()

# Create the project.
project (${PLUGIN_PROJECT_NAME})

if(CMAKE_BUILD_TYPE)
	set(TK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
	set(TK_BUILD_TYPE "$<CONFIG>")
endif()

set(TK_PLATFORM "Windows")
set(CMAKE_CXX_STANDARD 17) # Set C++ standard.

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
    "${PLUGIN_CODE_DIR}")

###############################
# Project Source Files Begin. #
###############################
set(SOURCE 
    PluginMain.cpp)

set(HEADERS 
    PluginMain.h)
###############################
# Project Source Files End.   #
###############################

# GLM definitions.
add_definitions(-DTK_GLM)
add_definitions(-DGLM_FORCE_QUAT_DATA_XYZW)
add_definitions(-DGLM_FORCE_XYZW_ONLY)
add_definitions(-DGLM_FORCE_CTOR_INIT)
add_definitions(-DGLM_ENABLE_EXPERIMENTAL)
add_definitions(-DGLM_FORCE_ALIGNED_GENTYPES)
add_definitions(-DGLM_FORCE_INTRINSICS)

# Set lib names.
set(toolkit "ToolKit$<$<CONFIG:Debug>:d>")
set(imgui "imgui$<$<CONFIG:Debug>:d>")
set(editor "Editor$<$<CONFIG:Debug>:d>")

# Dependency library directories.
set(TK_DEPENDECY_OUT_DIR "${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${TK_BUILD_TYPE}")
set(TK_OUT_DIR "${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit/ToolKit/${TK_BUILD_TYPE}")

message("Using dependency output directory: ${TK_DEPENDECY_OUT_DIR}")
message("Using toolkit output directory: ${TK_OUT_DIR}")

# Plugin build for editor.
set(CMAKE_DEBUG_POSTFIX d) # Only editor needs to know the postfix for loading correct version of the plugin.
add_library(${PLUGIN_PROJECT_NAME} SHARED ${SOURCE} ${HEADERS}) # Plugin must be a dll.

target_link_directories(${PLUGIN_PROJECT_NAME} PRIVATE "${TOOLKIT_DIR}/Bin")
target_link_directories(${PLUGIN_PROJECT_NAME} PRIVATE "${TK_DEPENDECY_OUT_DIR}")
target_link_libraries(${PLUGIN_PROJECT_NAME} PRIVATE ${toolkit} ${imgui} ${editor})
target_compile_definitions(${PLUGIN_PROJECT_NAME} PRIVATE TK_WITH_EDITOR TK_DLL_IMPORT)

# Set precompiled header files for the project.
set(PCH
  "${TOOLKIT_DIR}/ToolKit/stdafx.h"
  "${TOOLKIT_DIR}/ToolKit/Types.h")

target_precompile_headers(${PLUGIN_PROJECT_NAME} PRIVATE ${PCH})

# Set TK_DEBUG for toolkit.
target_compile_definitions(${PLUGIN_PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:TK_DEBUG>)

# Place artifacts strictly in the bin.
# Packer picks them from here to create publishable packages.
set_target_properties(${PLUGIN_PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PLUGIN_BIN_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${PLUGIN_BIN_DIR}
)

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(isMultiConfig)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config_upper)
        set_target_properties(${PLUGIN_PROJECT_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
            PDB_OUTPUT_DIRECTORY_${config_upper} ${PLUGIN_BIN_DIR}
        )
    endforeach()
endif()

# MSVC specific options.
if (MSVC)
    target_link_options(${PLUGIN_PROJECT_NAME} PRIVATE $<$<NOT:$<CONFIG:DEBUG>>:/INCREMENTAL:NO> $<$<NOT:$<CONFIG:DEBUG>>:/LTCG>)
endif()

#For hot reload rename the previous output.
set(DLL $<SHELL_PATH:$<TARGET_FILE:${PLUGIN_PROJECT_NAME}>>)
set(PDB $<SHELL_PATH:$<TARGET_PDB_FILE:${PLUGIN_PROJECT_NAME}>>)

add_custom_command(TARGET ${PLUGIN_PROJECT_NAME} PRE_BUILD COMMAND ${CMAKE_COMMAND} -Ddll=${DLL} -Dpdb=${PDB} -Dtmp="${PLUGIN_BIN_DIR}/tmp" -P "${PLUGIN_CODE_DIR}/CMakeHotReload.cmake")
