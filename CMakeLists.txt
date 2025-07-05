cmake_minimum_required(VERSION 3.6)

# ToolKit project build file. 
# Builds all dependencies and the ToolKit library.
project(ToolKit)

# Define TK_GL_ES_3_0 globally
add_definitions(-DTK_GL_ES_3_0)

# Define TK_DLL_EXPORT globally
add_definitions(-DTK_DLL_EXPORT)

# Build ToolKit
add_subdirectory(${TOOLKIT_DIR}/ToolKit "${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit/ToolKit")