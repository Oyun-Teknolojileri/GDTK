/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
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
    FullQuadPassPtr m_pass       = nullptr;
    ShaderPtr m_downsampleShader = nullptr;
    ShaderPtr m_upsampleShader   = nullptr;

    bool m_invalidRenderParams   = false;
    int m_currentIterationCount  = 0;

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