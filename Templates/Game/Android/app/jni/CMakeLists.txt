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

# Provide platform definitions.
set(TK_ANDROID "Android")
set(TK_WINDOWS "Windows")
set(TK_WEB "Web")

# Set platform-specific build directory.
set(TK_PLATFORM ${TK_ANDROID})

if(CMAKE_BUILD_TYPE)
	set(TK_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
	set(TK_BUILD_TYPE "$<CONFIG>")
endif()

# Check if the compiler is Clang.
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
	set(CLANG_COMPILER TRUE)
	set(CMAKE_CXX_STANDARD 17)
	add_compile_options(-w) # All warnings off for Clang
else()
	# Android can only use clang. If you are here, something is wrong ...
	message(SEND_ERROR "This project requires Clang compiler to build. Please use a compatible compiler.")
	return()
endif()

# ToolKit Dependencies.
set(SKIP_ASSIMP TRUE)
set(SKIP_IMGUI TRUE)
add_subdirectory(${TOOLKIT_DIR}/Dependency ${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${TK_BUILD_TYPE})

# ToolKit Engine
add_subdirectory(${TOOLKIT_DIR} ${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit)

# Android App project.
add_subdirectory(src)
