/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
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

    // Opaque + alpha-masked passive defaults. Lequal so a z-prepassed depth buffer keeps
    // exactly the visible fragments; depth write on so non-prepassed scenes still build
    // a correct z buffer.
    m_opaquePassState.depthFunction          = CompareFunctions::FuncLequal;
    m_opaquePassState.depthWriteEnabled      = true;
    m_opaquePassState.depthTestEnabled       = true;

    // Translucent passive defaults. Lequal matches the opaque path; depth write off so
    // back-to-front draws don't self-occlude.
    m_translucentPassState.depthFunction     = CompareFunctions::FuncLequal;
    m_translucentPassState.depthWriteEnabled = false;
    m_translucentPassState.depthTestEnabled  = true;
  }

  void ForwardRenderPass::Render()
  {
    TK_PROFILE_FUNCTION();

    // Pre-bind the slot-7 custom UBO (if any — set via m_requirements.customUbos by the
    // caller's onPreRender callback, typically the Grid's UBO). Doing this once at the
    // top of Render() means every subsequent renderer->Render(job) sees a descriptor set
    // already pointed at the right VkBuffer, so the grid material's draw picks up its
    // GridPassData without needing per-job rebinds.
    //
    // Pass-level passive state is also stamped in once; per-job bits (cull, blend,
    // alphaMask) are merged from the job's material inside Renderer::Render.
    m_requirements.frameBuffer    = m_params.FrameBuffer;
    m_requirements.clearBits      = m_params.clearBuffer;
    m_requirements.passState      = m_opaquePassState;
    m_requirements.program        = nullptr; // let Apply build from vert/frag below

    // We don't have a single vert/frag here (ForwardRenderPass builds programs
    // dynamically per-config-material). Pull vert/frag from m_programConfigMat so
    // ApplyRequirements has a valid program. For jobs that override the material
    // (shader materials, two-sided translucent), the per-job renderer->Render path
    // rebinds with the right program internally — the customUbos binding we set
    // here survives across those rebinds.
    m_requirements.fragmentShader = m_programConfigMat->GetFragmentShaderVal();
    m_requirements.vertexShader   = m_programConfigMat->GetVertexShaderVal();

    ApplyRequirements(GetRenderer());

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

    renderer->FinishPass();
  }

  void ForwardRenderPass::BuildPrograms(GpuProgramPtr& perObjectProgram)
  {
    Renderer* renderer = GetRenderer();
    ShaderPtr frag     = m_programConfigMat->GetFragmentShaderVal();
    ShaderPtr vert     = m_programConfigMat->GetVertexShaderVal();

    perObjectProgram = renderer->GetGpuProgramManager()->CreateProgram(vert, frag);

    // The instanced variant differs only in the vertex shader define: the define is flipped on the shared
    // shader, the program is created (and cached under that state), and the define is put back so every
    // other user still builds the per object program. It is built here, next to the fragment program and
    // therefore with the same defines, because the two are used interchangeably for the SAME range: the
    // opaque range and the alpha masked range each draw through both. Building it once in ConfigureProgram
    // left the masked range batched with DrawAlphaMasked = 0, so an instanced masked mesh filled the holes
    // its mask cuts and wrote depth where it is transparent.
    vert->SetDefine("INSTANCE_TABLE", "1");
    m_instancedProgram = renderer->GetGpuProgramManager()->CreateProgram(vert, frag);
    vert->SetDefine("INSTANCE_TABLE", "0");
  }

  void ForwardRenderPass::RenderOpaque(RenderData* renderData)
  {
    TK_PROFILE_FUNCTION();

    // Adjust program configuration.
    ConfigureProgram();

    // Render opaque.
    ShaderPtr frag           = m_programConfigMat->GetFragmentShaderVal();
    GpuProgramPtr gpuProgram = nullptr;

    frag->SetDefine("DrawAlphaMasked", "0");
    BuildPrograms(gpuProgram);

    RenderJobItr begin = renderData->GetForwardOpaqueBegin();
    RenderJobItr end   = renderData->GetForwardAlphaMaskedBegin();
    RenderOpaqueHelper(renderData, begin, end, gpuProgram);

    // Render alpha masked.
    frag->SetDefine("DrawAlphaMasked", "1");
    BuildPrograms(gpuProgram);

    begin = renderData->GetForwardAlphaMaskedBegin();
    end   = renderData->GetForwardTranslucentBegin();
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
      renderer->SetPassState(m_translucentPassState);

      RenderState twoSidedState  = m_translucentPassState;
      twoSidedState.cullOverride = true;

      for (RenderJobArray::iterator job = begin; job != end; job++)
      {
        if (job->Material->IsShaderMaterial())
        {
          renderer->RenderWithProgramFromMaterial(*job);
        }
        else
        {
          renderer->BindProgram(program);

          if (job->Material->cullMode == CullingType::TwoSided)
          {
            // Two-sided translucent: draw back faces first then front. cullOverride forces the
            // cull mode without touching the shared material — the pass state restores itself at
            // the end of the iteration so the next job sees the standard translucent state.
            twoSidedState.cullOverrideMode = CullingType::Front;
            renderer->SetPassState(twoSidedState);
            renderer->Render(*job);

            twoSidedState.cullOverrideMode = CullingType::Back;
            renderer->SetPassState(twoSidedState);
            renderer->Render(*job);

            renderer->SetPassState(m_translucentPassState);
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

    renderer->SetPassState(m_opaquePassState);

    // Batching needs the instanced program; without it the pass falls back to the per object path.
    if (m_instancedProgram != nullptr)
    {
      RenderOpaqueBatches(renderData, begin, end, defaultGpuProgram);
      return;
    }

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

  namespace
  {
    /** Two jobs can share a draw when nothing but their transform differs. */
    bool CanShareDraw(const RenderJob& a, const RenderJob& b)
    {
      if (a.Mesh != b.Mesh || a.Material != b.Material)
      {
        return false;
      }
      if (a.requireCullFlip != b.requireCullFlip)
      {
        return false;
      }
      if (a.EnvironmentVolume != b.EnvironmentVolume ||
          a.SecondaryEnvironmentVolume != b.SecondaryEnvironmentVolume)
      {
        return false;
      }
      if (a.Material->IsShaderMaterial() || a.Mesh->IsSkinned())
      {
        return false;
      }
      if (a.lights.size() != b.lights.size())
      {
        return false;
      }
      for (size_t i = 0; i < a.lights.size(); i++)
      {
        if (a.lights[i] != b.lights[i])
        {
          return false;
        }
      }
      return true;
    }

    struct JobRun
    {
      int begin = 0;
      int count = 0;
    };
  } // namespace

  void ForwardRenderPass::RenderOpaqueBatches(RenderData* renderData,
                                               RenderJobItr begin,
                                               RenderJobItr end,
                                               GpuProgramPtr defaultGpuProgram)
  {
    TK_PROFILE_FUNCTION();

    Renderer* renderer    = GetRenderer();
    RenderJobArray& jobs = renderData->jobs;
    const int first      = int(begin - jobs.begin());
    const int last       = int(end - jobs.begin());

    // Runs of consecutive jobs that can share a draw. The jobs are sorted by material, so these
    // runs are long for the usual case of many objects of one type.
    const bool mayBatch = GetEngineSettings().m_graphics->GetInstancedBatchingVal();

    std::vector<JobRun> runs;
    std::vector<JobRun> singles;
    int instanceCount = 0;
    for (int i = first; i < last;)
    {
      int j = i + 1;
      while (mayBatch && j < last && CanShareDraw(jobs[i], jobs[j]))
      {
        j++;
      }

      if (j - i > 1)
      {
        runs.push_back({i, j - i});
        instanceCount += j - i;
      }
      else
      {
        singles.push_back({i, 1});
      }
      i = j;
    }

    // Pack every batched transform first, then upload once: the table is one texture for the
    // whole pass, not one per run.

    if (instanceCount > 0)
    {
      const int texelCount = instanceCount * InstanceTexelsPerItem;
      const int rows       = glm::max(1, (texelCount + InstanceTexWidth - 1) / InstanceTexWidth);
      m_instanceData.assign(size_t(rows) * size_t(InstanceTexWidth) * 4, 0.0f);

      int instance = 0;
      for (const JobRun& run : runs)
      {
        for (int k = 0; k < run.count; k++)
        {
          const Mat4& m     = jobs[run.begin + k].WorldTransform;
          const Mat4 normal = glm::transpose(glm::inverse(m));
          float* out        = m_instanceData.data() + size_t(instance) * InstanceTexelsPerItem * 4;

          for (int c = 0; c < 4; c++)
          {
            out[c * 4 + 0]      = m[c].x;
            out[c * 4 + 1]      = m[c].y;
            out[c * 4 + 2]      = m[c].z;
            out[c * 4 + 3]      = m[c].w;
            out[16 + c * 4 + 0] = normal[c].x;
            out[16 + c * 4 + 1] = normal[c].y;
            out[16 + c * 4 + 2] = normal[c].z;
            out[16 + c * 4 + 3] = normal[c].w;
          }

          instance++;
        }
      }

      if (m_instanceTable == nullptr || m_instanceTable->m_height != rows)
      {
        TextureSettings settings;
        settings.Target         = GraphicTypes::Target2D;
        settings.WarpS          = GraphicTypes::UVClampToEdge;
        settings.WarpT          = GraphicTypes::UVClampToEdge;
        settings.InternalFormat = GraphicTypes::FormatRGBA32F;
        settings.Format         = GraphicTypes::FormatRGBA;
        settings.Type           = GraphicTypes::TypeFloat;

        m_instanceTable = MakeNewPtr<DataTexture>(InstanceTexWidth, rows, settings, "ForwardInstanceTable");
        m_instanceTable->Init(m_instanceData.data());
      }
      else
      {
        m_instanceTable->Map(m_instanceData.data());
      }

      renderer->SetInstanceData(m_instanceTable);
      renderer->BindProgram(m_instancedProgram);

      int firstInstance = 0;
      for (const JobRun& run : runs)
      {
        renderer->RenderBatch(jobs[run.begin], (uint) run.count, (uint) firstInstance);
        firstInstance += run.count;
      }

      renderer->SetInstanceData(nullptr);
    }

    for (const JobRun& single : singles)
    {
      RenderJob& job = jobs[single.begin];
      if (job.Material->IsShaderMaterial())
      {
        renderer->RenderWithProgramFromMaterial(job);
      }
      else
      {
        renderer->BindProgram(defaultGpuProgram);
        renderer->Render(job);
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
