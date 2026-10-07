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
    /**
     * SSR_STEP_COUNT variants the fragment shader is compiled with. Only the loop bound is picked
     * from here; the requested step count itself travels as a uniform, so the setting stays
     * continuous and a value between two bounds is not rounded away.
     */
    int ClampMaxSteps(int steps)
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

    m_filterPass                       = MakeNewPtr<FullQuadPass>();
    m_filterPass->m_params.frameBuffer = MakeNewPtr<Framebuffer>("SsrFilterFB");
    m_filterShader                     = GetShaderManager()->Create<Shader>(ShaderPath("ssrFilterFrag.shader", true));
    m_traceTexture                     = MakeNewPtr<RenderTarget>("SsrTraceRT");
  }

  SsrPass::~SsrPass()
  {
    m_quadPass     = nullptr;
    m_ssrShader    = nullptr;
    m_copyTexture  = nullptr;

    m_filterPass   = nullptr;
    m_filterShader = nullptr;
    m_traceTexture = nullptr;
  }

  void SsrPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    if (m_params.ColorRt == nullptr || m_params.GNormalDepthBuffer == nullptr || m_params.Cam == nullptr)
    {
      return;
    }

    // A fragment shader that failed to compile keeps a null GpuResourceData, and handing that to the
    // program creation makes the backend dereference it. The backend has already logged the compile
    // error, so disable the pass instead of taking the renderer down with it.
    if (m_shaderUnavailable)
    {
      return;
    }

    m_ssrShader->Init();
    if (m_ssrShader->m_gpuData == nullptr)
    {
      TK_ERR("SsrPass: the fragment shader did not compile, screen space reflections are disabled.");
      m_shaderUnavailable = true;
      return;
    }

    // The pass reads the scene color and composites the result back into the same target, so it
    // samples a single sample copy of it (the same trick DoFPass uses). The copy carries a mip
    // chain: the reflection is gathered from it, and a mip chosen from the reflection cone is what
    // keeps a mirror reflection of a slanted surface from aliasing into stripes.
    TextureSettings copySet = m_params.ColorRt->Settings();
    copySet.msaaCount       = MsaaSampleCount::x0;
    copySet.GenerateMipMap  = true;
    copySet.MinFilter       = GraphicTypes::SampleLinearMipmapLinear;
    copySet.MagFilter       = GraphicTypes::SampleLinear;
    copySet.WarpS           = GraphicTypes::UVClampToEdge;
    copySet.WarpT           = GraphicTypes::UVClampToEdge;
    m_copyTexture->ReconstructIfNeeded(m_params.ColorRt->m_width, m_params.ColorRt->m_height, &copySet);

    GetRenderer()->CopyTexture(m_params.ColorRt, m_copyTexture);
    m_copyTexture->GenerateMipMaps();

    m_quadPass->SetFragmentShader(m_ssrShader, GetRenderer());
    m_filterPass->SetFragmentShader(m_filterShader, GetRenderer());

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

    // Step count: used exactly as requested (a uniform) so the setting is continuous. The shader
    // define only picks the compile time loop bound above it, which is what keeps the loop legal
    // without rounding the request away.
    const int steps                           = glm::clamp(m_params.StepCount, 1, m_maxStepCount);

    m_passDataBuffer.m_data.flags             = Vec4(m_params.DebugView ? 1.0f : 0.0f,
                                                     float(steps),
                                                     float(m_params.DebugViewMode),
                                                     0.0f);

    const int maxSteps                        = ClampMaxSteps(steps);
    if (maxSteps != m_currentMaxSteps)
    {
      m_ssrShader->Init();

      const void* variantBefore = m_ssrShader->m_gpuData.get();
      m_ssrShader->SetDefine("SSR_STEP_COUNT", std::to_string(maxSteps));
      const void* variantAfter = m_ssrShader->m_gpuData.get();

      TK_LOG("SsrPass: steps %d (loop bound %d), shader variant %p -> %p",
             steps,
             maxSteps,
             variantBefore,
             variantAfter);

      m_currentMaxSteps = maxSteps;
    }

    // Trace target: the reflection and its weight, at the scene resolution. The resolve phase reads it
    // back and composites, which is what lets the filter spread the reflection past the silhouettes it
    // can not see through.
    TextureSettings traceSet = copySet;
    traceSet.GenerateMipMap  = false;
    traceSet.MinFilter       = GraphicTypes::SampleLinear;
    traceSet.MagFilter       = GraphicTypes::SampleLinear;
    m_traceTexture->ReconstructIfNeeded(size.x, size.y, &traceSet);

    m_quadPass->m_params.frameBuffer->ReconstructIfNeeded({size.x, size.y, false, false});
    m_quadPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_traceTexture);

    m_filterPass->m_params.frameBuffer->ReconstructIfNeeded({size.x, size.y, false, false});
    m_filterPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_params.ColorRt);

    m_quadPass->m_params.blendFunc        = BlendFunction::NONE;
    m_quadPass->m_params.clearFrameBuffer = GraphicBitFields::None;
    m_filterPass->m_params.blendFunc      = BlendFunction::NONE;
    m_filterPass->m_params.clearFrameBuffer = GraphicBitFields::None;
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
    m_filterPass->SetFragmentShader(m_filterShader, renderer);

    m_passDataBuffer.Invalidate();
    m_passDataBuffer.Map();

    // Phase 1: trace the reflection into its own target, carrying its weight in the alpha channel.
    m_requirements = PassRequirements();
    GatherRequirements(m_requirements);
    m_requirements.fragmentShader                     = m_ssrShader;
    m_requirements.vertexShader                       = m_quadPass->m_material->GetVertexShaderVal();
    m_requirements.program                            = m_quadPass->GetProgram();
    m_requirements.frameBuffer                        = m_quadPass->m_params.frameBuffer;
    m_requirements.semanticTextures["s_diffuseColor"] = m_copyTexture;
    m_requirements.semanticTextures["s_normalDepth"]  = normalDepth;
    m_requirements.customUbos[7]                      = &m_passDataBuffer.GetBuffer();

    ApplyRequirements(renderer);
    RenderSubPass(m_quadPass);

    // Phase 2: filter that result in screen space and composite it over the scene color. The
    // requirements are rebuilt so the sampler names of the trace phase do not leak into this one.
    m_requirements = PassRequirements();
    GatherRequirements(m_requirements);
    m_requirements.fragmentShader                     = m_filterShader;
    m_requirements.vertexShader                       = m_filterPass->m_material->GetVertexShaderVal();
    m_requirements.program                            = m_filterPass->GetProgram();
    m_requirements.frameBuffer                        = m_filterPass->m_params.frameBuffer;
    m_requirements.semanticTextures["s_trace"]        = m_traceTexture;
    m_requirements.semanticTextures["s_sceneColor"]   = m_copyTexture;
    m_requirements.semanticTextures["s_normalDepth"]  = normalDepth;
    m_requirements.customUbos[7]                      = &m_passDataBuffer.GetBuffer();

    ApplyRequirements(renderer);
    RenderSubPass(m_filterPass);
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
