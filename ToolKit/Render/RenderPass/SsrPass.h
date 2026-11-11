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

  /** Screen space reflection pass UBO (`ssrPassDataInc.shader`). Mirrors the block in that include
      byte for byte. */
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
    /** World -> environment space: the sky cubemap's orientation, inverted. */
    Mat4 envRotation;
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
    /** .x/.y = unused since the sample count drives the temporal blend, .z = brdf bias (how far the
     *  sampled lobe is bent towards the mirror), .w = 1 when the history must be dropped. */
    Vec4 temporal;
    /** .x = 1 when the object motion targets are bound, .y = screen height. */
    Vec4 velocity;
    /** .x = environment intensity, .y = highest mip the cubemap carries, .z = 1 when it is bound. */
    Vec4 environment;
    /** .x = sample cap, .y = disocclusion threshold, .z = history clip weight, .w = default variance. */
    Vec4 denoiser;
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

    /**
     * The two ends of the temporal response. Unused since the accumulation is driven by how many samples
     * a pixel holds, kept so existing scene settings keep loading.
     */
    float TemporalResponseMin          = 0.85f;
    float TemporalResponseMax          = 1.0f;

    /** Screen space motion of the objects, from the velocity pass. Null when the frame has none. */
    TexturePtr VelocityCurrent         = nullptr;
    TexturePtr VelocityPrevious        = nullptr;

    /** How far the sampled lobe is bent towards the mirror direction. 0 = pure GGX, 1 = mirror. */
    float BrdfBias                     = 0.7f;

    /**
     * Pre filtered sky radiance, the reflection a ray falls back to when screen space can not answer it.
     * Without it every miss is a hole the accumulation has to work around; with it the reflection is
     * complete and a miss is simply a coarser answer.
     */
    TexturePtr EnvSpecular             = nullptr;

    /** Orientation of the environment cubemap, as the sky entity carries it. */
    Mat4 EnvRotation                   = Mat4(1.0f);

    /** Intensity of the environment contribution; 0 skips the cubemap. */
    float EnvIntensity                 = 1.0f;

    /** Cap on the samples the accumulation holds, before the roughness scaling. */
    float MaxSamples                   = 32.0f;

    /** How far a history sample may sit from this frame's local statistics before it is faded. */
    float DisocclusionThreshold        = 0.9f;

    /** Multiplier on the clip box the history is pulled into. 0 disables the accumulation. */
    float HistoryClipWeight            = 1.0f;

    /** Output only what this pass contributes, instead of compositing it over the scene color. */
    bool DebugView                     = false;

    /** Which stage the debug view shows, see SSR_DEBUG_* in ssrFrag.shader. */
    int DebugViewMode                  = 0;
  };

  /**
   * Screen space reflections: one ray per pixel, sampled from the surface's reflection lobe, marched
   * against the pre process g buffer.
   *
   * The reflection a pixel ends up with is complete. A ray that finds a surface takes that surface's
   * radiance, a ray that leaves the screen or finds nothing falls back to the environment cubemap, and
   * the confidence of the hit decides how much of each. That is the reference's (FidelityFX SSSR)
   * arrangement and it is what a pixel that misses needs: there is no "no data" state the history has to
   * paper over, so an old reflection can not be kept alive by a ray that no longer finds anything.
   *
   * The accumulation follows the same split as the reference. A mirror is traced along the exact mirror
   * direction and takes this frame's sample as it is - nothing to average, nothing to ghost. A glossy
   * surface goes through the spatial filter, the reprojected history and a variance clipped blend whose
   * weight is how many samples the pixel holds.
   *
   * The composite goes to a target of the pass's own (see GetOutputFramebuffer): compositing back into
   * the frame makes the pass read the target it writes, which a driver that resolves a multisampled
   * render with a draw (Mali) can hand a frame old content.
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
     * Null while the target has no picture of this frame in it: the first frames, where nothing has been
     * sized yet, and a frame the pass skipped because its shader or its parameters were missing. A caller
     * then has to fall back to the frame it gave the pass.
     */
    FramebufferPtr GetOutputFramebuffer() const;

   public:
    SsrPassParams m_params;

   private:
    /** Phase one: one ray per pixel, with the environment filling in whatever the march did not find. */
    FullQuadPassPtr m_quadPass        = nullptr;
    ShaderPtr m_ssrShader             = nullptr;

    /** Build pass for the min depth hierarchy: one draw per level, all with the same shader. */
    FullQuadPassPtr m_pyramidPass     = nullptr;
    ShaderPtr m_pyramidShader         = nullptr;

    /** Phase two: the 1/8 resolution average of the trace, the anchor the temporal clip leans on. */
    FullQuadPassPtr m_avgPass         = nullptr;
    ShaderPtr m_avgShader             = nullptr;

    /** Phase three: spatial filter, reprojection and the variance clipped accumulation. */
    FullQuadPassPtr m_resolvePass     = nullptr;
    ShaderPtr m_resolveShader         = nullptr;

    /** Phase four: composite the accumulated reflection over the scene color. */
    FullQuadPassPtr m_compositePass   = nullptr;
    ShaderPtr m_compositeShader       = nullptr;

    /** Single sample copy of the scene color: the pass reads it while writing a target of its own. */
    RenderTargetPtr m_copyTexture     = nullptr;

    /** Blue noise tile the trace draws its sample from. */
    TexturePtr m_blueNoise            = nullptr;

    /** This frame's reflection and its coverage, produced by the trace phase. */
    RenderTargetPtr m_traceTexture    = nullptr;

    /** Where each ray ended, for the reprojection of the reflected content. */
    RenderTargetPtr m_hitTexture[2]   = {nullptr, nullptr};

    /** Levels of the min depth hierarchy the march climbs, level 0 half the screen. */
    static constexpr int m_pyramidLevels = 6;
    RenderTargetPtr m_depthPyramid[m_pyramidLevels];

    /** Which of the two hit targets this frame writes; the other holds the frame before it. */
    int m_hitIndex                    = 0;

    /** 1/8 resolution average of the trace. */
    RenderTargetPtr m_avgRadiance     = nullptr;

    /** Accumulated reflection (rgb) and its variance (a), one read and one written. */
    RenderTargetPtr m_history[2]      = {nullptr, nullptr};

    /** How many samples the accumulation holds, per pixel, carried next to the radiance. */
    RenderTargetPtr m_historyCount[2] = {nullptr, nullptr};

    /** The g buffer of the frame the history was written for, for the reprojection's disocclusion test. */
    RenderTargetPtr m_prevNormalDepth = nullptr;

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
