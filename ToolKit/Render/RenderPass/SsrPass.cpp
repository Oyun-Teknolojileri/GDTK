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

    // The resolve phase composites into a target of the pass's own, so the frame it copied stays read
    // only for the rest of the frame. The path reads that target through GetOutputFramebuffer.
    m_reflectionRt                   = MakeNewPtr<RenderTarget>("SsrReflectionRT");
    m_reflectionFb                   = MakeNewPtr<Framebuffer>("SsrReflectionFB");

    m_filterPass                       = MakeNewPtr<FullQuadPass>();
    m_filterPass->m_params.frameBuffer = m_reflectionFb;
    m_filterShader                     = GetShaderManager()->Create<Shader>(ShaderPath("ssrFilterFrag.shader", true));
    m_traceTexture                     = MakeNewPtr<RenderTarget>("SsrTraceRT");
  }

  SsrPass::~SsrPass()
  {
    m_quadPass      = nullptr;
    m_ssrShader     = nullptr;
    m_copyTexture   = nullptr;

    m_filterPass    = nullptr;
    m_filterShader  = nullptr;
    m_traceTexture  = nullptr;

    m_reflectionRt  = nullptr;
    m_reflectionFb  = nullptr;
  }

  FramebufferPtr SsrPass::GetOutputFramebuffer() const
  {
    if (!m_outputWritten || m_reflectionFb == nullptr ||
        m_reflectionFb->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0) == nullptr)
    {
      return nullptr;
    }

    return m_reflectionFb;
  }

  void SsrPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    if (m_params.ColorRt == nullptr || m_params.GNormalDepthBuffer == nullptr || m_params.Cam == nullptr)
    {
      m_outputWritten = false;
      return;
    }

    // A fragment shader that failed to compile keeps a null GpuResourceData, and handing that to the
    // program creation makes the backend dereference it. The backend has already logged the compile
    // error, so disable the pass instead of taking the renderer down with it.
    if (m_shaderUnavailable)
    {
      m_outputWritten = false;
      return;
    }

    // The step count is a compile time loop bound, so the variant has to be selected before the first
    // compile. Setting it afterwards left the first Init with no SSR_STEP_COUNT chosen at all: the shader
    // was never built, m_gpuData stayed null, and the pass reported a shader that "did not compile" while
    // the offline compiler builds the same source without a complaint.
    const int steps    = glm::clamp(m_params.StepCount, 1, m_maxStepCount);
    const int maxSteps = ClampMaxSteps(steps);
    if (maxSteps != m_currentMaxSteps)
    {
      m_ssrShader->SetDefine("SSR_STEP_COUNT", std::to_string(maxSteps));
      m_currentMaxSteps = maxSteps;
      TK_LOG("SsrPass: step count %d, compile time loop bound %d.", steps, maxSteps);
    }

    m_ssrShader->Init();

    // A shader that carries defines leaves Init without gpu data on purpose: Init only sets up the variant
    // selection, and the variant itself is compiled when a program first asks for it. Judging this one by its
    // gpu data disabled the pass on every frame of every run, which is why screen space reflections never
    // appeared while the shader itself was fine. Only a shader without defines compiles inside Init.
    if (m_ssrShader->m_defineArray.empty() && m_ssrShader->m_gpuData == nullptr)
    {
      TK_ERR("SsrPass: the fragment shader did not compile, screen space reflections are disabled.");
      m_shaderUnavailable = true;
      m_outputWritten     = false;
      return;
    }

    // The pass reads the scene color and writes a target of its own, so it samples a single sample copy
    // of the frame (the same trick DoFPass uses). Beyond keeping the read and the write apart, the copy
    // carries a mip chain: the reflection is gathered from it, and a mip chosen from the reflection cone
    // is what keeps a mirror reflection of a slanted surface from aliasing into stripes.
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

    // Step count: used exactly as requested (a uniform) so the setting is continuous; the compile time
    // loop bound was selected in PreRender, before the shader was built.
    m_passDataBuffer.m_data.flags             = Vec4(m_params.DebugView ? 1.0f : 0.0f,
                                                     float(steps),
                                                     float(m_params.DebugViewMode),
                                                     0.0f);

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

    // The composite target: the frame with the reflections in it, at the frame's resolution and format so
    // everything behind this pass reads it instead of the frame. No mip chain and no multisampling, because
    // it is written by one full quad and then sampled one to one.
    TextureSettings reflectionSet = copySet;
    reflectionSet.GenerateMipMap  = false;
    reflectionSet.MinFilter       = GraphicTypes::SampleLinear;
    reflectionSet.MagFilter       = GraphicTypes::SampleLinear;
    m_reflectionRt->ReconstructIfNeeded(size.x, size.y, &reflectionSet);

    m_filterPass->m_params.frameBuffer->ReconstructIfNeeded({size.x, size.y, false, false});
    m_filterPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_reflectionRt);

    m_quadPass->m_params.blendFunc        = BlendFunction::NONE;
    m_quadPass->m_params.clearFrameBuffer = GraphicBitFields::None;
    m_filterPass->m_params.blendFunc      = BlendFunction::NONE;
    m_filterPass->m_params.clearFrameBuffer = GraphicBitFields::None;
  }

  void SsrPass::Render()
  {
    TK_PROFILE_FUNCTION();

    // PreRender is where the targets get their size, and it bails out when the shader did not compile or
    // the params are missing. Drawing anyway hands the backend a framebuffer that was never given a size, and
    // the pass start dereferences its null gpu data: that is the crash this guard removes. The shader failure
    // itself is reported by PreRender.
    if (m_shaderUnavailable || m_params.ColorRt == nullptr || m_params.GNormalDepthBuffer == nullptr ||
        m_params.Cam == nullptr)
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

    // The sampler state the march needs, stated here rather than inherited. This target is a float render
    // target with no mip chain, and a default sampler asks for mipmaps it does not have: the texture is then
    // incomplete, every fetch returns zero, and the march runs against a depth of zero and finds nothing.
    // That is why the reflections only showed up after ambient occlusion had been enabled - it applies the same
    // state, once, on the pass that used to be the only one reading this target.
    TextureSettings ndSettings = normalDepth->Settings();
    ndSettings.MinFilter       = GraphicTypes::SampleNearest;
    ndSettings.MagFilter       = GraphicTypes::SampleNearest;
    ndSettings.WarpS           = GraphicTypes::UVClampToEdge;
    ndSettings.WarpT           = GraphicTypes::UVClampToEdge;
    normalDepth->ApplyTextureSettings(ndSettings);

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

    // Phase 2: filter that result in screen space and composite the scene copy with it into the pass's
    // own target. The requirements are rebuilt so the sampler names of the trace phase do not leak into
    // this one.
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

    // The quad above wrote every pixel of the target, so it now holds this frame's picture and the passes
    // behind this one may read it.
    m_outputWritten = true;
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
