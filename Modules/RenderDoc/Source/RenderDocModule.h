/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file RenderDocModule.h Editor glue for the engine capture core.
 *
 * The capture machinery itself is ToolKit/Source/RenderDocCapture.h: it binds
 * RenderDoc, drives the frame, writes the capture and serves the request file,
 * and both the editor and a game link it. What is left for this module is the
 * editor half:
 *
 *   - answer where captures go (<workspace>/<project>/Captures) and what the
 *     sidecar should record (workspace, project, scene),
 *   - contribute the console commands and the Modules menu entries,
 *   - call into the core from Editor/Source/main.cpp, which is the only file in
 *     the editor that knows capture exists.
 *
 * Nothing here appears in a game build.
 */

#include "EditorTypes.h"

#include <RenderDocCapture.h>

#ifdef TK_WIN
  #if defined(TK_RENDERDOC_DLL_EXPORT)
    #define TK_RENDERDOC_API __declspec(dllexport)
  #else
    #define TK_RENDERDOC_API __declspec(dllimport)
  #endif
#else
  #define TK_RENDERDOC_API __attribute__((visibility("default")))
#endif

namespace ToolKit
{
  namespace RenderDoc
  {

    /** The core's state struct, named once so editor code includes one header. */
    typedef RenderDocCapture::Status Status;

    /**
     * Points the capture core at the editor's capture directory, then registers
     * the commands and menu entries. Called from PreInit, before any graphics
     * work; returns whatever the core reports (false when RenderDoc is not in
     * the process, which is the normal editor start).
     */
    TK_RENDERDOC_API bool Init();

    /** Takes the commands and the menu back out, then shuts the core down. */
    TK_RENDERDOC_API void Shutdown();

    /** Frame hooks, forwarded to the core. See RenderDocCapture.h. */
    TK_RENDERDOC_API void BeginFrame();
    TK_RENDERDOC_API void EndFrame();

    TK_RENDERDOC_API bool IsAvailable();
    TK_RENDERDOC_API Status GetStatus();
    TK_RENDERDOC_API bool RequestCapture(const String& label, int frameCount = 1);
    TK_RENDERDOC_API bool OpenLastCapture();

  } // namespace RenderDoc
} // namespace ToolKit
