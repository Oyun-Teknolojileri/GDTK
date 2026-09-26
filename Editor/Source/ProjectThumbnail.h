/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file ProjectThumbnail.h Header for the project thumbnail the launcher shows.
 */

namespace ToolKit
{
  namespace Editor
  {

    /** Side of the square PNG a project's card shows in the launcher. */
    const int ProjectThumbnailSize = 512;

    /**
     * Writes the thumbnail of the active project, the `thumbnail.png` the launcher shows on the
     * project's card, next to the project's Resources folder.
     *
     * The image is the last one the active viewport was drawn into -- the resolved texture when the
     * viewport is multisampled, which is the one the editor shows -- cropped to its center square so
     * the thumbnail is not distorted, and scaled to ProjectThumbnailSize.
     *
     * The pixels are read back in a render task, where the graphics context is the one the engine
     * renders with, and the file is written from that task's callback. The call itself only queues
     * the work, so it is safe to run from the console (or any UI code) in the middle of a frame.
     */
    void SaveProjectThumbnail();

  } // namespace Editor
} // namespace ToolKit
