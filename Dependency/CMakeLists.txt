cmake_minimum_required(VERSION 3.16)

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

# Build static libraries (minizip, zstd, SDL2main) with -fPIC so they
# can be linked into the shared ToolKit.so. The previous Phase 2 design
# linked these static libs directly into the Editor executable (which
# does not require PIC); Phase 3+ moved the linkage up into ToolKit so
# the editor / launcher / import / packer inherit the deps transitively.
# That change makes ToolKit -> minizip / ToolKit -> zstd a shared->static
# edge, which requires PIC on Linux (and on Windows when building with
# Ninja/clang-cl). Shared libs (SDL2, tkimgui, assimp) are unaffected --
# they are already PIC because they are built as shared.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
# Disable OpenSSL backend: we don't need its encryption here and Linux dev
# boxes without openssl-devel installed shouldn't fail to build the tree.
set(MZ_OPENSSL OFF CACHE BOOL "" FORCE)
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
    # Modern GCC (>= 16) flags a long-standing `set but not used` variable
    # in MS3DLoader.cpp and turns it into a hard error because assimp enables
    # -Werror. We are not patching upstream assimp; instead we just disable
    # that specific upgrade here. The warning itself is still emitted, just
    # not fatal.
    set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
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

# On Linux, force-enable the native video backends. SDL2's defaults already
# turn SDL_WAYLAND / SDL_X11 / SDL_KMSDRM on for UNIX_SYS, but their
# CheckWayland() / CheckX11() / CheckKMSDRM() macros silently disable the
# driver when pkg-config can't locate the corresponding .pc files (e.g. on
# Fedora when only the runtime wayland/X11 packages are installed, not the
# *-devel ones). Pinning these here makes the configure step fail loudly
# instead of producing a working-looking libSDL2.so that has no usable video
# driver -- which is exactly the situation that hit us on Fedora: the editor
# loaded libSDL2-2.0d.so.0 and ran "fine" but only because the dummy video
# driver was being selected, leaving the window invisible.
if(UNIX AND NOT APPLE)
    # X11 is the primary Linux backend. We want the same windowing
    # behavior on Linux that we already have on Windows, and Wayland's
    # input / focus / coordinate semantics differ enough from Win32 that
    # the editor behaves inconsistently between the two. Wayland is
    # intentionally left off so SDL_CreateWindow() goes straight to X11
    # instead of preferring Wayland when both are available.
    # KMSDRM is for embedded/headless; leave it off too.
    set(SDL_WAYLAND  OFF CACHE BOOL "" FORCE)
    set(SDL_X11      ON  CACHE BOOL "" FORCE)
    set(SDL_KMSDRM   OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGL   ON CACHE BOOL "" FORCE)
    set(SDL_OPENGLES ON CACHE BOOL "" FORCE)
    # offscreen / dummy are only useful for CI; keep them available but don't
    # let them silently mask a missing real driver.
    set(SDL_OFFSCREEN ON CACHE BOOL "" FORCE)
    set(SDL_DUMMYVIDEO ON CACHE BOOL "" FORCE)
endif()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/SDL2 ${CMAKE_CURRENT_LIST_DIR}/Intermediate/${TK_PLATFORM}/SDL2)

