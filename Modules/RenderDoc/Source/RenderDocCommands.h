/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file RenderDocCommands.h Module internal: the console commands the module
 * contributes to the editor.
 */

namespace ToolKit
{
  namespace RenderDoc
  {

    /**
     * Registers CaptureFrame / CaptureStatus / CaptureOpen through the editor's
     * console API. Idempotent, so Init() can call it on every attempt.
     */
    void RegisterCommands();

    /** Takes the commands back out. Called from Shutdown(). */
    void UnregisterCommands();

  } // namespace RenderDoc
} // namespace ToolKit
