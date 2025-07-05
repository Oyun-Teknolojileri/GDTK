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
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  ForwardRenderPass::ForwardRenderPass() : Pass("ForwardRenderPass")
  {
    ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    m_EVSM4                   = shadows->GetUseEVSM4Val();
    m_SMFormat16Bit           = !shadows->GetUse32BitShadowMapVal();

    m_programConfigMat        = GetMaterialManager()->GetCopyOfDefaultMaterial();

    ShaderPtr fragmentShader  = m_programConfigMat->GetFragmentShaderVal();
    fragmentShader->SetDefine("EVSM4", std::to_string(m_EVSM4));
    fragmentShader->SetDefine("SMFormat16Bit", std::to_string(m_SMFormat16Bit));
  }

  void ForwardRenderPass::Render()
  {
    RenderOpaque(m_params.renderData);
    RenderTranslucent(m_params.renderData);
  }

  void ForwardRenderPass::PreRender()
  {
    Pass::PreRender();

    // Set self data.
    Renderer* renderer = GetRenderer();

    renderer->SetFramebuffer(m_params.FrameBuffer, m_params.clearBuffer);
    renderer->SetCamera(m_params.Cam, true);

    // Adjust the depth test considering z-pre pass.
    if (m_params.hasForwardPrePass)
    {
      // This is the optimal flag if the depth buffer is filled.
      // Only the visible fragments will pass the test.
      renderer->SetDepthTestFunc(CompareFunctions::FuncLequal);
    }
  }

  void ForwardRenderPass::PostRender()
  {
    Pass::PostRender();

    // Set the default depth test.
    Renderer* renderer = GetRenderer();
    renderer->SetDepthTestFunc(CompareFunctions::FuncLess);
  }

  void ForwardRenderPass::RenderOpaque(RenderData* renderData)
  {
    // Adjust program configuration.
    ConfigureProgram();

    // Render opaque.
    ShaderPtr frag = m_programConfigMat->GetFragmentShaderVal();
    frag->SetDefine("DrawAlphaMasked", "0");

    ShaderPtr vert           = m_programConfigMat->GetVertexShaderVal();
    GpuProgramPtr gpuProgram = GetGpuProgramManager()->CreateProgram(vert, frag);

    RenderJobItr begin       = renderData->GetForwardOpaqueBegin();
    RenderJobItr end         = renderData->GetForwardAlphaMaskedBegin();
    RenderOpaqueHelper(renderData, begin, end, gpuProgram);

    // Render alpha masked.
    frag->SetDefine("DrawAlphaMasked", "1");
    gpuProgram = GetGpuProgramManager()->CreateProgram(vert, frag);

    begin      = renderData->GetForwardAlphaMaskedBegin();
    end        = renderData->GetForwardTranslucentBegin();
    RenderOpaqueHelper(renderData, begin, end, gpuProgram);
  }

  void ForwardRenderPass::RenderTranslucent(RenderData* renderData)
  {
    ConfigureProgram();

    ShaderPtr frag = m_programConfigMat->GetFragmentShaderVal();
    frag->SetDefine("DrawAlphaMasked", "0");

    ShaderPtr vert        = m_programConfigMat->GetVertexShaderVal();

    GpuProgramPtr program = GetGpuProgramManager()->CreateProgram(vert, frag);

    Renderer* renderer    = GetRenderer();
    renderer->BindProgram(program);

    RenderJobItr begin = renderData->GetForwardTranslucentBegin();
    RenderJobItr end   = renderData->jobs.end();
    RenderJobProcessor::SortByDistanceToCamera(begin, end, m_params.Cam);

    if (begin != end)
    {
      renderer->SetDepthTestFunc(CompareFunctions::FuncLess);
      renderer->EnableDepthWrite(false);
      for (RenderJobArray::iterator job = begin; job != end; job++)
      {
        if (job->Material->IsShaderMaterial())
        {
          renderer->RenderWithProgramFromMaterial(*job);
        }
        else
        {
          renderer->BindProgram(program);

          Material* mat = job->Material;
          if (mat->GetRenderState()->cullMode == CullingType::TwoSided)
          {
            mat->GetRenderState()->cullMode = CullingType::Front;
            renderer->Render(*job);

            mat->GetRenderState()->cullMode = CullingType::Back;
            renderer->Render(*job);

            mat->GetRenderState()->cullMode = CullingType::TwoSided;
          }
          else
          {
            renderer->Render(*job);
          }
        }
      }
      renderer->EnableDepthWrite(true);
    }
  }

  void ForwardRenderPass::RenderOpaqueHelper(RenderData* renderData,
                                             RenderJobItr begin,
                                             RenderJobItr end,
                                             GpuProgramPtr defaultGpuProgram)
  {
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
    if (shadows->GetUseEVSM4Val() != m_EVSM4)
    {
      m_EVSM4 = shadows->GetUseEVSM4Val();
      frag->SetDefine("EVSM4", std::to_string(m_EVSM4));
    }

    bool is16Bit = !shadows->GetUse32BitShadowMapVal();
    if (is16Bit != m_SMFormat16Bit)
    {
      m_SMFormat16Bit = is16Bit;
      frag->SetDefine("SMFormat16Bit", std::to_string(is16Bit));
    }

    uint adc = glm::min(RHIConstants::MaxDirectionalLightPerObject, m_params.activeDirectionalLightCount);
    frag->SetDefine("ActiveDirectionalLightCount", std::to_string(adc));

    int shadowSample = shadows->GetShadowSamples();
    frag->SetDefine("ShadowSampleCount", std::to_string(shadowSample));
  }

} // namespace ToolKit