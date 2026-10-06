/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "SsrPass.h"

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
    /** SSR_STEP_COUNT variants the fragment shader is compiled for. */
    int ClampStepCount(int steps)
    {
      if (steps <= 8)
      {
        return 8;
      }

      if (steps <= 16)
      {
        return 16;
      }

      return steps <= 32 ? 32 : 64;
    }
  } // namespace

  SsrPass::SsrPass() : Pass("SsrPass")
  {
    m_quadPass                       = MakeNewPtr<FullQuadPass>();
    m_quadPass->m_params.frameBuffer = MakeNewPtr<Framebuffer>("SsrFB");
    m_ssrShader                      = GetShaderManager()->Create<Shader>(ShaderPath("ssrFrag.shader", true));
    m_copyTexture                    = MakeNewPtr<RenderTarget>("SsrSceneCopyRT");
  }

  SsrPass::~SsrPass()
  {
    m_quadPass    = nullptr;
    m_ssrShader   = nullptr;
    m_copyTexture = nullptr;
  }

  void SsrPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    if (m_params.ColorRt == nullptr || m_params.GNormalDepthBuffer == nullptr || m_params.Cam == nullptr)
    {
      return;
    }

    // The pass reads the scene color and composites the result back into the same target, so it
    // samples a single sample copy of it (the same trick DoFPass uses).
    TextureSettings copySet = m_params.ColorRt->Settings();
    copySet.msaaCount       = MsaaSampleCount::x0;
    m_copyTexture->ReconstructIfNeeded(m_params.ColorRt->m_width, m_params.ColorRt->m_height, &copySet);

    GetRenderer()->CopyTexture(m_params.ColorRt, m_copyTexture);

    m_quadPass->SetFragmentShader(m_ssrShader, GetRenderer());

    if (!m_passDataBufferInitialized)
    {
      m_passDataBuffer.Init(7);
      m_passDataBufferInitialized = true;
    }

    const Mat4& proj                          = m_params.Cam->GetProjectionMatrix();
    const IVec2 size(m_copyTexture->m_width, m_copyTexture->m_height);

    m_passDataBuffer.m_data.view              = m_params.Cam->GetViewMatrix();
    m_passDataBuffer.m_data.inverseProjection = glm::inverse(proj);

    // clip.x = P00 * x + P20 * z, clip.y = P11 * y + P21 * z, clip.w = -z
    m_passDataBuffer.m_data.projParams        = Vec4(proj[0][0], proj[1][1], proj[2][0], proj[2][1]);

    m_passDataBuffer.m_data.params            = Vec4(glm::max(m_params.Intensity, 0.0f),
                                                     glm::max(m_params.MaxDistance, 0.01f),
                                                     glm::max(m_params.Thickness, 0.001f),
                                                     glm::clamp(m_params.RoughnessCutoff, 0.0f, 1.0f));

    // Texel and screen size: the shader needs both for the hit gather, the screen edge fade and the
    // ray length fade in pixels.
    m_passDataBuffer.m_data.screenParams      = Vec4(1.0f / float(size.x),
                                                     1.0f / float(size.y),
                                                     float(size.x),
                                                     float(size.y));

    // The step count lives in a shader define (like SSAO's KERNEL_SIZE), so a change recompiles.
    const int steps                           = ClampStepCount(m_params.StepCount);
    if (steps != m_currentStepCount)
    {
      m_ssrShader->Init();
      m_ssrShader->SetDefine("SSR_STEP_COUNT", std::to_string(steps));
      m_currentStepCount = steps;
    }

    m_quadPass->m_params.frameBuffer->ReconstructIfNeeded({size.x, size.y, false, false});
    m_quadPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                         m_params.ColorRt);
    m_quadPass->m_params.blendFunc        = BlendFunction::NONE;
    m_quadPass->m_params.clearFrameBuffer = GraphicBitFields::None;
  }

  void SsrPass::Render()
  {
    TK_PROFILE_FUNCTION();

    if (m_params.ColorRt == nullptr || m_params.GNormalDepthBuffer == nullptr || m_params.Cam == nullptr)
    {
      return;
    }

    Renderer* renderer = GetRenderer();

    // The shader samples the g buffer with a sampler2D, so MSAA has to be resolved away first,
    // exactly like SSAOPass and DoFPass do.
    TexturePtr normalDepth = m_params.GNormalDepthBuffer;
    if (normalDepth->IsMultiSampled())
    {
      normalDepth = m_params.GNormalDepthBuffer->GetResolvedTexture();
    }

    m_quadPass->SetFragmentShader(m_ssrShader, renderer);

    GatherRequirements(m_requirements);
    m_requirements.fragmentShader                   = m_ssrShader;
    m_requirements.vertexShader                     = m_quadPass->m_material->GetVertexShaderVal();
    m_requirements.program                          = m_quadPass->GetProgram();
    m_requirements.semanticTextures["s_diffuseColor"] = m_copyTexture;
    m_requirements.semanticTextures["s_normalDepth"]  = normalDepth;
    m_requirements.customUbos[7]                    = &m_passDataBuffer.GetBuffer();

    m_passDataBuffer.Invalidate();
    m_passDataBuffer.Map();

    ApplyRequirements(renderer);
    RenderSubPass(m_quadPass);
  }

  void SsrPass::GatherRequirements(PassRequirements& reqs)
  {
    // Full screen composite onto an already rendered color buffer: no depth interaction at all.
    reqs.passState.depthTestEnabled  = false;
    reqs.passState.depthWriteEnabled = false;
    reqs.passState.depthFunction     = CompareFunctions::FuncAlways;
  }

  void SsrPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PostRender();
  }

} // namespace ToolKit
