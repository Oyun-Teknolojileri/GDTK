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

  /** Screen space reflection pass UBO (`ssrFrag.shader`). Mirrors the block in
      `ssrPassDataInc.shader` byte for byte. */
  struct SsrPassDataLayout
  {
    /** World -> view. The g buffer stores world space normals, the march happens in view space. */
    Mat4 view;
    /** Clip -> view, used to rebuild the view position from the stored linear depth. */
    Mat4 inverseProjection;
    /** (P00, P11, P20, P21) - turns a view position into a UV without a matrix multiply. */
    Vec4 projParams;
    /** .x = intensity, .y = maxDistance, .z = thickness, .w = roughness cutoff. */
    Vec4 params;
    /** .xy = 1 / size (texel size), .zw = size in pixels. */
    Vec4 screenParams;
  };

  typedef GpuBufferBase<SsrPassDataLayout> SsrPassDataBuffer;

  struct SsrPassParams
  {
    /** G buffer the reflection rays are marched against: world normal + linear depth + roughness. */
    RenderTargetPtr GNormalDepthBuffer = nullptr;

    /** Scene color. Read through an internal single sample copy, composited back in place. */
    RenderTargetPtr ColorRt            = nullptr;

    CameraPtr Cam                      = nullptr;

    /** How much of the hit color replaces the forward shaded reflection. */
    float Intensity                    = 0.5f;

    /** Ray march length along the reflection direction, in view space units. */
    float MaxDistance                  = 50.0f;

    /** Depth window around a surface hit that still counts as the same surface. */
    float Thickness                    = 0.4f;

    /** Requested march steps. Clamped to the shader's SSR_STEP_COUNT variants. */
    int StepCount                      = 32;

    /** Surfaces rougher than this keep the environment / sky reflection only. */
    float RoughnessCutoff              = 0.6f;
  };

  /**
   * Screen space reflections. Marching a reflection ray per pixel against the g buffer and blending
   * the hit color over the scene color. A miss leaves the pixel exactly as the forward pass shaded
   * it, so the sky or the active environment volumes keep providing the reflection wherever screen
   * space can not: background pixels, off screen rays and rough surfaces.
   */
  class TK_API SsrPass : public Pass
  {
   public:
    SsrPass();
    virtual ~SsrPass();

    void Render() override;
    void PreRender() override;
    void PostRender() override;

    /** Populates the passive render state every requirement set of this pass shares. */
    void GatherRequirements(PassRequirements& reqs) override;

   public:
    SsrPassParams m_params;

   private:
    FullQuadPassPtr m_quadPass       = nullptr;
    ShaderPtr m_ssrShader            = nullptr;

    /** Single sample copy of the scene color: the pass reads it while writing ColorRt in place. */
    RenderTargetPtr m_copyTexture    = nullptr;

    SsrPassDataBuffer m_passDataBuffer;
    bool m_passDataBufferInitialized = false;

    /** SSR_STEP_COUNT the fragment shader was last compiled with (-1 = never). */
    int m_currentStepCount           = -1;
  };

  typedef std::shared_ptr<SsrPass> SsrPassPtr;

} // namespace ToolKit
