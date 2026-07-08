cmake_minimum_required(VERSION 3.16)

# ToolKit App/Game CMake file.
#
# Two build modes are driven by PLUGIN_BUILD (set by the parent CMakeLists):
#   * Plugin build   -> a SHARED library the editor dlopen()s and runs in PIE.
#   * Standalone     -> a runnable executable that hosts the engine itself.
#
# Engine consumption (the IMPORTED target TK::ToolKit + CMAKE_PREFIX_PATH for
# the vendored deps) is set up by the parent CMakeLists.txt; this file only
# declares the game target for the active mode.

set(GAME_PROJECT_NAME "__projectname__" CACHE STRING "What is the project name ?")

set(GAME_CODE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(GAME_BIN_DIR "${GAME_CODE_DIR}/Bin")

# Create the project.
project(${GAME_PROJECT_NAME})

# Log build details.
string(TOLOWER ${CMAKE_CXX_COMPILER_ID} TK_COMPILER)
message("Compiling ${GAME_PROJECT_NAME} with ${TK_COMPILER} for platform ${TK_PLATFORM} (${TK_BUILD_TYPE})")

set(HEADERS Game.h)

if(${PLUGIN_BUILD})
    # ----------------------------------------------------------------------- #
    # Plugin build: SHARED library loaded by the editor.
    # The editor resolves it at <project>/Codes/Bin/<name><ext> via
    # Workspace::GetBinPath() + Util::GetPluginExtention(), and looks up the
    # exported GetInstance() symbol (Game.cpp). Only the engine is linked;
    # SDL2/imgui symbols are provided at runtime by the host editor process.
    # ----------------------------------------------------------------------- #
    set(CMAKE_DEBUG_POSTFIX d) # Matches TK_PLUGIN_DEBUG_EXT ("d.so" / "d.dll").
    add_library(${GAME_PROJECT_NAME} SHARED Game.cpp ${HEADERS})

    # The editor loads the project name verbatim -- strip the default "lib" prefix.
    set_target_properties(${GAME_PROJECT_NAME} PROPERTIES PREFIX "")

    target_link_libraries(${GAME_PROJECT_NAME} PRIVATE TK::ToolKit)
else()
    # ----------------------------------------------------------------------- #
    # Standalone build: executable hosting the engine.
    # ----------------------------------------------------------------------- #
    if("${TK_PLATFORM}" STREQUAL "${TK_WINDOWS}")
        add_executable(${GAME_PROJECT_NAME} WIN32 Game.cpp Main.cpp ${HEADERS})
    else()
        add_executable(${GAME_PROJECT_NAME} Game.cpp Main.cpp ${HEADERS})
    endif()

    # Pre-built vendored deps consumed directly by the standalone app.
    # (Resolved via CMAKE_PREFIX_PATH = TK_DEPS_DIR from the parent CMakeLists.)
    find_package(SDL2 REQUIRED CONFIG)
    find_package(SDL2main REQUIRED CONFIG)
    find_package(imgui REQUIRED CONFIG)

    target_link_libraries(${GAME_PROJECT_NAME} PRIVATE
        TK::ToolKit
        SDL2
        SDL2main
        imgui)

    if("${TK_PLATFORM}" STREQUAL "${TK_WINDOWS}")
        target_link_libraries(${GAME_PROJECT_NAME} PRIVATE OpenGL32)
    endif()

    # Dev vs publish RPATH.
    #
    # Dev (Codes/Bin binary): BUILD_RPATH stays absolute so the unbundled exe,
    # run from Codes/Bin, finds libToolKit<cfg>.so under Bin<Config>/ and the
    # vendored deps under TK_DEPS_DIR without LD_LIBRARY_PATH. $<CONFIG> selects
    # the matching per-config Bin folder.
    #
    # Publish bundle: INSTALL_RPATH is $ORIGIN. install(TARGETS) below rewrites
    # the exe's RPATH BUILD_RPATH -> INSTALL_RPATH at install time (CMP0087 NEW,
    # enabled by cmake_minimum_required(3.16)), and the bundle colocates every
    # lib next to the exe (see the install(FILES) block), so $ORIGIN alone
    # resolves them and the bundle is relocatable. INSTALL_RPATH_USE_LINK_PATH
    # is left OFF (default) so the absolute link dirs are NOT appended back in.
    set_target_properties(${GAME_PROJECT_NAME} PROPERTIES
        BUILD_RPATH   "${TOOLKIT_DIR}/Bin$<CONFIG>;${TK_DEPS_DIR}"
        INSTALL_RPATH "$ORIGIN")

    # ----------------------------------------------------------------------- #
    # Self-contained publish bundle. Driven by the Packer via:
    #   cmake --build   <build> [--config <cfg>]
    #   cmake --install <build> [--config <cfg>] --prefix <Publish/<Plat>>
    # Everything lands in <prefix>/Bin next to the exe. install(FILES) does NOT
    # rewrite RPATH, so libToolKit/libimgui ship as-built: libToolKit already
    # carries $ORIGIN (engine root policy); libimgui is fixed at its build
    # (Dependency/tkimgui/CMakeLists.txt). No generator expressions are used in
    # the install(FILES) paths -- they are materialized at configure time, which
    # is required on the cmake_minimum_required(3.16) floor.
    # ----------------------------------------------------------------------- #
    install(TARGETS ${GAME_PROJECT_NAME}
        RUNTIME DESTINATION Bin)

    get_property(_TK_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_TK_IS_MULTI_CONFIG)
        # ---- Windows / Visual Studio (multi-config) ----
        # The engine ships ONLY BinDebug/ and BinRelease/ (no BinRelWithDebInfo),
        # and deps are built only for Debug/Release. TK::ToolKit maps
        # RelWithDebInfo->Debug and MinSizeRel->Release (ToolKitEngine.cmake
        # MAP_IMPORTED_CONFIG_*), so each game config bundles the matching
        # {source dir, "d" suffix} pair rather than a literal Bin<cfg>. OPTIONAL
        # keeps a missing file non-fatal during bring-up.
        set(_tk_dir_Debug          "BinDebug")
        set(_tk_dir_Release        "BinRelease")
        set(_tk_dir_RelWithDebInfo "BinDebug")    # mapped to Debug
        set(_tk_dir_MinSizeRel     "BinRelease")  # mapped to Release
        set(_tk_sfx_Debug          "d")
        set(_tk_sfx_Release        "")
        set(_tk_sfx_RelWithDebInfo "d")           # mapped to Debug
        set(_tk_sfx_MinSizeRel     "")

        foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
            if(NOT _cfg IN_LIST CMAKE_CONFIGURATION_TYPES)
                continue()
            endif()
            set(_d "${TOOLKIT_DIR}/${_tk_dir_${_cfg}}")
            set(_s "${_tk_sfx_${_cfg}}")
            install(FILES
                    "${_d}/libToolKit${_s}.dll"
                    "${_d}/SDL2${_s}.dll"
                    "${_d}/imgui${_s}.dll"
                DESTINATION Bin
                CONFIGURATIONS ${_cfg}
                OPTIONAL)
        endforeach()
    else()
        # ---- Linux / Make / Ninja (single-config) ----
        # Active config is known at configure time. Deps are built only for
        # Debug/Release (build_dependencies.py DEFAULT_CONFIGS). libToolKit and
        # libimgui are real files; SDL2 is handled separately below (it ships as
        # a SONAME symlink chain that install(FILES) would copy verbatim, leaving
        # a dangling link).
        if("${TK_PLATFORM}" STREQUAL "${TK_LINUX}")
            if(TK_BUILD_TYPE STREQUAL "Debug")
                set(_sfx "d")
            else()
                set(_sfx "")
            endif()
            # libToolKit + libimgui are real files; install directly.
            install(FILES
                    "${TOOLKIT_DIR}/Bin${TK_BUILD_TYPE}/libToolKit${_sfx}.so"
                    "${TK_DEPS_DIR}/libimgui${_sfx}.so"
                DESTINATION Bin)
            # SDL2 ships as a SONAME symlink chain (.so.0 -> .so.0.<ver>).
            # install(FILES) copies the symlink verbatim and leaves a dangling
            # link (the versioned real file is NOT installed), so the loader
            # silently falls back to a system SDL2. Resolve the real file at
            # configure time and install it under the SONAME name so the bundle
            # is self-contained. (get_filename_component REALPATH, not
            # file(REAL_PATH), to stay on the cmake 3.16 floor.)
            get_filename_component(_sdl2_real
                "${TK_DEPS_DIR}/libSDL2-2.0${_sfx}.so.0" REALPATH)
            install(FILES "${_sdl2_real}"
                DESTINATION Bin
                RENAME "libSDL2-2.0${_sfx}.so.0")
        endif()
    endif()
endif()

# Project's own headers (Game.h, Platform/*_main.h). Required so the platform
# main shims can resolve "../Codes/Game.h" and the sources find Game.h.
target_include_directories(${GAME_PROJECT_NAME} PRIVATE ${GAME_CODE_DIR})

# Shared precompiled header. The engine's stdafx.h provides Platform.h, STL,
# GLM config, RapidXml and the core ToolKit types for every translation unit.
# Single header only -- CMake allows one PCH per language per target.
target_precompile_headers(${GAME_PROJECT_NAME} PRIVATE
    "${TOOLKIT_DIR}/ToolKit/Source/stdafx.h")

# TK_DEBUG gate for the game target. TK_GL_3_0 / TK_DLL_IMPORT already flow in
# from the TK::ToolKit IMPORTED target's interface.
target_compile_definitions(${GAME_PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:TK_DEBUG>)

# Place artifacts in the project bin (the editor's GetBinPath() points here).
set_target_properties(${GAME_PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GAME_BIN_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${GAME_BIN_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${GAME_BIN_DIR})

# Multi-config generators (Visual Studio, Ninja Multi-Config) append the active
# config as a path component; pin per-config output dirs to keep artifacts flat
# in Codes/Bin (matches the editor's single-path GetBinPath() lookup).
get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(isMultiConfig)
    foreach(config ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${config} config_upper)
        set_target_properties(${GAME_PROJECT_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${GAME_BIN_DIR}
            LIBRARY_OUTPUT_DIRECTORY_${config_upper} ${GAME_BIN_DIR}
            ARCHIVE_OUTPUT_DIRECTORY_${config_upper} ${GAME_BIN_DIR}
            PDB_OUTPUT_DIRECTORY_${config_upper} ${GAME_BIN_DIR})
    endforeach()
endif()

# MSVC link options for the standalone executable.
if(MSVC AND NOT ${PLUGIN_BUILD})
    target_link_options(${GAME_PROJECT_NAME} PRIVATE
        $<$<NOT:$<CONFIG:DEBUG>>:/INCREMENTAL:NO>
        $<$<NOT:$<CONFIG:DEBUG>>:/LTCG>)
endif()

# Hot reload: rename the previous plugin output so the editor can release the
# file handle and reload the freshly built one. Editor-only (plugin build).
if(${PLUGIN_BUILD})
    set(DLL $<SHELL_PATH:$<TARGET_FILE:${GAME_PROJECT_NAME}>>)

    # PDB is MSVC-only; GCC/Clang carry DWARF debug info inside the binary.
    if(MSVC)
        set(_HOTRELOAD_PDB_ARG -Dpdb=$<SHELL_PATH:$<TARGET_PDB_FILE:${GAME_PROJECT_NAME}>>)
    else()
        set(_HOTRELOAD_PDB_ARG)
    endif()

    add_custom_command(TARGET ${GAME_PROJECT_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -Ddll=${DLL} ${_HOTRELOAD_PDB_ARG} -Dtmp="${GAME_BIN_DIR}/tmp"
        -P "${GAME_CODE_DIR}/CMakeHotReload.cmake")
endif()
