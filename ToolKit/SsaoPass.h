/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "FullQuadPass.h"

namespace ToolKit
{

  struct SSAOPassParams
  {
    RenderTargetPtr GNormalDepthBuffer = nullptr;
    CameraPtr Cam                      = nullptr;
    /**
     * How far the samples will be taken from.
     */
    float Radius                  = 0.5f;

    /**
     * Base offset from the sample location.
     */
    float Bias                    = 0.025f;

    /**
     * 0-1 value defining how diverse the samples from the normal.
     */
    float spread                  = 1.0;

    /**
     * Number of samples per pixel. Must be 8, 16 or 32.
     */
    int KernelSize                = 16;

    /**
     * If true, SSAO is rendered at half resolution for better performance.
     */
    bool HalfResolution           = true;
  };

  class TK_API SSAOPass : public Pass
  {
   public:
    SSAOPass();
    explicit SSAOPass(const SSAOPassParams& params);
    ~SSAOPass();

    void Render();
    void PreRender();
    void PostRender();

   private:
    void GenerateSSAONoise();

   public:
    SSAOPassParams m_params;
    RenderTargetPtr m_ssaoTexture = nullptr;

   private:
    Vec3Array m_ssaoKernel;

    FramebufferPtr m_ssaoFramebuffer = nullptr;
    FramebufferPtr m_blurFramebuffer = nullptr;
    RenderTargetPtr m_rawSsaoRt      = nullptr;

    FullQuadPassPtr m_quadPass       = nullptr;
    FullQuadPassPtr m_blurPass       = nullptr;
    ShaderPtr m_ssaoShader           = nullptr;
    ShaderPtr m_blurShader           = nullptr;

    int m_currentKernelSize          = 0;

    static constexpr int m_maximumKernelSize = 32;

    // Used to detect if the spread has changed. If so, kernel updated.
    float m_prevSpread               = -1.0f;

    static StringArray m_ssaoSamplesStrCache;
    static constexpr int m_ssaoSamplesStrCacheSize = 32;
  };

  typedef std::shared_ptr<SSAOPass> SSAOPassPtr;

} // namespace ToolKit