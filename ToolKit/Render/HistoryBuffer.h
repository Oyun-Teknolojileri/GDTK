/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file HistoryBuffer.h
 * Header for HistoryBuffer, the ping pong a pass accumulates its own result in across frames.
 */

#include "Texture.h"

namespace ToolKit
{

  /**
   * Two targets a screen space pass alternates between to reuse its own result from the last frames.
   *
   * The pass reads `Read()` (the frame before this one) and writes `Write()`, then calls `Swap()` to
   * make this frame's result the one the next frame reads. It exists because that bookkeeping is the
   * same for every such pass while the meaning of the values in it is not: what a pass stores, how it
   * reprojects and what range it clamps a history sample into stay with the pass (see
   * `temporalInc.shader` for the shared half of that).
   *
   * A frame that changes the target size drops the history instead of reprojecting into a frame that
   * no longer matches.
   */
  class TK_API HistoryBuffer
  {
   public:
    /** Reconstructs both targets for the given size and settings. Returns false when the history was
     *  dropped because the size changed. */
    bool Reconstruct(int width, int height, const TextureSettings* settings = nullptr,
                     const String& name = "HistoryRT")
    {
      bool sizeChanged = false;
      for (RenderTargetPtr& target : m_targets)
      {
        sizeChanged |= target == nullptr || target->m_width != width || target->m_height != height;

        if (target == nullptr)
        {
          target = MakeNewPtr<RenderTarget>(width, height,
                                            settings != nullptr ? *settings : TextureSettings(),
                                            name);
        }

        // Called for a target that was just constructed as well: a RenderTarget has no gpu data until
        // something reconstructs it, and a framebuffer attachment of one that has none asserts.
        target->ReconstructIfNeeded(width, height, settings);
      }

      if (sizeChanged)
      {
        m_valid = false;
      }

      return !sizeChanged;
    }

    /** The result of the frames before this one. */
    RenderTargetPtr Read() const { return m_targets[m_write ^ 1]; }

    /** Where this frame's result goes. */
    RenderTargetPtr Write() const { return m_targets[m_write]; }

    /** Makes this frame's result, written into Write(), the one the next frame reads. */
    void Swap()
    {
      m_write ^= 1;
      m_valid = true;
    }

    /** Whether Read() holds anything a pass may reproject into. */
    bool IsValid() const { return m_valid; }

    /** Drops the history, so the next frame starts over from its own result. */
    void Invalidate() { m_valid = false; }

   private:
    RenderTargetPtr m_targets[2];
    int m_write = 0;
    bool m_valid = false;
  };

} // namespace ToolKit
