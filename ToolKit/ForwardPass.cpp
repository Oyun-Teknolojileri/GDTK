/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "ForwardPass.h"

#include "EngineSettings.h"
#include "Material.h"
#include "Mesh.h"
#include "Pass.h"
#include "Shader.h"
#include "Stats.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  ForwardRenderPass::ForwardRenderPass() : Pass("ForwardRenderPass")
  {
    ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    m_shadowPCF               = shadows->GetShadowPCFVal().GetValue<int>();

    m_programConfigMat        = GetMaterialManager()->GetCopyOfDefaultMaterial();

    ShaderPtr fragmentShader  = m_programConfigMat->GetFragmentShaderVal();
    fragmentShader->SetDefine("ShadowPCF", std::to_string(m_shadowPCF));
  }

  void ForwardRenderPass::Render()
  {
    TK_PROFILE_FUNCTION();

    RenderOpaque(m_params.renderData);
    RenderTranslucent(m_params.renderData);
  }

  void ForwardRenderPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    // Set self data.
    Renderer* renderer           = GetRenderer();

    GraphicBitFields discardBits = GraphicBitFields::None;
    if (m_params.invalidateDepthBuffer)
    {
      if (m_params.FrameBuffer->IsMultiSampled() && m_params.resolveFrameBuffer != nullptr)
      {
        discardBits = GraphicBitFields::AllBits;
      }
      else
      {
        discardBits = GraphicBitFields::DepthBits;
      }
    }

    renderer->SetFramebuffer(m_params.FrameBuffer, m_params.clearBuffer, Vec4(0.0f), discardBits);
    renderer->SetCamera(m_params.Cam, true);

    // Adjust the depth test considering z-pre pass. With a filled depth buffer Lequal lets
    // exactly the visible fragments survive; without it we keep the default Less.
    if (m_params.hasForwardPrePass && m_params.renderData != nullptr)
    {
      RenderJobItr begin = m_params.renderData->GetForwardOpaqueBegin();
      RenderJobItr end   = m_params.renderData->jobs.end();
      for (RenderJobItr job = begin; job != end; ++job)
      {
        job->State.depthFunction = CompareFunctions::FuncLequal;
      }
    }

    if (m_params.onPreRender)
    {
      m_params.onPreRender();
    }
  }

  void ForwardRenderPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PostRender();

    Renderer* renderer = GetRenderer();

    // Resolve render target if necessary.
    if (m_params.FrameBuffer->IsMultiSampled() && m_params.resolveFrameBuffer != nullptr)
    {
      renderer->ResolveFramebuffer(m_params.FrameBuffer,
                                   m_params.resolveFrameBuffer,
                                   {(int) Framebuffer::Attachment::ColorAttachment0});
    }

    renderer->EndPass();
  }

  void ForwardRenderPass::RenderOpaque(RenderData* renderData)
  {
    TK_PROFILE_FUNCTION();

    Renderer* renderer = GetRenderer();

    // Adjust program configuration.
    ConfigureProgram();

    // Render opaque.
    ShaderPtr frag = m_programConfigMat->GetFragmentShaderVal();
    frag->SetDefine("DrawAlphaMasked", "0");

    ShaderPtr vert           = m_programConfigMat->GetVertexShaderVal();
    GpuProgramPtr gpuProgram = renderer->GetGpuProgramManager()->CreateProgram(vert, frag);

    RenderJobItr begin       = renderData->GetForwardOpaqueBegin();
    RenderJobItr end         = renderData->GetForwardAlphaMaskedBegin();
    RenderOpaqueHelper(renderData, begin, end, gpuProgram);

    // Render alpha masked.
    frag->SetDefine("DrawAlphaMasked", "1");
    gpuProgram = renderer->GetGpuProgramManager()->CreateProgram(vert, frag);

    begin      = renderData->GetForwardAlphaMaskedBegin();
    end        = renderData->GetForwardTranslucentBegin();
    RenderOpaqueHelper(renderData, begin, end, gpuProgram);
  }

  void ForwardRenderPass::RenderTranslucent(RenderData* renderData)
  {
    TK_PROFILE_FUNCTION();

    ConfigureProgram();

    // Disable SSAO for translucent objects. The SSAO texture contains occlusion
    // from opaque geometry only, applying it to translucent surfaces causes
    // background AO to bleed onto transparent objects (e.g. glass).
    Renderer* renderer = GetRenderer();
    renderer->SetAmbientOcclusionTexture(nullptr);

    ShaderPtr frag = m_programConfigMat->GetFragmentShaderVal();
    frag->SetDefine("DrawAlphaMasked", "0");

    ShaderPtr vert        = m_programConfigMat->GetVertexShaderVal();

    GpuProgramPtr program = renderer->GetGpuProgramManager()->CreateProgram(vert, frag);

    renderer->BindProgram(program);

    RenderJobItr begin = renderData->GetForwardTranslucentBegin();
    RenderJobItr end   = renderData->jobs.end();
    RenderJobProcessor::SortByDistanceToCamera(begin, end, m_params.Cam);

    if (begin != end)
    {
      // Translucent draws: keep depth test (default Less, even when PreRender bumped opaque
      // jobs to Lequal for z-prepass), no depth write so back-to-front draws don't self-occlude.
      for (RenderJobArray::iterator job = begin; job != end; job++)
      {
        job->State.depthFunction     = CompareFunctions::FuncLess;
        job->State.depthWriteEnabled = false;

        if (job->Material->IsShaderMaterial())
        {
          renderer->RenderWithProgramFromMaterial(*job);
        }
        else
        {
          renderer->BindProgram(program);

          if (job->State.cullMode == CullingType::TwoSided)
          {
            // Two-sided translucent: draw back faces first then front, both via the per-job
            // state so the shared material asset stays untouched.
            job->State.cullMode = CullingType::Front;
            renderer->Render(*job);

            job->State.cullMode = CullingType::Back;
            renderer->Render(*job);
          }
          else
          {
            renderer->Render(*job);
          }
        }
      }
    }
  }

  void ForwardRenderPass::RenderOpaqueHelper(RenderData* renderData,
                                             RenderJobItr begin,
                                             RenderJobItr end,
                                             GpuProgramPtr defaultGpuProgram)
  {
    TK_PROFILE_FUNCTION();

    Renderer* renderer = GetRenderer();
    renderer->SetAmbientOcclusionTexture(m_params.SsaoTexture);

    for (RenderJobItr job = begin; job != end; job++)
    {
      if (job->Material->IsShaderMaterial())
      {
        renderer->RenderWithProgramFromMaterial(*job);
      }
      else
      {
        renderer->BindProgram(defaultGpuProgram);
        renderer->Render(*job);
      }
    }
  }

  void ForwardRenderPass::ConfigureProgram()
  {
    const ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    ShaderPtr frag                  = m_programConfigMat->GetFragmentShaderVal();

    int shadowPCF                   = shadows->GetShadowPCFVal().GetValue<int>();
    if (shadowPCF != m_shadowPCF)
    {
      m_shadowPCF = shadowPCF;
      frag->SetDefine("ShadowPCF", std::to_string(m_shadowPCF));
    }

    Renderer* renderer = GetRenderer();
    switch (renderer->m_shadingMode)
    {
      case ShadingMode::Lighting:
        frag->SetDefine("ShadingMode", "1");
        break;
      case ShadingMode::Albedo:
        frag->SetDefine("ShadingMode", "2");
        break;
      case ShadingMode::Normal:
        frag->SetDefine("ShadingMode", "3");
        break;
      case ShadingMode::Metallic:
        frag->SetDefine("ShadingMode", "4");
        break;
      case ShadingMode::Roughness:
        frag->SetDefine("ShadingMode", "5");
        break;
      default:
      case ShadingMode::None:
        frag->SetDefine("ShadingMode", "0");
        break;
    }
  }

} // namespace ToolKit