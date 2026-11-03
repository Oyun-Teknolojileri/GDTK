/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file RenderDocCommands.h Module internal: what the module contributes to the
 * editor - its console commands and its Modules menu entries.
 */

namespace ToolKit
{
  namespace RenderDoc
  {

    /**
     * Registers CaptureFrame / CaptureStatus / CaptureOpen and the matching
     * Modules -> RenderDoc entries through the editor API. Idempotent, so Init()
     * can call it on every attempt.
     */
    void RegisterEditorContributions();

    /** Takes the commands and the menu back out. Called from Shutdown(). */
    void UnregisterEditorContributions();

  } // namespace RenderDoc
} // namespace ToolKit
