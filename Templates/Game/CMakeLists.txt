cmake_minimum_required(VERSION 3.6)

# Master android cmake for toolkit projects.
project(ToolKitApp)

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

set(PLUGIN_BUILD FALSE)
if (NOT DEFINED TK_PLATFORM)
    set(PLUGIN_BUILD TRUE)
    message("Preparing for plugin build.")
endif()

# Provide platform definitions
set(TK_ANDROID "Android")
set(TK_WINDOWS "Windows")
set(TK_WEB "Web")

if(CMAKE_BUILD_TYPE)
	set(TK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
	set(TK_BUILD_TYPE "$<CONFIG>")
endif()

# Check if the compiler is Clang.
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CLANG_COMPILER TRUE)
    set(CMAKE_CXX_STANDARD 17) # Set C++ standard.
    add_compile_options(-w) # All warnings off for Clang
else()
    set(CLANG_COMPILER FALSE)
endif()

# Check if the compiler is MSVC
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(MSVC_COMPILER TRUE)
    set(CMAKE_CXX_STANDARD 17) # Set C++ standard.
else()
    set(MSVC_COMPILER FALSE)
endif()

if (NOT MSVC_COMPILER AND NOT CLANG_COMPILER)
    message(SEND_ERROR "This project requires MSVC or Clang compiler to build, found ${CMAKE_CXX_COMPILER_ID}. Please use a compatible compiler.")
    return()
endif()

# ToolKit Engine
if (NOT ${PLUGIN_BUILD})
    add_subdirectory(${TOOLKIT_DIR} ${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit)
endif()

# App project
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/Codes ${CMAKE_CURRENT_LIST_DIR}/Codes/Intermediate/${TK_PLATFORM})
