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

  /** Temporal anti-aliasing pass UBO (`taaFrag.shader`). Mirrors `taaPassDataInc.shader` byte
      for byte. */
  struct TaaPassDataLayout
  {
    /** Clip space of the previous frame times the inverse of this frame's: takes a pixel of the
        frame being drawn to where the surface it shows was on screen in the previous frame. */
    Mat4 reprojection;

    /** The same for the sky: everything at the far plane is at infinity, so it moves with the
        camera's rotation alone and a matrix built from the translation free views is exact. */
    Mat4 skyReprojection;

    /** .xy = 1 / size in pixels (texel size), .zw = size in pixels. */
    Vec4 screenParams;

    /** .x = window depth to NDC z scale, .y = the matching bias (2, -1 on GL and 1, 0 where the
        backend renders with a zero to one depth range). */
    Vec4 depthParams;

    /** .x = weight of the current frame in the resolve, .y = 1 when the history must be dropped,
        .z = NDC depth difference that still counts as the same surface. */
    Vec4 params;
  };

  typedef GpuBufferBase<TaaPassDataLayout> TaaPassDataBuffer;

  struct TaaPassParams
  {
    /** The framebuffer the forward pass drew into. Its color attachment is read, resolved and
        written back in place, and its depth attachment is what the reprojection walks. */
    FramebufferPtr ForwardFrameBuffer = nullptr;

    CameraPtr Cam                     = nullptr;

    /** Fraction of the frame the current sample contributes; the rest is history. 0.1 keeps a
        ten frame tail, which is what turns eight jittered samples per pixel into a filter wide
        enough to remove a highlight that is narrower than a pixel. */
    float blendWeight                 = 0.1f;

    /** NDC depth difference beyond which a reprojected pixel is not the same surface. */
    float depthTolerance              = 0.02f;
  };

  /**
   * Temporal anti-aliasing. Every frame is drawn with the camera projection nudged by a
   * sub-pixel offset (a Halton sequence of eight phases), and the result is resolved against a
   * history buffer that is reprojected through the camera motion. A highlight that is narrower
   * than a pixel can not be drawn by a single frame - it either lands on a sample or misses it,
   * which reads as the shimmer a moving camera produces - so the frame is integrated over
   * several sub-pixel positions instead of being sampled once.
   *
   * The reprojection is camera only: a pixel is mapped to its previous screen position by its
   * own depth. Static geometry under a moving camera - the case a specular highlight crawls in -
   * is exact. A surface that moved relative to the world (a skinned mesh, an animated prop) has
   * no motion of its own here, so the history it would sample is clamped away by the
   * neighborhood test rather than smeared.
   */
  class TK_API TaaPass : public Pass
  {
   public:
    TaaPass();
    virtual ~TaaPass();

    void PreRender() override;
    void Render() override;
    void PostRender() override;

    /** Populates the passive render state every requirement set of this pass shares. */
    void GatherRequirements(PassRequirements& reqs) override;

    /**
     * Advances the jitter sequence and returns this frame's offset in pixels, in the range
     * [-0.5, 0.5]. Call once per frame, before the scene is drawn, and hand the result to
     * Renderer::SetCamera through the forward passes: the offset has to reach the projection the
     * frame is rendered with, or the samples all land on the same spots.
     */
    Vec2 BeginFrame(const CameraPtr& cam);

    /** Drops the history and restarts the jitter sequence on the next BeginFrame. */
    void Reset();

   public:
    TaaPassParams m_params;

   private:
    /** Resolves the current frame against the history and writes the result into m_history[m_write]. */
    void Resolve();

    /** Number of sub-pixel positions the jitter walks. Eight covers a pixel with a 3x3 pattern,
        which is where the extra detail a static camera needs comes from. */
    static constexpr int m_jitterPhaseCount = 8;

    FullQuadPassPtr m_quadPass          = nullptr;
    ShaderPtr m_taaShader               = nullptr;

    /** Previous frame's resolved color. One is sampled, the other is written, and they swap:
        a pass can not read the attachment it draws into. */
    RenderTargetPtr m_history[2];
    int m_historyIndex                  = 0;

    TaaPassDataBuffer m_passDataBuffer;
    bool m_passDataBufferInitialized    = false;

    /** Set once the fragment shader is known to not compile, so the pass stays out of the frame. */
    bool m_shaderUnavailable            = false;

    /** Set when PreRender accepted the frame; Render draws only then. */
    bool m_active                       = false;

    /** The camera the history belongs to, and the matrices the reprojection is built from. */
    ObjectId m_cameraId                 = 0;
    Mat4 m_prevViewProjection           = Mat4(1.0f);
    Mat4 m_currentViewProjection        = Mat4(1.0f);
    Mat4 m_prevViewProjectionSky        = Mat4(1.0f);
    Mat4 m_currentViewProjectionSky     = Mat4(1.0f);
    bool m_hasHistory                   = false;

    /** Set until the next frame consumes it. */
    bool m_resetHistory                 = false;
    bool m_resetRequested               = false;

    /** Position in the jitter sequence. */
    int m_frameIndex                    = 0;

    /** This frame's sub-pixel offset, as BeginFrame chose it. */
    Vec2 m_jitter                       = Vec2(0.0f);

    /** Logged once when the depth attachment can not be sampled (MSAA), to keep the reason visible. */
    bool m_reportedUnsupportedDepth     = false;

    /** Logged once when the pass first resolves a frame, so a run says whether it ran at all. */
    bool m_loggedActivation             = false;
  };

  typedef std::shared_ptr<TaaPass> TaaPassPtr;

} // namespace ToolKit
