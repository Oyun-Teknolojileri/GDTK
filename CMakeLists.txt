cmake_minimum_required(VERSION 3.6)

# ToolKit project build file. 
# Builds all dependencies and the ToolKit library.
project(ToolKit)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define TK_GL_ES_3_0 only for platforms that use OpenGL ES (Android, iOS, Emscripten)
# Desktop platforms (Windows, macOS, Linux) use desktop OpenGL
if(ANDROID OR IOS OR EMSCRIPTEN)
  add_definitions(-DTK_GL_ES_3_0)
endif()

# Define TK_DLL_EXPORT globally
add_definitions(-DTK_DLL_EXPORT)

# Build ToolKit
add_subdirectory(${TOOLKIT_DIR}/ToolKit "${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit/ToolKit")