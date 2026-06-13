/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "FullQuadPass.h"
#include "Renderer.h"

namespace ToolKit
{

  /** SSAO bilinear 5x5 blur pass UBO (`ssaoBlurFrag.shader`). Single vec2 texel size, padded to
      a vec4 so the std140 layout sits on a 16-byte boundary. */
  struct SsaoBlurPassDataLayout
  {
    /** .xy = 1.0 / textureSize (pixel size in UV). */
    Vec4 texelSizeAndPad;
  };

  typedef GpuBufferBase<SsaoBlurPassDataLayout> SsaoBlurPassDataBuffer;

  /** SSAO calc pass UBO (`ssaoCalcFrag.shader`). Aggregates the 6 bare uniforms the SSAO calc
      shader used to read scatter-style. Sized for the maximum kernel (32 samples) so the same
      buffer works across the 8/16/32 KERNEL_SIZE define variants — the shader's loop iterates up
      to KERNEL_SIZE only, so unused tail entries are harmless.

      std140 quirks captured here:
      - `mat3 normalToView` would take 3×vec4 = 48 bytes with awkward column padding. Stored as
        Mat4 instead; shader extracts via `mat3(ssaoCalc.normalToView)`.
      - `vec3 samples[N]` in std140 already pads each element to 16 bytes — directly using
        `Vec4 samples[32]` matches the GPU view byte-for-byte; shader reads `.xyz`. */
  struct SsaoCalcPassDataLayout
  {
    /** Camera view's rotation as Mat4; shader reads `mat3(normalToView)`. */
    Mat4 normalToView;
    /** Hemisphere kernel — 32 vec4 (max kernel size). Only first KERNEL_SIZE entries consumed. */
    Vec4 samples[32];
    /** (P00, P11, P20, P21) — precomputed projection matrix entries. */
    Vec4 projParams;
    Mat4 inverseProjection;
    /** .x = radius, .y = bias. */
    Vec4 radiusBiasAndPad;
  };

  typedef GpuBufferBase<SsaoCalcPassDataLayout> SsaoCalcPassDataBuffer;

  struct SSAOPassParams
  {
    RenderTargetPtr GNormalDepthBuffer = nullptr;
    CameraPtr Cam                      = nullptr;
    /**
     * How far the samples will be taken from.
     */
    float Radius                       = 0.5f;

    /**
     * Base offset from the sample location.
     */
    float Bias                         = 0.025f;

    /**
     * 0-1 value defining how diverse the samples from the normal.
     */
    float spread                       = 1.0;

    /**
     * Number of samples per pixel. Must be 8, 16 or 32.
     */
    int KernelSize                     = 16;

    /**
     * If true, SSAO is rendered at half resolution for better performance.
     */
    bool HalfResolution                = true;
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

    /** Slot-7 UBO for `ssaoCalcFrag.shader`. Lazy-init on first PreRender; data filled in
        PreRender, Map() called from Render right before m_quadPass renders so slot 7 has the
        calc buffer at draw time (blur Map runs between calc and blur subpasses, see Render). */
    SsaoCalcPassDataBuffer m_calcPassDataBuffer;
    bool m_calcPassDataBufferInitialized = false;

    /** Slot-7 UBO for `ssaoBlurFrag.shader`. Lazy-init on first PreRender. */
    SsaoBlurPassDataBuffer m_blurPassDataBuffer;
    bool m_blurPassDataBufferInitialized     = false;

    int m_currentKernelSize                  = 0;

    static constexpr int m_maximumKernelSize = 32;

    // Used to detect if the spread has changed. If so, kernel updated.
    float m_prevSpread                       = -1.0f;
  };

  typedef std::shared_ptr<SSAOPass> SSAOPassPtr;

} // namespace ToolKit