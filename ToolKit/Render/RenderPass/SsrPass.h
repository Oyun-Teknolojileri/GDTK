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
    /** View -> world, used to carry the surface pixel back to where it was last frame. */
    Mat4 inverseView;
    /** Previous frame's view projection, for the reprojection of the reflection history. */
    Mat4 prevViewProjection;
    /** (P00, P11, P20, P21) - turns a view position into a UV without a matrix multiply. */
    Vec4 projParams;
    /** .x = intensity, .y = maxDistance, .z = thickness, .w = roughness cutoff. */
    Vec4 params;
    /** .xy = 1 / size (texel size), .zw = size in pixels. */
    Vec4 screenParams;
    /** .x = debug view, .y = march step count (the requested count clamped to the loop bound),
     *  .z = debug view mode (see SSR_DEBUG_* in ssrFrag.shader), .w = frame index (sample seed). */
    Vec4 flags;
    /** .x = screen border fade, .y = screen length fade start, .z = hit distance fade start. */
    Vec4 fades;
    /** .x = history weight, .y = 1 when the history must be dropped,
     *  .z = brdf bias (how far the sampled lobe is bent towards the mirror), .w = temporal enabled. */
    Vec4 temporal;
  };

  typedef GpuBufferBase<SsrPassDataLayout> SsrPassDataBuffer;

  struct SsrPassParams
  {
    /** G buffer the reflection rays are marched against: world normal + linear depth + roughness. */
    RenderTargetPtr GNormalDepthBuffer = nullptr;

    /**
     * Scene color, the input of the pass only. It is read through an internal single sample copy and
     * never written: the composite goes to a target of the pass's own, see GetOutputFramebuffer.
     */
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

    /** Screen border the reflections fade out across, as a fraction of the frame's width plus height. */
    float EdgeFade                     = 0.05f;

    /** Where the fade along the ray's screen length starts, as a fraction of the length it can reach. */
    float ScreenFadeStart              = 0.5f;

    /** Where the fade along the hit distance starts, as a fraction of MaxDistance. */
    float RangeFadeStart               = 0.6f;

    /** Weight the previous frame keeps in the accumulation. 0 turns the temporal resolve off. */
    float HistoryWeight                = 0.95f;

    /** How far the sampled lobe is bent towards the mirror direction. 0 = pure GGX, 1 = mirror. */
    float BrdfBias                     = 0.7f;

    /** Output only what this pass contributes, instead of compositing it over the scene color. */
    bool DebugView                     = false;

    /** Which stage the debug view shows, see SSR_DEBUG_* in ssrFrag.shader. */
    int DebugViewMode                  = 0;
  };

  /**
   * Screen space reflections with stochastic sampling. Every pixel shoots one ray sampled from the
   * surface's reflection lobe instead of the mirror direction, so roughness becomes a real blur: the
   * lobe the sample lands in is what the pixel reflects, not a mip of the mirror image. The samples
   * are averaged in screen space and accumulated over frames against a history that follows the
   * surface through the camera motion.
   *
   * A miss leaves the pixel exactly as the forward pass shaded it, so the sky or the active
   * environment volumes keep providing the reflection wherever screen space can not: background
   * pixels, off screen rays and surfaces whose lobe points away from the screen.
   *
   * The composite goes to a target of the pass's own (see GetOutputFramebuffer): compositing back
   * into the frame makes the pass read the target it writes, which a driver that resolves a
   * multisampled render with a draw (Mali) can hand a frame old content, and the previous frame's
   * reflections then alternate with the current one.
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

    /**
     * Where the composite of this pass goes: the frame's color with the reflections in it.
     *
     * A target of the pass's own, instead of the frame ColorRt names. Compositing back into that frame
     * makes the pass read the very target it writes, and a driver that resolves a multisampled render to
     * texture with a draw (Mali) can hand the copy a frame old content: whatever the previous frame left
     * there then comes back as a reflection of itself, alternating frame by frame. With the composite in
     * a target of its own the frame stays read only for the rest of the frame, and everything behind this
     * pass reads this target instead.
     *
     * Null while the target has no picture of this frame in it: the first frames, where nothing has been
     * sized yet, and a frame the pass skipped because its shader or its parameters were missing. A caller
     * then has to fall back to the frame it gave the pass.
     */
    FramebufferPtr GetOutputFramebuffer() const;

   public:
    SsrPassParams m_params;

   private:
    /** Phase one: one stochastic ray per pixel, gathered into the scene color mip chain. */
    FullQuadPassPtr m_quadPass        = nullptr;
    ShaderPtr m_ssrShader             = nullptr;

    /** Phase two: screen space resolve of the samples plus the temporal accumulation. */
    FullQuadPassPtr m_resolvePass     = nullptr;
    ShaderPtr m_resolveShader         = nullptr;

    /** Phase three: composite the accumulated reflection over the scene color. */
    FullQuadPassPtr m_compositePass   = nullptr;
    ShaderPtr m_compositeShader       = nullptr;

    /** Single sample copy of the scene color: the pass reads it while writing a target of its own. */
    RenderTargetPtr m_copyTexture     = nullptr;

    /** This frame's reflection and its weight, produced by the trace phase. */
    RenderTargetPtr m_traceTexture    = nullptr;

    /** Accumulated reflection, one is read and the other is written, and they swap. */
    RenderTargetPtr m_history[2];

    /** The color the composite phase writes: the frame with the reflections in it. */
    RenderTargetPtr m_reflectionRt    = nullptr;
    FramebufferPtr m_reflectionFb     = nullptr;

    /** Index of the history texture the previous frame left its accumulation in. */
    int m_historyIndex                = 0;

    /** Set once a frame was accumulated, so the first frame after a reset starts clean. */
    bool m_hasHistory                 = false;

    /** The camera the history belongs to; a different camera can not reuse it. */
    ObjectId m_historyCameraId        = 0;

    /** Size the history targets were last built for. */
    IVec2 m_historySize               = IVec2(0);

    /** View projection of the last frame that accumulated, for the reprojection. */
    Mat4 m_prevViewProjection         = Mat4(1.0f);

    /** Set when the view changed since the last accumulated frame: re-rolls the samples and widens the resolve. */
    bool m_viewMoved                  = false;

    /** Seed of this frame's sample sequence. Wraps, so the offset stays exact in a float. */
    float m_frameIndex                = 0.0f;

    /** Set once the composite phase left a composite of this frame in m_reflectionFb. */
    bool m_outputWritten              = false;

    SsrPassDataBuffer m_passDataBuffer;
    bool m_passDataBufferInitialized  = false;

    /** Set once a fragment shader is known to not compile, so the pass stays out of the frame. */
    bool m_shaderUnavailable          = false;

    /** Highest SSR_MAX_STEPS the fragment shader is compiled with. */
    static constexpr int m_maxStepCount = 512;

    /** SSR_MAX_STEPS the fragment shader was last compiled with (-1 = never). */
    int m_currentMaxSteps             = -1;
  };

  typedef std::shared_ptr<SsrPass> SsrPassPtr;

} // namespace ToolKit
