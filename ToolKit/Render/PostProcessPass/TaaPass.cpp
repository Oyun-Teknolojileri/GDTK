/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "TaaPass.h"

#include "Camera.h"
#include "MathUtil.h"
#include "Shader.h"
#include "Stats.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  namespace
  {
    /**
     * One coordinate of the Halton sequence at the given index. Index zero is skipped by the
     * callers: Halton(0) is zero for every base, which would repeat the previous phase.
     */
    float Halton(int index, int base)
    {
      float result   = 0.0f;
      float fraction = 1.0f;

      while (index > 0)
      {
        fraction /= float(base);
        result   += float(index % base) * fraction;
        index    /= base;
      }

      return result;
    }

    /**
     * Window depth to NDC z as a (scale, bias) pair: ndc = depth * scale + bias. The GL backend
     * renders with the OpenGL convention, where the near plane is at -1. The Vulkan backend keeps
     * the zero to one range its viewport declares (minDepth 0, maxDepth 1), so its depth is
     * already the NDC value.
     */
    Vec2 DepthToNdc()
    {
#ifdef TK_VULKAN
      return Vec2(1.0f, 0.0f);
#else
      return Vec2(2.0f, -1.0f);
#endif
    }

    /**
     * The view matrix with its translation removed, which is how the engine draws the sky: a point
     * at infinity has no parallax, so only the rotation decides where it lands.
     */
    Mat4 TranslationFree(const Mat4& view)
    {
      Mat4 result  = view;
      result[0][3] = 0.0f;
      result[1][3] = 0.0f;
      result[2][3] = 0.0f;
      result[3][3] = 1.0f;
      result[3][0] = 0.0f;
      result[3][1] = 0.0f;
      result[3][2] = 0.0f;

      return result;
    }
  } // namespace

  TaaPass::TaaPass() : Pass("TaaPass")
  {
    m_taaShader = GetShaderManager()->Create<Shader>(ShaderPath("taaFrag.shader", true));

    m_quadPass                       = MakeNewPtr<FullQuadPass>();
    m_quadPass->m_params.frameBuffer = MakeNewPtr<Framebuffer>("TaaFB");

    m_history[0] = MakeNewPtr<RenderTarget>("TaaHistory0");
    m_history[1] = MakeNewPtr<RenderTarget>("TaaHistory1");
  }

  TaaPass::~TaaPass()
  {
    m_quadPass       = nullptr;
    m_taaShader      = nullptr;
    m_history[0]     = nullptr;
    m_history[1]     = nullptr;
  }

  void TaaPass::Reset()
  {
    m_resetRequested = true;
    m_hasHistory     = false;
  }

  Vec2 TaaPass::BeginFrame(const CameraPtr& cam)
  {
    const ObjectId camId = cam ? cam->GetIdVal() : 0;
    if (camId != m_cameraId)
    {
      m_cameraId    = camId;
      m_resetHistory = true;
      m_hasHistory  = false;
    }

    if (m_resetRequested)
    {
      m_resetRequested = false;
      m_resetHistory   = true;
      m_hasHistory     = false;
    }

    if (m_resetHistory)
    {
      m_frameIndex = 0;
    }

    // Phase zero is skipped: the first sample of the sequence sits on the pixel center, and a
    // frame that covers only the center adds nothing to what the rasterizer already does.
    const int phase = m_frameIndex % m_jitterPhaseCount;
    m_frameIndex++;

    m_jitter = Vec2(Halton(phase + 1, 2) - 0.5f, Halton(phase + 1, 3) - 0.5f);
    return m_jitter;
  }

  void TaaPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    m_active = false;

    if (m_params.ForwardFrameBuffer == nullptr || m_params.Cam == nullptr)
    {
      return;
    }

    if (m_shaderUnavailable)
    {
      return;
    }

    Framebuffer* forwardFb  = m_params.ForwardFrameBuffer.get();
    RenderTargetPtr colorRt = forwardFb->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);
    DepthTexturePtr depthRt = forwardFb->GetDepthTexture();

    if (colorRt == nullptr || depthRt == nullptr)
    {
      return;
    }

    // A multisampled depth attachment is a renderbuffer with no texture behind it, and neither
    // GL ES 3.0 nor WebGL 2 can sample one. The scene path keeps TAA off while MSAA is on for
    // exactly this reason; the guard stays so a host that turns MSAA on at runtime does not draw
    // against a depth that reads zeroes.
    if (depthRt->IsMultiSampled())
    {
      if (!m_reportedUnsupportedDepth)
      {
        TK_WRN("TaaPass: the frame's depth attachment is multisampled and can not be sampled. "
               "Temporal anti-aliasing needs MSAA off; the pass stays out of the frame.");
        m_reportedUnsupportedDepth = true;
      }

      return;
    }

    m_taaShader->Init();

    // A shader that carries defines leaves Init without gpu data on purpose; this one carries
    // none, so a null gpu data here really is a compile failure that Init already reported.
    if (m_taaShader->m_defineArray.empty() && m_taaShader->m_gpuData == nullptr)
    {
      TK_ERR("TaaPass: the fragment shader did not compile, temporal anti-aliasing is disabled.");
      m_shaderUnavailable = true;
      return;
    }

    m_quadPass->SetFragmentShader(m_taaShader, GetRenderer());

    if (!m_passDataBufferInitialized)
    {
      m_passDataBuffer.Init(7);
      m_passDataBufferInitialized = true;
    }

    const int width  = colorRt->m_width;
    const int height = colorRt->m_height;

    if (width <= 0 || height <= 0)
    {
      return;
    }

    // The history carries the same format and filtering as the frame it accumulates: half float
    // so a highlight above one survives the round trip, linear so the reprojected sample lands
    // between texels instead of snapping to one.
    TextureSettings historySet = colorRt->Settings();
    historySet.msaaCount       = MsaaSampleCount::x0;
    historySet.GenerateMipMap  = false;
    historySet.MinFilter       = GraphicTypes::SampleLinear;
    historySet.MagFilter       = GraphicTypes::SampleLinear;
    historySet.WarpS           = GraphicTypes::UVClampToEdge;
    historySet.WarpT           = GraphicTypes::UVClampToEdge;

    if (m_history[0]->m_width != width || m_history[0]->m_height != height)
    {
      m_history[0]->ReconstructIfNeeded(width, height, &historySet);
      m_history[1]->ReconstructIfNeeded(width, height, &historySet);

      // A resized target has nothing in common with the frame that filled the old one.
      m_resetHistory = true;
      m_hasHistory   = false;
    }

    m_quadPass->m_params.frameBuffer->ReconstructIfNeeded({width, height, false, false});
    m_quadPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                         m_history[1 - m_historyIndex]);
    m_quadPass->m_params.clearFrameBuffer = GraphicBitFields::None;

    const Mat4 view                = m_params.Cam->GetViewMatrix();
    const Mat4 projection          = m_params.Cam->GetProjectionMatrix();
    const Mat4 jitteredProjection  = GetRenderer()->BuildJitteredProjection(projection, m_jitter);

    // The matrix the history is sampled with carries no jitter: the history is a pixel grid and
    // what the jittered samples average to is the image that grid would hold.
    m_currentViewProjection    = projection * view;
    m_currentViewProjectionSky = projection * TranslationFree(view);

    // With no usable history the matrices would multiply a history that describes some other
    // frame, so the shader is told to trust the current frame and they stay the identity.
    const bool hasHistory = m_hasHistory && !m_resetHistory;
    Mat4 reprojection     = Mat4(1.0f);
    Mat4 skyReprojection  = Mat4(1.0f);
    if (hasHistory)
    {
      // The depth buffer holds samples taken through the jittered projection, so the unprojection
      // has to use that same matrix: reconstructing with the unjittered one puts the surface point
      // back by up to the jitter, by an amount that changes with the phase, and that is what reads
      // as the image swimming under water.
      reprojection = m_prevViewProjection * glm::inverse(jitteredProjection * view);

      // Everything at the far plane is at infinity, so it moves with the rotation alone. An
      // unjittered depth of one identifies those pixels, and a translation free matrix carries
      // them exactly, including the ones the sky shares an edge with.
      skyReprojection = m_prevViewProjectionSky * glm::inverse(jitteredProjection * TranslationFree(view));
    }

    const Vec2 depthToNdc = DepthToNdc();

    m_passDataBuffer.m_data.reprojection    = reprojection;
    m_passDataBuffer.m_data.skyReprojection = skyReprojection;
    m_passDataBuffer.m_data.screenParams    = Vec4(1.0f / float(width),
                                                   1.0f / float(height),
                                                   float(width),
                                                   float(height));
    m_passDataBuffer.m_data.depthParams     = Vec4(depthToNdc.x, depthToNdc.y, 0.0f, 0.0f);
    m_passDataBuffer.m_data.params          = Vec4(glm::clamp(m_params.blendWeight, 0.01f, 1.0f),
                                                   (hasHistory ? 0.0f : 1.0f),
                                                   glm::max(m_params.depthTolerance, 0.0f),
                                                   0.0f);

    m_passDataBuffer.Invalidate();
    m_passDataBuffer.Map();

    if (!m_loggedActivation)
    {
      TK_LOG("TaaPass: resolving %dx%d at %.2f history weight.", width, height, m_params.blendWeight);
      m_loggedActivation = true;
    }

    m_active = true;
  }

  void TaaPass::Render()
  {
    TK_PROFILE_FUNCTION();

    if (!m_active)
    {
      return;
    }

    Renderer* renderer      = GetRenderer();
    Framebuffer* forwardFb  = m_params.ForwardFrameBuffer.get();
    RenderTargetPtr colorRt = forwardFb->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0);
    DepthTexturePtr depthRt = forwardFb->GetDepthTexture();

    m_quadPass->SetFragmentShader(m_taaShader, renderer);

    m_requirements                                    = PassRequirements();
    GatherRequirements(m_requirements);
    m_requirements.fragmentShader                     = m_taaShader;
    m_requirements.vertexShader                       = m_quadPass->m_material->GetVertexShaderVal();
    m_requirements.program                            = m_quadPass->GetProgram();
    m_requirements.frameBuffer                        = m_quadPass->m_params.frameBuffer;
    m_requirements.semanticTextures["s_color"]        = colorRt;
    m_requirements.semanticTextures["s_depth"]        = depthRt;
    m_requirements.semanticTextures["s_history"]      = m_history[m_historyIndex];
    m_requirements.customUbos[7]                      = &m_passDataBuffer.GetBuffer();

    ApplyRequirements(renderer);
    RenderSubPass(m_quadPass);

    // The resolve sits next to the buffer it fed, not in it: the color attachment could not be
    // sampled while it was being written, so the result travels back with a copy. Pointing the
    // passes behind this one at the history target instead would save that copy, and is what a
    // later version should do once the post chain names its source in one place.
    renderer->CopyTexture(m_history[1 - m_historyIndex], colorRt);

    m_prevViewProjection    = m_currentViewProjection;
    m_prevViewProjectionSky = m_currentViewProjectionSky;
    m_historyIndex          = 1 - m_historyIndex;
    m_hasHistory            = true;
    m_resetHistory          = false;
  }

  void TaaPass::GatherRequirements(PassRequirements& reqs)
  {
    // Full screen composite over an already rendered color buffer: no depth interaction at all.
    reqs.passState.depthTestEnabled  = false;
    reqs.passState.depthWriteEnabled = false;
    reqs.passState.depthFunction     = CompareFunctions::FuncAlways;
  }

  void TaaPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PostRender();
  }

} // namespace ToolKit
