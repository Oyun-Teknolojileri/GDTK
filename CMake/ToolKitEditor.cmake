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
# INTERFACE target for editor consumers.
#
# Editor plugins link TK::Editor to get the editor's public include dirs and
# the TK_EDITOR compile definition. There is no shared library -- editor symbols
# are provided by the host editor at runtime (dlopen / LoadLibrary).
#
# TK_EDITOR_DLL_EXPORT is intentionally NOT set here: plugins consume editor
# symbols, they don't export them. Without it, TK_EDITOR_API resolves to
# dllimport on Windows (correct for plugins) and visibility("default") on
# Linux (also correct).
# --------------------------------------------------------------------------- #
add_library(TK::Editor INTERFACE IMPORTED)
set_target_properties(TK::Editor PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${_TK_EDITOR_INCLUDE_DIRS}"
    INTERFACE_COMPILE_DEFINITIONS "TK_EDITOR")
