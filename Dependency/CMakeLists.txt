cmake_minimum_required(VERSION 3.6)

# Builds all dependencies for the ToolKit.
project(ToolKitDependencies)

#---- COMMON OUTPUT DIR ----#
if(CMAKE_BUILD_TYPE)
    set(TK_DEPENDECY_OUT_DIR "${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/${CMAKE_BUILD_TYPE}")
else()
    set(TK_DEPENDECY_OUT_DIR "${TOOLKIT_DIR}/Dependency/Intermediate/${TK_PLATFORM}/$<CONFIG>")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${TK_DEPENDECY_OUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${TK_DEPENDECY_OUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${TK_DEPENDECY_OUT_DIR}")

#---- MINIZIP ----#
set(MZ_COMPAT ON)
set(MZ_ZSTD ON)              
set(MZ_FETCH_LIBS ON)
set(MZ_FORCE_FETCH_LIBS OFF)
set(ZSTD_BUILD_STATIC ON)
set(BUILD_SHARED_LIBS OFF)
set(ZSTD_BUILD_SHARED OFF)
set(MZ_ZLIB OFF)
set(MZ_BZIP2 OFF)
set(MZ_LZMA OFF)
set(MZ_PKCRYPT OFF)
set(MZ_WZAES OFF)
set(MZ_ICONV OFF)
set(FETCHCONTENT_BASE_DIR ${CMAKE_CURRENT_LIST_DIR}/Intermediate/${TK_PLATFORM}/minizip-ng/_deps CACHE STRING "" FORCE)
set(CMAKE_DEBUG_POSTFIX d)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/minizip-ng ${CMAKE_CURRENT_LIST_DIR}/Intermediate/${TK_PLATFORM}/minizip-ng)


#---- IMGUI ----#
if (NOT ${SKIP_IMGUI})
    set(CMAKE_DEBUG_POSTFIX d)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/tkimgui ${CMAKE_CURRENT_LIST_DIR}/Intermediate/${TK_PLATFORM}/tkimgui)
endif()

#---- ASSIMP ----#
if (NOT ${SKIP_ASSIMP})
    set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INJECT_DEBUG_POSTFIX ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS ON)
    set(CMAKE_DEBUG_POSTFIX d)

    # Force Assimp to use our output directories
    set(ASSIMP_LIBRARY_OUTPUT_DIRECTORY "${TK_DEPENDECY_OUT_DIR}" CACHE STRING "" FORCE)
    set(ASSIMP_RUNTIME_OUTPUT_DIRECTORY "${TK_DEPENDECY_OUT_DIR}" CACHE STRING "" FORCE)
    set(ASSIMP_ARCHIVE_OUTPUT_DIRECTORY "${TK_DEPENDECY_OUT_DIR}" CACHE STRING "" FORCE)

    # Ensure debug postfix is applied
    set(CMAKE_DEBUG_POSTFIX "d" CACHE STRING "" FORCE)

    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/assimp ${CMAKE_CURRENT_LIST_DIR}/Intermediate/${TK_PLATFORM}/assimp)

    # After Assimp is configured, ensure the debug library has the correct name
    if(TARGET assimp)
        set_target_properties(assimp PROPERTIES DEBUG_POSTFIX "d")
    endif()
endif()

#---- SDL2 ----#
set(SDL_SHARED ON)
set(SDL_STATIC OFF)
set(SDL_TEST OFF)
set(BUILD_SHARED_LIBS ON)
set(CMAKE_DEBUG_POSTFIX d)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/SDL2 ${CMAKE_CURRENT_LIST_DIR}/Intermediate/${TK_PLATFORM}/SDL2)

#---- Copy DLL/LIB ----#
# Create target directories (common for all platforms)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../Bin/")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../Utils/Import/")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../Utils/Packer/")

# Set dependencies list (common for all platforms)
set(COPY_DEPENDENCIES_LIST minizip SDL2)
if (NOT ${SKIP_IMGUI})
    list(APPEND COPY_DEPENDENCIES_LIST imgui)
endif()
if (NOT ${SKIP_ASSIMP})
    list(APPEND COPY_DEPENDENCIES_LIST assimp)
endif()

# Windows also needs zstd copied
if ("${TK_PLATFORM}" STREQUAL "Windows")
    list(APPEND COPY_DEPENDENCIES_LIST zstd)
endif()

# Create copy target (common for all platforms)
add_custom_target(CopyDependencies ALL
    # Copy SDL2 to all required directories
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:SDL2>"
        "${CMAKE_CURRENT_LIST_DIR}/../Bin/"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:SDL2>"
        "${CMAKE_CURRENT_LIST_DIR}/../Utils/Import/"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:SDL2>"
        "${CMAKE_CURRENT_LIST_DIR}/../Utils/Packer/"

    DEPENDS ${COPY_DEPENDENCIES_LIST}
    COMMENT "Copying dependency libraries for ${TK_PLATFORM}: $<CONFIG>"
    VERBATIM
)

# Copy ImGui if enabled (common for all platforms)
if (NOT ${SKIP_IMGUI})
    add_custom_command(TARGET CopyDependencies POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:imgui>"
            "${CMAKE_CURRENT_LIST_DIR}/../Bin/"
        COMMENT "Copying ImGui library"
    )
endif()

# Copy Assimp if enabled (common for all platforms)
if (NOT ${SKIP_ASSIMP})
    add_custom_command(TARGET CopyDependencies POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:assimp>"
            "${CMAKE_CURRENT_LIST_DIR}/../Utils/Import/"
        COMMENT "Copying Assimp library"
    )
endif()

# Debug information (common for all platforms)
message(STATUS "TK_PLATFORM: ${TK_PLATFORM}")
if ("${TK_PLATFORM}" STREQUAL "Windows")
    message(STATUS "CMAKE_CURRENT_LIST_DIR: ${CMAKE_CURRENT_LIST_DIR}")
    message(STATUS "CMAKE_GENERATOR: ${CMAKE_GENERATOR}")
    message(STATUS "CMAKE_CONFIGURATION_TYPES: ${CMAKE_CONFIGURATION_TYPES}")
endif()
message(STATUS "Target directories:")
message(STATUS "  - Bin: ${CMAKE_CURRENT_LIST_DIR}/../Bin/")
message(STATUS "  - Import: ${CMAKE_CURRENT_LIST_DIR}/../Utils/Import/")
message(STATUS "  - Packer: ${CMAKE_CURRENT_LIST_DIR}/../Utils/Packer/")