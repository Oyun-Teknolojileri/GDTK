cmake_minimum_required(VERSION 3.17)
project(Editor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Path to your root project
set(TOOLKIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ----------------------------
# Add ToolKit library
# ----------------------------
add_subdirectory(${TOOLKIT_DIR}/ToolKit "${CMAKE_BINARY_DIR}/ToolKit")

# ----------------------------
# Include directories
# ----------------------------
include_directories(
    "${TOOLKIT_DIR}/Editor"
    "${TOOLKIT_DIR}/ToolKit"
    "${TOOLKIT_DIR}/Dependency"
    "${TOOLKIT_DIR}/Dependency/SDL2"
    "${TOOLKIT_DIR}/Dependency/SDL2/include"
    "${TOOLKIT_DIR}/Dependency/RapidXml"
    "${TOOLKIT_DIR}/Dependency/stb"
    "${TOOLKIT_DIR}/Dependency/glm"
    "${TOOLKIT_DIR}/Dependency/tkimgui"
    "${TOOLKIT_DIR}/Dependency/tkimgui/imgui"
    "${TOOLKIT_DIR}/Dependency/tkimgui/imgui/backends"
)

# ----------------------------
# Source files
# ----------------------------
file(GLOB SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ImGui/*.cpp"
)

# ----------------------------
# Create Editor executable
# ----------------------------
add_executable(Editor ${SOURCES})

# ----------------------------
# Dependency library paths
# ----------------------------

# Detect current build type and set dependency path accordingly
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Default build type")
endif()

set(DEPENDENCY_DIR
    "${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}"
)
set(DEPENDENCY_LIB_DIR
    "${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}"
)


# ----------------------------
# Link libraries
# ----------------------------

if(APPLE)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(OPENGL_FRAMEWORK OpenGL)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    find_library(COMPRESSION_FRAMEWORK Compression)  # <-- Add this

    target_link_libraries(Editor
        PRIVATE
            ${COCOA_FRAMEWORK}
            ${OPENGL_FRAMEWORK}
            ${IOKIT_FRAMEWORK}
            ${COREVIDEO_FRAMEWORK}
            ${COMPRESSION_FRAMEWORK}  # <-- Add this
    )
endif()

target_link_libraries(Editor
    PRIVATE
        ToolKit
        "${DEPENDENCY_LIB_DIR}/libSDL2-2.0.0.dylib"
        "${DEPENDENCY_LIB_DIR}/libSDL2-2.0.dylib"
        "${DEPENDENCY_LIB_DIR}/libSDL2main.a"
        "${DEPENDENCY_LIB_DIR}/libimgui.dylib"
        "${DEPENDENCY_LIB_DIR}/libminizip.a"
        "${DEPENDENCY_LIB_DIR}/libzstd.a"
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
)


# ----------------------------
# Output directory
# ----------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/Bin")

# ----------------------------
# RPATH configuration for macOS
# ----------------------------
if(APPLE)
    # Set RPATH for finding dylibs relative to executable
set_target_properties(Editor PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path;@executable_path/../lib;@executable_path/../Frameworks;${DEPENDENCY_LIB_DIR}"
        INSTALL_RPATH_USE_LINK_PATH TRUE
    )

    # For .app bundles, use @executable_path/../Frameworks
    if(CMAKE_MACOSX_BUNDLE)
        set_target_properties(Editor PROPERTIES
            INSTALL_RPATH "@executable_path/../Frameworks"
        )
    endif()
endif()

# ----------------------------
# Compiler flags for debug/native builds
# ----------------------------
if(NOT EMSCRIPTEN)
    message(STATUS "Building native Editor")
    target_compile_options(Editor PRIVATE -O0 -g)
endif()
