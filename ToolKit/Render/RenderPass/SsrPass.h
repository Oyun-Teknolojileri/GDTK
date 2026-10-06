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
    /** .x = number of levels the min depth pyramid holds, .y = pixels a level 1 tile covers. */
    Vec4 hizParams;
    /** View space -> the previous frame's clip. Reprojects this frame's surface to find last frame's
     *  reflection for the same point. */
    Mat4 prevReprojection;
    /** .x = how much of the accumulated history the resolve keeps, .y = 1 when there is a history. */
    Vec4 temporal;
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
    /** Levels the min depth pyramid holds. Level n covers m_hizTilePixels ^ n pixels of the screen. */
    static constexpr int m_hizLevelCount = 4;

    /** Pixels a level 1 tile covers along one edge. Four keeps the level count, the tap count of the
     *  build passes and the sampler count low, while the march still resolves a single g buffer
     *  texel: the finest level of the pyramid is the g buffer itself. */
    static constexpr int m_hizTilePixels = 4;

    FullQuadPassPtr m_quadPass       = nullptr;
    ShaderPtr m_ssrShader            = nullptr;

    /** Nearest depth pyramid of the g buffer, one target per level (`hiZDepthFrag.shader` for level
     *  1, `hiZDownsampleFrag.shader` for the levels above it). The trace phase tests the ray against
     *  a tile of this pyramid instead of against the single depth sample it lands on: a tile the ray
     *  can not cross is stepped over whole, and a tile it can cross is descended into. */
    RenderTargetPtr m_hizLevels[m_hizLevelCount];
    FullQuadPassPtr m_hizPasses[m_hizLevelCount];
    ShaderPtr m_hizDepthShader       = nullptr;
    ShaderPtr m_hizReduceShader      = nullptr;

    /** Second phase: resolve of the trace result, accumulated over the last frames. */
    FullQuadPassPtr m_accumPass      = nullptr;
    ShaderPtr m_accumShader          = nullptr;

    /** Third phase: composite the resolved reflection over the scene color. */
    FullQuadPassPtr m_filterPass     = nullptr;
    ShaderPtr m_filterShader         = nullptr;

    /** The resolved reflection of the frames before this one, ping ponged: a screen space reflection
     *  is one sample per pixel per frame, so reusing the last frames is what keeps it from
     *  shimmering as the camera or the geometry moves. */
    RenderTargetPtr m_history[2];
    int m_historyWrite               = 0;
    bool m_historyValid              = false;

    /** View projection and view of the frame before this one, for the reprojection. */
    Mat4 m_prevViewProj              = Mat4(1.0f);
    Mat4 m_prevView                  = Mat4(1.0f);

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

    /** How much of the accumulated reflection the resolve keeps each frame. High enough to smooth the
     *  per frame sampling, low enough that a moving reflection still follows in a few frames. */
    static constexpr float m_temporalBlend = 0.85f;
  };

  typedef std::shared_ptr<SsrPass> SsrPassPtr;

} // namespace ToolKit
