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
set(ZSTD_BUILD_STATIC ON)
set(BUILD_SHARED_LIBS OFF)
set(ZSTD_BUILD_SHARED OFF)
set(MZ_ZLIB OFF)
set(MZ_BZIP2 OFF)
set(MZ_LZMA OFF)
set(MZ_FETCH_LIBS ON)
set(MZ_FORCE_FETCH_LIBS ON)
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
    set(ASSIMP_BUILD_TESTS OFF)
    set(ASSIMP_INJECT_DEBUG_POSTFIX ON)
    set(BUILD_SHARED_LIBS ON)
    set(CMAKE_DEBUG_POSTFIX d)
    
    # Force Assimp to use our output directories
    set(ASSIMP_LIBRARY_OUTPUT_DIRECTORY "${TK_DEPENDECY_OUT_DIR}" CACHE STRING "" FORCE)
    set(ASSIMP_RUNTIME_OUTPUT_DIRECTORY "${TK_DEPENDECY_OUT_DIR}" CACHE STRING "" FORCE)
    set(ASSIMP_ARCHIVE_OUTPUT_DIRECTORY "${TK_DEPENDECY_OUT_DIR}" CACHE STRING "" FORCE)
    
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/assimp ${CMAKE_CURRENT_LIST_DIR}/Intermediate/${TK_PLATFORM}/assimp)

    # ---- ToolKit: stabilize assimp output names ----
    # Upstream assimp injects a compiler/toolset suffix (e.g. "-vc143-mt") into
    # LIBRARY_SUFFIX, which lands in the final library name (assimp-vc143-mt.lib
    # / assimpd-vc143-mt.lib). That breaks downstream projects whenever the
    # MSVC toolset changes (vc142 <-> vc143 <-> vc14x). The Import project links
    # against the unsuffixed "assimp.lib" / "assimpd.lib", so we override the
    # output name here, on the wrapper level only — assimp's own CMakeLists
    # files are NOT modified. Platform-specific prefixes/suffixes (.lib/.dll on
    # Windows, .a/.so + SONAME on Linux/macOS, .a/.bc on Emscripten) are still
    # applied by CMake automatically; we only strip the *internal* suffix.
    if(TARGET assimp)
        # Force a flat name: assimp (Release) / assimpd (Debug, via CMAKE_DEBUG_POSTFIX).
        set_target_properties(assimp PROPERTIES
            OUTPUT_NAME              "assimp"
            ARCHIVE_OUTPUT_NAME      "assimp"
            LIBRARY_OUTPUT_NAME      "assimp"
            RUNTIME_OUTPUT_NAME      "assimp"
            # Reuse the same name for PDB / versioned outputs to keep them
            # discoverable next to the .lib/.dll. SONAME on POSIX is left
            # intact via VERSION/SOVERSION already set by assimp.
            COMPILE_PDB_NAME         "assimp"
            COMPILE_PDB_NAME_DEBUG   "assimpd"
        )
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
if ("${TK_PLATFORM}" STREQUAL "Windows")
    # Create target directories
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../Bin/")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../Utils/Import/")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../Utils/Packer/")
    
    # Set dependencies list
    if (NOT ${SKIP_ASSIMP})
        set(COPY_DEPENDENCIES minizip zstd SDL2 imgui assimp)
    else()
        set(COPY_DEPENDENCIES minizip zstd SDL2 imgui)
    endif()
    
    add_custom_target(CopyDependencies ALL
        # Copy SDL2
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:SDL2>" 
            "${CMAKE_CURRENT_LIST_DIR}/../Bin/"
        
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:SDL2>" 
            "${CMAKE_CURRENT_LIST_DIR}/../Utils/Import/"
        
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:SDL2>" 
            "${CMAKE_CURRENT_LIST_DIR}/../Utils/Packer/"
        
        # Copy ImGui
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            "$<TARGET_FILE:imgui>" 
            "${CMAKE_CURRENT_LIST_DIR}/../Bin/"
        
        DEPENDS ${COPY_DEPENDENCIES}
        COMMENT "Copying dependencies for configuration: $<CONFIG>"
        VERBATIM
    )
    
    # Add Assimp copy conditionally
    if (NOT ${SKIP_ASSIMP})
        add_custom_command(TARGET CopyDependencies POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different 
                "$<TARGET_FILE:assimp>" 
                "${CMAKE_CURRENT_LIST_DIR}/../Utils/Import/"
            COMMENT "Copying Assimp for configuration: $<CONFIG>"
        )
    endif()
    
    # Debug information
    message(STATUS "TK_PLATFORM: ${TK_PLATFORM}")
    message(STATUS "CMAKE_CURRENT_LIST_DIR: ${CMAKE_CURRENT_LIST_DIR}")
    message(STATUS "CMAKE_GENERATOR: ${CMAKE_GENERATOR}")
    message(STATUS "CMAKE_CONFIGURATION_TYPES: ${CMAKE_CONFIGURATION_TYPES}")
    message(STATUS "Target directories:")
    message(STATUS "  - Bin: ${CMAKE_CURRENT_LIST_DIR}/../Bin/")
    message(STATUS "  - Import: ${CMAKE_CURRENT_LIST_DIR}/../Utils/Import/")
    message(STATUS "  - Packer: ${CMAKE_CURRENT_LIST_DIR}/../Utils/Packer/")
endif()