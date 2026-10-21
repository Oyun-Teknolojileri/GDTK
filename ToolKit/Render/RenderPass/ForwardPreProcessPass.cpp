/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

// Purpose of this pass is exporting forward depths and normals before SSAO pass

#include "ForwardPreProcessPass.h"
#include "EngineSettings.h"

#include "Mesh.h"
#include "Shader.h"
#include "Stats.h"

#include <algorithm>

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

    TextureSettings set           = {};
    set.WarpS                     = GraphicTypes::UVClampToEdge;
    set.WarpT                     = GraphicTypes::UVClampToEdge;
    set.InternalFormat            = GraphicTypes::FormatRGBA16F;
    set.Format                    = GraphicTypes::FormatRGBA;
    set.Type                      = GraphicTypes::TypeFloat;
    set.GenerateMipMap            = false;
    m_normalDepthRt               = MakeNewPtr<RenderTarget>(128, 128, set, "NormalDepthRT");

    // FuncLess is correct here: this pass renders into a freshly cleared depth buffer with
    // no prior writes from any other pass, and is itself the writer of that buffer.
    m_passState.depthTestEnabled  = true;
    m_passState.depthWriteEnabled = true;
    m_passState.depthFunction     = CompareFunctions::FuncLess;
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
    Renderer* renderer                   = GetRenderer();
    GpuProgramManager* gpuProgramManager = renderer->GetGpuProgramManager();
    m_program                            = gpuProgramManager->CreateProgram(vert, frag);

    // Instanced variant of the same program. The define is flipped on the shared shader only for
    // the duration of the program build, then put back for everyone else, which is the same
    // pattern the forward pass uses.
    vert->SetDefine("INSTANCE_TABLE", "1");
    m_instancedProgram = gpuProgramManager->CreateProgram(vert, frag);
    vert->SetDefine("INSTANCE_TABLE", "0");

    renderer->SetPassState(m_passState);
    renderer->BindProgram(m_program);

    if (m_instancedProgram != nullptr)
    {
      RenderBatches(begin, end, false);
    }
    else
    {
      for (RenderJobItr job = begin; job != end; job++)
      {
        renderer->Render(*job);
      }
    }

    begin = m_params.renderData->GetForwardAlphaMaskedBegin();
    end   = m_params.renderData->GetForwardTranslucentBegin();
    frag->SetDefine("DrawAlphaMasked", "1");

    m_program = gpuProgramManager->CreateProgram(vert, frag);
    renderer->BindProgram(m_program);

    if (m_instancedProgram != nullptr)
    {
      // Alpha masked jobs sample their own material, so they can only share a draw when the
      // material matches too.
      RenderBatches(begin, end, true);
    }
    else
    {
      for (RenderJobItr job = begin; job != end; job++)
      {
        renderer->Render(*job);
      }
    }
  }

  namespace
  {
    /** A prepass draw only needs the geometry, the transform and the cull side. */
    bool PreProcessCanShare(const RenderJob& a, const RenderJob& b, bool masked)
    {
      if (a.Mesh != b.Mesh || a.requireCullFlip != b.requireCullFlip || a.Mesh->IsSkinned())
      {
        return false;
      }

      // An alpha masked job reads its alpha mask from the material, so the material has to match.
      return !masked || a.Material == b.Material;
    }

    struct PreProcessRun
    {
      int begin = 0;
      int count = 0;
    };
  } // namespace

  void ForwardPreProcessPass::RenderBatches(RenderJobItr begin, RenderJobItr end, bool masked)
  {
    TK_PROFILE_FUNCTION();

    Renderer* renderer   = GetRenderer();
    RenderJobArray& jobs = m_params.renderData->jobs;
    const int first      = int(begin - jobs.begin());
    const int last       = int(end - jobs.begin());

    const bool mayBatch = GetEngineSettings().m_graphics->GetInstancedBatchingVal();

    std::vector<PreProcessRun> runs;
    for (int i = first; i < last;)
    {
      int j = i + 1;
      while (mayBatch && j < last && PreProcessCanShare(jobs[i], jobs[j], masked))
      {
        j++;
      }

      if (j - i > 1)
      {
        runs.push_back({i, j - i});
      }
      i = j;
    }

    if (runs.empty())
    {
      for (int i = first; i < last; i++)
      {
        renderer->Render(jobs[i]);
      }
      return;
    }

    int instanceCount = 0;
    for (const PreProcessRun& run : runs)
    {
      instanceCount += run.count;
    }

    const int texelCount = instanceCount * InstanceTexelsPerItem;
    const int rows       = glm::max(1, (texelCount + InstanceTexWidth - 1) / InstanceTexWidth);
    m_instanceData.assign(size_t(rows) * size_t(InstanceTexWidth) * 4, 0.0f);

    int instance = 0;
    for (const PreProcessRun& run : runs)
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

      m_instanceTable = MakeNewPtr<DataTexture>(InstanceTexWidth, rows, settings, "PreProcessInstanceTable");
      m_instanceTable->Init(m_instanceData.data());
    }
    else
    {
      m_instanceTable->Map(m_instanceData.data());
    }

    renderer->SetInstanceData(m_instanceTable);
    renderer->BindProgram(m_instancedProgram);

    int firstInstance = 0;
    for (const PreProcessRun& run : runs)
    {
      renderer->RenderBatch(jobs[run.begin], (uint) run.count, (uint) firstInstance);
      firstInstance += run.count;
    }

    renderer->SetInstanceData(nullptr);

    // Jobs that could not join a run are drawn per object, so the per object program has to be
    // bound again: the instanced one would read a table that is no longer there.
    renderer->BindProgram(m_program);

    // Jobs that could not join a run still have to be drawn.
    for (int i = first; i < last;)
    {
      int j = i + 1;
      while (j < last && PreProcessCanShare(jobs[i], jobs[j], masked))
      {
        j++;
      }

      if (j - i == 1)
      {
        renderer->Render(jobs[i]);
      }
      i = j;
    }
  }

  void ForwardPreProcessPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    Renderer* renderer           = GetRenderer();

    GraphicBitFields discardBits = GraphicBitFields::None;
    if (m_framebuffer->IsMultiSampled())
    {
      discardBits = GraphicBitFields::ColorBits;
    }

    renderer->SetFramebuffer(m_framebuffer, GraphicBitFields::AllBits, Vec4(0.0f), discardBits);
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
                                   {(int) Framebuffer::Attachment::ColorAttachment0});
    }
    GetRenderer()->FinishPass();
  }

} // namespace ToolKit
