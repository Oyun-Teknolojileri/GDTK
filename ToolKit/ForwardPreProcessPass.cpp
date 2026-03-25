/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

// Purpose of this pass is exporting forward depths and normals before SSAO pass

#include "ForwardPreProcessPass.h"

#include "Shader.h"
#include "Stats.h"

#include "DebugNew.h"

namespace ToolKit
{

  ForwardPreProcessPass::ForwardPreProcessPass() : Pass("ForwardPreProcessPass")
  {
    m_framebuffer          = MakeNewPtr<Framebuffer>("ForwardPreProcessFB");
    m_resolveFramebuffer   = MakeNewPtr<Framebuffer>("ForwardPreProcessResolveFB");

    m_linearMaterial       = MakeNewPtr<Material>();
    ShaderPtr vertexShader = GetShaderManager()->Create<Shader>(ShaderPath("forwardPreProcessVert.shader", true));
    m_linearMaterial->SetVertexShaderVal(vertexShader);

    ShaderPtr fragmentShader = GetShaderManager()->Create<Shader>(ShaderPath("forwardPreProcess.shader", true));
    m_linearMaterial->SetFragmentShaderVal(fragmentShader);
    m_linearMaterial->Init();

    TextureSettings set = {};
    set.WarpS           = GraphicTypes::UVClampToEdge;
    set.WarpT           = GraphicTypes::UVClampToEdge;
    set.InternalFormat  = GraphicTypes::FormatRGBA16F;
    set.Format          = GraphicTypes::FormatRGBA;
    set.Type            = GraphicTypes::TypeFloat;
    set.GenerateMipMap  = false;
    m_normalDepthRt     = MakeNewPtr<RenderTarget>(128, 128, set, "NormalDepthRT");
  }

  void ForwardPreProcessPass::InitBuffers(int width, int height, MsaaSampleCount sampleCount)
  {
    const FramebufferSettings& fbs = m_framebuffer->GetSettings();
    bool requiresReconstruct       = fbs.width != width || fbs.height != height || fbs.msaaCount != sampleCount;

    if (requiresReconstruct)
    {
      m_framebuffer->DetachDepthTexture();
      m_framebuffer->ReconstructIfNeeded({width, height, false, false, sampleCount});

      if (m_framebuffer->IsMultiSampled())
      {
        m_resolveFramebuffer->ReconstructIfNeeded({width, height, false, false, MsaaSampleCount::x0});
      }

      TextureSettings rtSettings = m_normalDepthRt->Settings();
      rtSettings.msaaCount       = sampleCount;
      m_normalDepthRt->ReconstructIfNeeded(width, height, &rtSettings);

      m_framebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_normalDepthRt);
    }

    // Pass incoming depth buffer to create z buffer for early z test.
    if (DepthTexturePtr depth = m_params.FrameBuffer->GetDepthTexture())
    {
      if (depth != m_framebuffer->GetDepthTexture())
      {
        m_framebuffer->AttachDepthTexture(depth);
      }
    }
  }

  void ForwardPreProcessPass::Render()
  {
    TK_PROFILE_FUNCTION();

    // Currently transparent objects are not rendered to export screen space normals or linear depth
    // we want SSAO and DOF to effect on opaque objects only renderLinearDepthAndNormalFn(m_params.TranslucentJobs);
    RenderJobItr begin = m_params.renderData->GetForwardOpaqueBegin();
    RenderJobItr end   = m_params.renderData->GetForwardAlphaMaskedBegin();

    ShaderPtr frag     = m_linearMaterial->GetFragmentShaderVal();
    frag->SetDefine("DrawAlphaMasked", "0");

    ShaderPtr vert                       = m_linearMaterial->GetVertexShaderVal();
    GpuProgramManager* gpuProgramManager = GetGpuProgramManager();
    m_program                            = gpuProgramManager->CreateProgram(vert, frag);

    Renderer* renderer                   = GetRenderer();
    renderer->BindProgram(m_program);

    for (RenderJobItr job = begin; job != end; job++)
    {
      renderer->Render(*job);
    }

    begin = m_params.renderData->GetForwardAlphaMaskedBegin();
    end   = m_params.renderData->GetForwardTranslucentBegin();
    frag->SetDefine("DrawAlphaMasked", "1");

    m_program = gpuProgramManager->CreateProgram(vert, frag);
    renderer->BindProgram(m_program);

    for (RenderJobItr job = begin; job != end; job++)
    {
      renderer->Render(*job);
    }
  }

  void ForwardPreProcessPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    Renderer* renderer = GetRenderer();
    renderer->SetFramebuffer(m_framebuffer, GraphicBitFields::AllBits);
    renderer->SetCamera(m_params.Cam, true);
  }

  void ForwardPreProcessPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PostRender();

    if (m_framebuffer->IsMultiSampled())
    {
      Renderer* renderer = GetRenderer();
      renderer->ResolveFramebuffer(m_framebuffer,
                                   m_resolveFramebuffer,
                                   {(int) Framebuffer::Attachment::ColorAttachment0},
                                   true);

      // We don't need msaa color buffer after resolve, but we want to keep depth for upcoming passes.
      renderer->InvalidateFramebuffer(GraphicBitFields::ColorBits, m_framebuffer);
    }
  }

} // namespace ToolKit