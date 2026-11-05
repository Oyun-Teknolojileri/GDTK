/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "FullQuadPass.h"
#include "Renderer.h"

namespace ToolKit
{

  /** BloomPass UBO. Shared by `bloomGlow.shader` (bright pass) and `bloomComposite.shader`
      (the weighted sum of the glow pyramid). Mirrors bloomPassDataInc.shader byte for byte. */
  struct BloomPassDataLayout
  {
    /** .x = threshold, .y = HDR luminance ceiling. */
    Vec4 params;
    /** .x = intensity of the glow added to the frame, .y = 1 / sum of the level weights. */
    Vec4 composite;
    /** Radius shaped weight of mip level 0, 1, 2 and 3 of the glow pyramid. */
    Vec4 weightsA;
    /** Radius shaped weight of mip level 4 and 5. */
    Vec4 weightsB;
  };

  typedef GpuBufferBase<BloomPassDataLayout> BloomPassDataBuffer;

  struct BloomPassParams
  {
    /** The frame the glow is read from and added to. */
    FramebufferPtr FrameBuffer = nullptr;

    /** Number of glow levels the composite sums, 1 to 6. Each level is a factor of two wider
        than the one below, so this is the reach of the halo. */
    int iterationCount         = 5;

    /** How the energy is distributed over those levels. 1 gives every level the same weight, which is
        the widest halo the count allows; lower values pull the energy toward the tight levels with a
        Gaussian falloff, so the halo shrinks without the reach changing and no level pops on or off at
        a boundary. Weights are normalized, so the radius moves the shape and not the brightness. */
    float radius               = 1.0f;

    /** Bicubic reconstruction of a level in the composite. Four bilinear taps either way, so the cost
        is the extra arithmetic; off falls back to the 2x2 box the pyramid used before. */
    bool bicubicUpscale        = true;

    float minThreshold         = 1.0f;
    float intensity            = 1.0f;

    /** HDR luminance ceiling applied before the bright pass averages its samples. Without it a
        single very bright pixel (a specular glint, an emissive surface) owns the whole halo
        around it and the frame looks blown out. */
    float luminanceCap         = 10.0f;
  };

  /**
   * Bloom as a pyramid rather than a chain of passes.
   *
   * A bright pass reduces the frame to a quarter of its size in each axis and writes level zero.
   * The texture's mip chain holds every level above it, each one the box average of the level
   * below, and the composite sums a few of those levels at the frame's resolution: level zero is
   * the tight halo against the source, the wide levels carry the spread. The iteration count is the
   * reach of that sum, the radius shapes how much of the energy each level carries, and the composite
   * reads its levels with a bicubic filter so a wide level does not show its texels. There is no
   * downsample and upsample chain, so the cost is one small pass and one full screen pass instead of
   * two passes per level.
   */
  class TK_API BloomPass : public Pass
  {
   public:
    BloomPass();

    void Render() override;
    void PreRender() override;
    void PostRender() override;

    /** The bright pass result, mip mapped: level i is the halo at 2^i the glow resolution. */
    RenderTargetPtr GetGlowTexture() const { return m_glowRt; }

   public:
    BloomPassParams m_params;

   private:
    /** Bright pass: the frame's 4x4 blocks averaged, thresholded and capped, into level zero. */
    FullQuadPassPtr m_glowPass       = nullptr;
    ShaderPtr m_glowShader           = nullptr;

    /** The weighted sum of the glow pyramid, added over the frame. */
    FullQuadPassPtr m_compositePass  = nullptr;
    ShaderPtr m_compositeShader      = nullptr;

    /** Level zero of the pyramid and the framebuffer it is drawn into. */
    RenderTargetPtr m_glowRt         = nullptr;
    FramebufferPtr m_glowFb          = nullptr;

    /** Size of level zero, so a resize rebuilds the pyramid. */
    UVec2 m_cachedGlowRes            = UVec2(0, 0);

    /** Pass-specific UBO (slot 7): bright pass and composite parameters. */
    BloomPassDataBuffer m_passDataBuffer;
    bool m_passDataBufferInitialized = false;
  };

  typedef std::shared_ptr<BloomPass> BloomPassPtr;

} // namespace ToolKit
