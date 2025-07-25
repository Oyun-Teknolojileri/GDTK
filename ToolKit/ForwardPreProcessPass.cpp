﻿/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

// Purpose of this pass is exporting forward depths and normals before SSAO pass

#include "ForwardPreProcessPass.h"

#include "Shader.h"

#include "DebugNew.h"

namespace ToolKit
{

  ForwardPreProcessPass::ForwardPreProcessPass() : Pass("ForwardPreProcessPass")
  {
    m_framebuffer          = MakeNewPtr<Framebuffer>("ForwardPreProcessFB");

    m_linearMaterial       = MakeNewPtr<Material>();
    ShaderPtr vertexShader = GetShaderManager()->Create<Shader>(ShaderPath("forwardPreProcessVert.shader", true));
    m_linearMaterial->SetVertexShaderVal(vertexShader);

    ShaderPtr fragmentShader = GetShaderManager()->Create<Shader>(ShaderPath("forwardPreProcess.shader", true));
    m_linearMaterial->SetFragmentShaderVal(fragmentShader);
    m_linearMaterial->Init();

    TextureSettings oneChannelSet = {};
    oneChannelSet.WarpS           = GraphicTypes::UVClampToEdge;
    oneChannelSet.WarpT           = GraphicTypes::UVClampToEdge;
    oneChannelSet.InternalFormat  = GraphicTypes::FormatRGBA16F;
    oneChannelSet.Format          = GraphicTypes::FormatRGBA;
    oneChannelSet.Type            = GraphicTypes::TypeFloat;
    oneChannelSet.GenerateMipMap  = false;
    m_normalRt                    = MakeNewPtr<RenderTarget>(128, 128, oneChannelSet, "NormalRT");

    oneChannelSet.InternalFormat  = GraphicTypes::FormatRGBA32F;
    m_linearDepthRt               = MakeNewPtr<RenderTarget>(128, 128, oneChannelSet, "LinearDepthRT");
  }

  void ForwardPreProcessPass::InitBuffers(int width, int height, int sampleCount)
  {
    const FramebufferSettings& fbs = m_framebuffer->GetSettings();
    bool requiresReconstruct = fbs.width != width || fbs.height != height || fbs.multiSampleFrameBuffer != sampleCount;

    if (requiresReconstruct)
    {
      m_framebuffer->ReconstructIfNeeded({width, height, false, false, sampleCount});
      m_normalRt->ReconstructIfNeeded(width, height);
      m_linearDepthRt->ReconstructIfNeeded(width, height);

      m_framebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_linearDepthRt);
      m_framebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment1, m_normalRt);

      // Pass incoming depth buffer to create z buffer for early z test.
      if (DepthTexturePtr depth = m_params.FrameBuffer->GetDepthTexture())
      {
        m_framebuffer->AttachDepthTexture(depth);
      }
    }
  }

  void ForwardPreProcessPass::Render()
  {
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
    Pass::PreRender();

    Renderer* renderer = GetRenderer();
    renderer->SetFramebuffer(m_framebuffer, GraphicBitFields::AllBits);
    renderer->SetCamera(m_params.Cam, true);
  }

} // namespace ToolKit