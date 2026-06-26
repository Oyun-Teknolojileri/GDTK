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
    # Wayland is the primary Linux backend on modern Fedora/Ubuntu/etc.
    # X11 is intentionally left off: it pulls in libXext / libXrandr /
    # libXcursor / libXi / libXinerama dev packages, all of which are
    # unrelated to our use case (we run on a native Wayland session).
    # KMSDRM is for embedded/headless; leave it off too.
    set(SDL_WAYLAND  ON CACHE BOOL "" FORCE)
    set(SDL_X11      OFF CACHE BOOL "" FORCE)
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
                wayland-client wayland-egl wayland-cursor egl xkbcommon
                x11 xext xrandr xcursor xi xinerama
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
            "  dnf install wayland-devel libxkbcommon-devel mesa-libEGL-devel\n"
            "on Fedora, or\n"
            "  apt install libwayland-dev libxkbcommon-dev libegl-dev\n"
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

#---- Copy DLLs to runtime locations (Windows only) ----#
#
# Stages the runtime DLLs of the GDTK tools into the directories where
# the binaries land. Windows resolves DLLs from the binary's directory
# first, so without these copies Import.exe / Packer.exe / Editor.exe
# would not find SDL2 / imgui at runtime. Linux relies on RPATH
# ($ORIGIN + TK_DEPS_DIR) instead and doesn't need any of this.
#
# What is *not* handled here and why:
#   - ToolKit.dll copying is in Import/CMakeLists.txt and
#     Packer/CMakeLists.txt (POST_BUILD on each target, tied to the
#     target via add_dependencies(... ToolKit) so a ToolKit source
#     change re-runs the copy). Keeping it there means a single source
#     of truth for "where ToolKit lives next to the consumer".
#   - assimp.dll copying is in Import/CMakeLists.txt the same way --
#     tied to the Import target via the link line. Packer does not
#     depend on assimp.
#   - minizip and zstd are static libs (BUILD_SHARED_LIBS=OFF / ZSTD_
#     BUILD_SHARED=OFF above) -- they're linked into the binaries,
#     not dlopen'd, so they don't need a runtime copy.
if ("${TK_PLATFORM}" STREQUAL "Windows")
    set(COPY_RUNTIME_DLLS SDL2 imgui)

    # `cmake -E copy_if_different` errors with "Invalid argument" when the
    # destination directory does not exist yet -- it does not auto-create
    # parents the way `cp` does. The Bin/ and Utils/{Import,Packer}/
    # folders only get created when the first build of the corresponding
    # target runs, which is *after* this CopyDependencies step on a clean
    # checkout. Make sure each destination directory exists immediately
    # before the copy that targets it; doing it once up-front is not
    # enough on the Visual Studio generator because MSBuild may run
    # sibling CustomBuild steps concurrently and race the mkdir.
    set(_BIN_DIR     "${CMAKE_CURRENT_LIST_DIR}/../Bin")
    set(_IMPORT_DIR  "${CMAKE_CURRENT_LIST_DIR}/../Utils/Import")
    set(_PACKER_DIR  "${CMAKE_CURRENT_LIST_DIR}/../Utils/Packer")

    add_custom_target(CopyDependencies ALL
        # SDL2: needed by Editor / Launcher / Import / Packer at runtime.
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_BIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2>"
            "${_BIN_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_IMPORT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2>"
            "${_IMPORT_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_PACKER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2>"
            "${_PACKER_DIR}/"

        # imgui: needed by Editor / Launcher at runtime. _BIN_DIR was
        # already created above; mkdir is a no-op if it exists.
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_BIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:imgui>"
            "${_BIN_DIR}/"

        DEPENDS ${COPY_RUNTIME_DLLS}
        COMMENT "Copying runtime DLLs for configuration: $<CONFIG>"
        VERBATIM
    )
endif()