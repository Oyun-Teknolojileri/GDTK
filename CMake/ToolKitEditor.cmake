# ToolKitEditor.cmake -- editor consumption helper for editor-plugin projects.
#
# include()'d by a plugin CMakeLists AFTER ToolKitEngine.cmake. Provides the
# editor's public include directories and compile definitions so plugins can
# use editor types (Window, EditorTypes, App, etc.) whose symbols are resolved
# at runtime by the host editor process.
#
# include("${TOOLKIT_DIR}/CMake/ToolKitEditor.cmake")
#
# Inputs (must be set before include()):
#   TOOLKIT_DIR  -- absolute path to the GDTK engine root (already resolved for
#                   ToolKitEngine.cmake).
#
# Provides (in the caller's scope):
#   the INTERFACE target TK::Editor (link it; it carries the editor's public
#   include dirs + compile definition: TK_EDITOR).

if(NOT DEFINED TOOLKIT_DIR)
    message(FATAL_ERROR
        "TOOLKIT_DIR must be set before include(ToolKitEditor.cmake). "
        "Resolve it in your project's CMakeLists.txt first.")
endif()

# --------------------------------------------------------------------------- #
# Editor include directories.
#
# Editor headers include each other unqualified (e.g. #include "EditorTypes.h"
# in Editor/UI/Window.h), so every Editor subdirectory must be on the include
# path. This mirrors the Editor's own CMakeLists.txt.
# --------------------------------------------------------------------------- #
set(_TK_EDITOR_INCLUDE_DIRS "${TOOLKIT_DIR}/Editor")
foreach(_editor_subdir
    Source
    Mods
    UI
    UI/Overlay
    UI/Viewport
    UI/Window
    UI/View
    Entities
    Entities/Light
    Entities/UI
    Render
    Render/Pass
    Components)
    list(APPEND _TK_EDITOR_INCLUDE_DIRS "${TOOLKIT_DIR}/Editor/${_editor_subdir}")
endforeach()

# Editor headers also reference the Workspace module (e.g. App.h includes
# Workspace.h). Add its include directories so the precompiled header
# (stdafx.h) resolves these when TK_EDITOR is defined.
list(APPEND _TK_EDITOR_INCLUDE_DIRS
    "${TOOLKIT_DIR}/Modules/Workspace"
    "${TOOLKIT_DIR}/Modules/Workspace/Source")

# --------------------------------------------------------------------------- #
# Editor import target for editor-plugin consumers.
#
# Editor plugins link TK::Editor to get the editor's public include dirs and
# the TK_EDITOR compile definition. On Linux, editor symbols are resolved at
# runtime by the host process (dlopen). On Windows, the MSVC linker requires an
# import library even for symbols provided by the host .exe at runtime, so we
# provide the Editor import library per configuration.
#
# TK_EDITOR_DLL_EXPORT is intentionally NOT set here: plugins consume editor
# symbols, they don't export them. Without it, TK_EDITOR_API resolves to
# dllimport on Windows (correct for plugins) and visibility("default") on
# Linux (also correct).
# --------------------------------------------------------------------------- #
if(WIN32)
    # UNKNOWN IMPORTED: we only need the import library (.lib) to resolve editor
    # symbols at link time. The actual symbols come from Editor.exe at runtime.
    # UNKNOWN type doesn't require IMPORTED_LOCATION to be a real shared library.
    add_library(TK::Editor UNKNOWN IMPORTED)
    # Editor.lib has no debug postfix (unlike ToolKitd.lib) -- the Editor is a
    # host executable, not a plugin, and its import library name is always Editor.lib.
    set(_TK_EDITOR_CONFIGS Debug Release RelWithDebInfo MinSizeRel)
    foreach(_editor_cfg IN LISTS _TK_EDITOR_CONFIGS)
        string(TOUPPER "${_editor_cfg}" _editor_cfgu)
        set_target_properties(TK::Editor PROPERTIES
            IMPORTED_LOCATION_${_editor_cfgu} "${TOOLKIT_DIR}/Bin${_editor_cfg}/Editor.lib")
    endforeach()
    list(JOIN _TK_EDITOR_CONFIGS ";" _TK_EDITOR_CONFIGS_STR)
    set_target_properties(TK::Editor PROPERTIES
        IMPORTED_CONFIGURATIONS "${_TK_EDITOR_CONFIGS_STR}")

    # Editor plugins also call ImGui directly (e.g. ImGui::Begin/End/Text). On
    # Windows the MSVC linker needs the import library; on Linux these symbols are
    # resolved by the host process at dlopen time.
    find_package(imgui REQUIRED)
    set_target_properties(TK::Editor PROPERTIES
        INTERFACE_LINK_LIBRARIES "imgui")
else()
    add_library(TK::Editor INTERFACE IMPORTED)
endif()

set_target_properties(TK::Editor PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_TK_EDITOR_INCLUDE_DIRS}"
    INTERFACE_COMPILE_DEFINITIONS "TK_EDITOR")
