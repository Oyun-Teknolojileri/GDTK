/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "FullQuadPass.h"
#include "Pass.h"
#include "Renderer.h"

namespace ToolKit
{

  /** BloomPass UBO. Shared by `bloomDownsample.shader` (filter + downsample chain) and
      `bloomUpsample.shader` (upsample chain + merge); each shader reads only the half it
      needs. Pass fills the relevant fields per iteration and re-Map()s the buffer. */
  struct BloomPassDataLayout
  {
    /** .xy = srcResolution (downsample), .z = threshold, .w = pad. */
    Vec4 downsampleParams;
    /** .x = filterRadius, .y = intensity (upsample). */
    Vec4 upsampleParams;
    /** .x = passIndx (downsample). 0 = filter pass with prefilter; 1 = first downsample with
        Karis average; \u22652 = regular weighted downsample. */
    IVec4 passIndxAndPad;
  };

  typedef GpuBufferBase<BloomPassDataLayout> BloomPassDataBuffer;

  struct BloomPassParams
  {
    FramebufferPtr FrameBuffer = nullptr;
    int iterationCount         = 6;
    float minThreshold         = 1.0f;
    float intensity            = 1.0f;
  };

  class TK_API BloomPass : public Pass
  {
   public:
    BloomPass();

    void Render() override;
    void PreRender() override;
    void PostRender() override;

   public:
    BloomPassParams m_params;

   private:
    // Iteration Count + 1 number of textures & framebuffers
    RenderTargetPtrArray m_resampleRenderTargets;
    FramebufferPtrArray m_resampleFrameBuffers;

    /** Two separate full-quad passes — one for the downsample chain, one for the upsample
     *  chain. Sharing a single m_pass worked when SetFragmentShader re-bound the program
     *  on every call, but refactor + backend shadow-state quirks let the downsample
     *  program occasionally linger into the upsample draw, leading to wrong shader
     *  output. Two quad instances guarantee each phase binds the correct program. */
    FullQuadPassPtr m_downPass   = nullptr;
    FullQuadPassPtr m_upPass     = nullptr;
    ShaderPtr m_downsampleShader = nullptr;
    ShaderPtr m_upsampleShader   = nullptr;

    bool m_invalidRenderParams   = false;
    int m_currentIterationCount  = 0;

    // Once the backend is alive (first PreRender), pin each quad to its fragment
    // shader so the program attached to m_downPass is the downsample program and
    // m_upPass is the upsample program — no more SetFragmentShader swaps during
    // Render, which is what let the downsample program linger into the upsample draw.
    bool m_fragmentsPinned       = false;

    // Render target caches.
    UVec2 m_cachedMainRes;
    int m_cachedIterCount;
    bool m_resourcesValid;

    /** Pass-specific UBO (slot 7). Holds both the downsample and upsample parameter halves;
        each iteration writes the relevant fields and re-Map()s. */
    BloomPassDataBuffer m_passDataBuffer;
    bool m_passDataBufferInitialized = false;
  };

  typedef std::shared_ptr<BloomPass> BloomPassPtr;

} // namespace ToolKit