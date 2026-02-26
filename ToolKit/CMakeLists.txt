cmake_minimum_required(VERSION 3.17)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files
file(GLOB SOURCES "${TOOLKIT_DIR}/ToolKit/*.cpp")

# Output directory
if (NOT DEFINED TK_OUT_DIR)
    set(TK_OUT_DIR "${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit/ToolKit/${TK_BUILD_TYPE}")
endif()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${TK_OUT_DIR}")

# Create ToolKit library
add_library(ToolKit STATIC ${SOURCES})

# Precompiled header
set(PCH_HEADER "stdafx.h")
target_precompile_headers(ToolKit PRIVATE ${PCH_HEADER})

# ----------------------------
# Include directories (target-specific)
# ----------------------------
target_include_directories(ToolKit
    PUBLIC
        "${TOOLKIT_DIR}/ToolKit"
        "${TOOLKIT_DIR}/Dependency" 
		"${TOOLKIT_DIR}/Dependency/minizip-ng" 
		"${TOOLKIT_DIR}/Dependency/minizip-ng/compat/"                    # parent of RapidXml, stb, glm, etc.
        "${TOOLKIT_DIR}/Dependency/glm"
        "${TOOLKIT_DIR}/Dependency/glad"
        "${TOOLKIT_DIR}/Dependency/mini_audio"
        "${TOOLKIT_DIR}/Dependency/poolSTL/include"
)


# ----------------------------
# Compiler flags
# ----------------------------
if(MSVC)
    target_compile_definitions(ToolKit PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if ("${TK_BUILD_TYPE}" STREQUAL "Debug")
    target_compile_definitions(ToolKit PRIVATE TK_DEBUG)
endif()

if (NOT TK_CXX_EXTRA STREQUAL "")
    target_compile_options(ToolKit PRIVATE ${TK_CXX_EXTRA})
endif()

set(CMAKE_DEBUG_POSTFIX d)
