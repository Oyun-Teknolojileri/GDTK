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

message(STATUS "DEBUG: TK_PLATFORM is ${TK_PLATFORM}")
if(TK_PLATFORM STREQUAL "MacOS")
    message(STATUS "DEBUG: Entering MacOS block")
    # Check if OpenSSL root dir is already set
    if(NOT DEFINED OPENSSL_ROOT_DIR)
        # Try to find openssl via brew
        find_program(BREW_PROG brew)
        if(BREW_PROG)
            message(STATUS "DEBUG: Logging brew at ${BREW_PROG}")
            execute_process(
                COMMAND ${BREW_PROG} --prefix openssl
                OUTPUT_VARIABLE OPENSSL_BREW_PREFIX
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            message(STATUS "DEBUG: OpenSSL prefix resolved to: '${OPENSSL_BREW_PREFIX}'")
            if(EXISTS "${OPENSSL_BREW_PREFIX}")
                set(OPENSSL_ROOT_DIR "${OPENSSL_BREW_PREFIX}" CACHE PATH "OpenSSL root directory")
                message(STATUS "Found OpenSSL via brew: ${OPENSSL_ROOT_DIR}")
            else()
                 message(STATUS "DEBUG: OpenSSL prefix does not exist")
            endif()
        else()
             message(STATUS "DEBUG: Brew not found")
        endif()
    endif()
endif()

# NOTE: TK_DLL_EXPORT should NOT be defined globally since ToolKit is built as a STATIC library
# If building as a DLL in the future, define it only for that specific build configuration

# Build ToolKit
add_subdirectory(${TOOLKIT_DIR}/ToolKit "${TOOLKIT_DIR}/Intermediate/${TK_PLATFORM}/ToolKit/ToolKit")