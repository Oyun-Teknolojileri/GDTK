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
    /** .x = debug view, .y = march step count (used exactly as requested, never rounded),
     *  .z = debug view mode (see SSR_DEBUG_* in ssrFrag.shader). */
    Vec4 flags;
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

    /** March steps. Used exactly as given; the shader's SSR_MAX_STEPS only bounds the loop. */
    int StepCount                      = 32;

    /** Surfaces rougher than this keep the environment / sky reflection only. */
    float RoughnessCutoff              = 0.6f;

    /** Output only what this pass contributes, instead of compositing it over the scene color. */
    bool DebugView                     = false;

    /** Which stage the debug view shows, see SSR_DEBUG_* in ssrFrag.shader. */
    int DebugViewMode                  = 0;
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

    /** Second phase: screen space filter and resolve of the trace result. */
    FullQuadPassPtr m_filterPass     = nullptr;
    ShaderPtr m_filterShader         = nullptr;

    /** Single sample copy of the scene color: the pass reads it while writing ColorRt in place. */
    RenderTargetPtr m_copyTexture    = nullptr;

    /** Reflection and its weight, produced by the trace phase and filtered by the resolve phase. */
    RenderTargetPtr m_traceTexture   = nullptr;

    SsrPassDataBuffer m_passDataBuffer;
    bool m_passDataBufferInitialized = false;

    /** Set once the fragment shader is known to not compile, so the pass stays out of the frame. */
    bool m_shaderUnavailable         = false;

    /** Highest SSR_MAX_STEPS the fragment shader is compiled with. */
    static constexpr int m_maxStepCount = 512;

    /** SSR_MAX_STEPS the fragment shader was last compiled with (-1 = never). */
    int m_currentMaxSteps            = -1;
  };

  typedef std::shared_ptr<SsrPass> SsrPassPtr;

} // namespace ToolKit