# SDL2's CheckWayland() / CheckX11() / CheckKMSDRM() macros silently
# disable the driver when pkg-config can't locate the corresponding
# *.pc files. SDL_WAYLAND=ON in the cache then *lies* about what's
# compiled in: the resulting libSDL2.so is a working-looking library
# that only has the dummy / offscreen video drivers, which means an
# app that calls SDL_CreateWindow() gets an internal "window" that
# never reaches the compositor -- a *silent* failure mode.
#
# Guard against that. Note that `SDL_VIDEO_DRIVER_WAYLAND` etc. are
# set inside the SDL2 subdirectory scope and are NOT visible here
# (add_subdirectory does not propagate plain set() values), so we
# have to inspect the generated SDL_config.h that SDL2 wrote into
# its own binary dir to know which video drivers actually got
# compiled in.
if(UNIX AND NOT APPLE)
    string(TOLOWER "${CMAKE_BUILD_TYPE}" _tk_lower_cfg)
    # SDL2 writes two config files during configure:
    #   - SDL2_BINARY_DIR/SDL_config.h.intermediate
    #     Written at CONFIGURE time by configure_file(); always present
    #     once SDL2's CMakeLists finishes, even if the rest of the build
    #     later errors out. This is the one we should read.
    #   - SDL2_BINARY_DIR/include-config-<cfg>/SDL2/SDL_config.h
    #     Written by file(GENERATE) at GENERATION time (i.e. when make
    #     runs). NOT available during configure when something else in
    #     the build errored out -- so we cannot rely on it here.
    set(_sdl_cfg_h "${CMAKE_CURRENT_LIST_DIR}/Intermediate/${TK_PLATFORM}/SDL2/SDL_config.h.intermediate")
    if(NOT EXISTS "${_sdl_cfg_h}")
        message(FATAL_ERROR
            "GDTK dependency build: SDL2's intermediate SDL_config.h is missing\n"
            "at ${_sdl_cfg_h}. This means SDL2's CMakeLists did not finish\n"
            "configure_file() -- check the cmake output above for the actual\n"
            "error inside SDL2."
        )
    endif()

    set(_SDL_REAL_DRIVERS "")
    file(STRINGS "${_sdl_cfg_h}" _sdl_cfg_lines)
    foreach(_line IN LISTS _sdl_cfg_lines)
        if(_line MATCHES "^#define SDL_VIDEO_DRIVER_WAYLAND 1$")
            list(APPEND _SDL_REAL_DRIVERS "wayland")
        elseif(_line MATCHES "^#define SDL_VIDEO_DRIVER_X11 1$")
            list(APPEND _SDL_REAL_DRIVERS "x11")
        elseif(_line MATCHES "^#define SDL_VIDEO_DRIVER_KMSDRM 1$")
            list(APPEND _SDL_REAL_DRIVERS "kmsdrm")
        endif()
    endforeach()

    if(NOT _SDL_REAL_DRIVERS)
        # No real video driver got compiled in. Probe pkg-config to figure
        # out *which* -devel package the user needs to install. The probe
        # is best-effort: a "found" here doesn't mean SDL2 will accept
        # it, but a "missing" is actionable.
        find_package(PkgConfig REQUIRED)
        set(_SDL_MISSING "")
        foreach(_m IN ITEMS
                x11 xext xrandr xcursor xi xinerama xkbcommon
                wayland-client wayland-egl wayland-cursor egl
                libdrm gbm)
            execute_process(
                COMMAND pkg-config --exists ${_m}
                RESULT_VARIABLE _rc
                ERROR_QUIET
            )
            if(NOT _rc EQUAL 0)
                list(APPEND _SDL_MISSING "  - ${_m}")
            endif()
        endforeach()

        message(FATAL_ERROR
            "\n"
            "GDTK dependency build: no usable SDL2 video driver was compiled in.\n"
            "SDL2's CheckWayland() / CheckX11() / CheckKMSDRM() silently turned\n"
            "off the requested drivers because pkg-config could not find the\n"
            "required development files. The following modules are missing on\n"
            "this system:\n"
            "\n"
            "${_SDL_MISSING}\n"
            "\n"
            "Install the corresponding *-devel / -dev packages (e.g.\n"
            "  dnf install libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXinerama-devel libxkbcommon-devel\n"
            "on Fedora, or\n"
            "  apt install libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxkbcommon-dev\n"
            "on Debian/Ubuntu) and re-run the dependency build.\n"
            "\n"
            "If you actually want a headless / CI build, pass\n"
            "  -DSDL_WAYLAND=OFF -DSDL_X11=OFF -DSDL_KMSDRM=OFF\n"
            "on the cmake command line and the build will proceed with the\n"
            "dummy / offscreen drivers only.\n"
        )
    endif()

    message(STATUS "GDTK: SDL2 video drivers enabled: ${_SDL_REAL_DRIVERS}")
endif()

#---- Runtime dependency staging ----#
#
# Shared runtime dependencies (SDL2, imgui, assimp) are staged into
# Bin<Config>/ via `cmake --install`. See the "Install rules" block in
# the root CMakeLists.txt for the install() invocations. On both Linux
# and Windows the install step copies every shared library the binaries
# need at runtime alongside them, so $ORIGIN in RPATH is all that's
# needed.
#
# No POST_BUILD copy_dll or CopyDependencies target is necessary any
# more -- the install() rules in the root CMakeLists are the single
# source of truth for staging.
#
# What's handled by install and not here:
#   - ToolKit.so / ToolKit.dll already builds into Bin<Config>/ via
#     RUNTIME_OUTPUT_DIRECTORY.
#   - SDL2, imgui, assimp shared libs are installed from TK_DEPS_DIR
#     via install(DIRECTORY ...) in the root CMakeLists.
#   - minizip and zstd are static libs -- they're linked into ToolKit,
#     not dlopen'd, so they don't need a runtime copy.