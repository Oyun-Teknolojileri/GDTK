cmake_minimum_required(VERSION 3.6)

include_directories(
	"${TOOLKIT_DIR}/ToolKit"
	"${TOOLKIT_DIR}/Dependency"
	"${TOOLKIT_DIR}/Dependency/glm"
	"${TOOLKIT_DIR}/Dependency/glad"
	"${TOOLKIT_DIR}/Dependency/mini_audio"
	"${TOOLKIT_DIR}/Dependency/RapidXml"
	"${TOOLKIT_DIR}/Dependency/stb"
	"${TOOLKIT_DIR}/Dependency/minizip-ng"
	"${TOOLKIT_DIR}/Dependency/minizip-ng/compat"
	"${TOOLKIT_DIR}/Dependency/poolSTL/include"
)

file(GLOB SOURCES "${TOOLKIT_DIR}/ToolKit/*.cpp")

if (NOT TK_CXX_EXTRA STREQUAL "")
	set(TK_CXX_FLAGS "${TK_CXX_FLAGS} ${TK_CXX_EXTRA}")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TK_CXX_FLAGS}")
set(CMAKE_DEBUG_POSTFIX d)

if(MSVC_COMPILER)
	add_compile_options(/D_CRT_SECURE_NO_WARNINGS)
endif()

if ("${TK_BUILD_TYPE}" STREQUAL "Debug")
	add_definitions(-DTK_DEBUG)
endif()

if (NOT DEFINED TK_OUT_DIR)
	set(TK_OUT_DIR "${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit/ToolKit/${TK_BUILD_TYPE}")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${TK_OUT_DIR}")

add_library(ToolKit STATIC ${SOURCES})

set(PCH_HEADER "stdafx.h")
set(PCH_SOURCE "stdafx.cpp")
target_precompile_headers(ToolKit PRIVATE ${PCH_HEADER})