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
     * SSR_MAX_STEPS variants the fragment shader is compiled with. Only the loop bound is picked
     * from here; the requested step count itself travels as a uniform, so the setting stays
     * continuous and a value between two bounds is not rounded away.
     */
    int ClampMaxSteps(int steps)
    {
      if (steps <= 64)
      {
        return 64;
      }

      if (steps <= 128)
      {
        return 128;
      }

      return steps <= 256 ? 256 : 512;
    }
  } // namespace

  SsrPass::SsrPass() : Pass("SsrPass")
  {
    m_quadPass                       = MakeNewPtr<FullQuadPass>();
    m_quadPass->m_params.frameBuffer = MakeNewPtr<Framebuffer>("SsrFB");
    m_ssrShader                      = GetShaderManager()->Create<Shader>(ShaderPath("ssrFrag.shader", true));
    m_copyTexture                    = MakeNewPtr<RenderTarget>("SsrSceneCopyRT");

    m_accumPass                        = MakeNewPtr<FullQuadPass>();
    m_accumPass->m_params.frameBuffer  = MakeNewPtr<Framebuffer>("SsrAccumFB");
    m_accumShader                      = GetShaderManager()->Create<Shader>(ShaderPath("ssrAccumFrag.shader", true));

    m_filterPass                       = MakeNewPtr<FullQuadPass>();
    m_filterPass->m_params.frameBuffer = MakeNewPtr<Framebuffer>("SsrFilterFB");
    m_filterShader                     = GetShaderManager()->Create<Shader>(ShaderPath("ssrFilterFrag.shader", true));
    m_traceTexture                     = MakeNewPtr<RenderTarget>("SsrTraceRT");

    m_history[0]                       = MakeNewPtr<RenderTarget>("SsrHistoryRT0");
    m_history[1]                       = MakeNewPtr<RenderTarget>("SsrHistoryRT1");

    // Min depth pyramid the march walks. One target per level: the levels are separate textures
    // instead of the mips of one, because a level is built by reading the level below it, and a
    // framebuffer that samples the texture it writes to is undefined on GL and a layout conflict on
    // Vulkan.
    m_hizDepthShader                   = GetShaderManager()->Create<Shader>(ShaderPath("hiZDepthFrag.shader", true));
    m_hizReduceShader                  = GetShaderManager()->Create<Shader>(ShaderPath("hiZDownsampleFrag.shader", true));

    TextureSettings hizSet             = {};
    hizSet.WarpS                       = GraphicTypes::UVClampToEdge;
    hizSet.WarpT                       = GraphicTypes::UVClampToEdge;
    hizSet.MinFilter                   = GraphicTypes::SampleNearest;
    hizSet.MagFilter                   = GraphicTypes::SampleNearest;

    // 32F for the same reason the g buffer is: a rounded up nearest depth turns a tile into one the
    // ray can not cross, and the crossing it then fails to find is a missing reflection.
    hizSet.InternalFormat              = GraphicTypes::FormatRGBA32F;
    hizSet.Format                      = GraphicTypes::FormatRGBA;
    hizSet.Type                        = GraphicTypes::TypeFloat;
    hizSet.GenerateMipMap              = false;

    for (int i = 0; i < m_hizLevelCount; ++i)
    {
      m_hizLevels[i]                       = MakeNewPtr<RenderTarget>(128, 128, hizSet, "SsrHiZLevelRT");
      m_hizPasses[i]                       = MakeNewPtr<FullQuadPass>();
      m_hizPasses[i]->m_params.frameBuffer = MakeNewPtr<Framebuffer>("SsrHiZFB");
      m_hizPasses[i]->m_params.blendFunc   = BlendFunction::NONE;
      m_hizPasses[i]->m_params.clearFrameBuffer = GraphicBitFields::None;
    }
  }

  SsrPass::~SsrPass()
  {
    m_quadPass     = nullptr;
    m_ssrShader    = nullptr;
    m_copyTexture  = nullptr;

    m_accumPass    = nullptr;
    m_accumShader  = nullptr;
    m_filterPass   = nullptr;
    m_filterShader = nullptr;
    m_traceTexture = nullptr;
    m_history[0]   = nullptr;
    m_history[1]   = nullptr;

    for (int i = 0; i < m_hizLevelCount; ++i)
    {
      m_hizLevels[i] = nullptr;
      m_hizPasses[i] = nullptr;
    }

    m_hizDepthShader  = nullptr;
    m_hizReduceShader = nullptr;
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

    // The depth pyramid the march walks is built by two more shaders. Without them the trace phase
    // would sample levels that were never written, so the pass stays out of the frame instead.
    m_hizDepthShader->Init();
    m_hizReduceShader->Init();
    if (m_hizDepthShader->m_gpuData == nullptr || m_hizReduceShader->m_gpuData == nullptr)
    {
      TK_ERR("SsrPass: a depth pyramid shader did not compile, screen space reflections are disabled.");
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
    m_accumPass->SetFragmentShader(m_accumShader, GetRenderer());
    m_filterPass->SetFragmentShader(m_filterShader, GetRenderer());

    m_accumShader->Init();
    if (m_accumShader->m_gpuData == nullptr)
    {
      TK_ERR("SsrPass: the resolve shader did not compile, screen space reflections are disabled.");
      m_shaderUnavailable = true;
      return;
    }

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

    m_passDataBuffer.m_data.hizParams         = Vec4(float(m_hizLevelCount), float(m_hizTilePixels), 0.0f, 0.0f);

    const Mat4& view                         = m_params.Cam->GetViewMatrix();

    const int maxSteps                        = ClampMaxSteps(steps);
    if (maxSteps != m_currentMaxSteps)
    {
      m_ssrShader->Init();

      const void* variantBefore = m_ssrShader->m_gpuData.get();
      m_ssrShader->SetDefine("SSR_MAX_STEPS", std::to_string(maxSteps));
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

    // The resolve writes its result into one history target and reads the other, so a frame that
    // changes the resolution starts over instead of reprojecting a frame that no longer matches.
    TextureSettings historySet = traceSet;
    historySet.WarpS           = GraphicTypes::UVClampToEdge;
    historySet.WarpT           = GraphicTypes::UVClampToEdge;
    historySet.MinFilter       = GraphicTypes::SampleLinear;
    historySet.MagFilter       = GraphicTypes::SampleLinear;

    for (int i = 0; i < 2; ++i)
    {
      const bool sizeChanged = m_history[i]->m_width != size.x || m_history[i]->m_height != size.y;
      m_history[i]->ReconstructIfNeeded(size.x, size.y, &historySet);

      if (sizeChanged)
      {
        m_historyValid = false;
      }
    }

    m_accumPass->m_params.frameBuffer->ReconstructIfNeeded({size.x, size.y, false, false});
    m_accumPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                          m_history[m_historyWrite]);
    m_accumPass->m_params.blendFunc        = BlendFunction::NONE;
    m_accumPass->m_params.clearFrameBuffer = GraphicBitFields::None;

    m_historyValid                         = m_historyValid && !m_params.DebugView;

    // Reprojection for the temporal accumulation: this frame's view space position to last frame's
    // clip, which is exact for geometry that did not move. Depth is enough to find where a point was,
    // so the camera matrices are the whole story. Filled here, after the history targets are known,
    // so a frame that changed the resolution does not reproject into the history it just dropped.
    m_passDataBuffer.m_data.prevReprojection = m_prevViewProj * glm::inverse(m_prevView) * glm::inverse(view);
    m_passDataBuffer.m_data.temporal         = Vec4(m_historyValid ? m_temporalBlend : 0.0f,
                                                    m_historyValid ? 1.0f : 0.0f,
                                                    0.0f,
                                                    0.0f);

    m_prevViewProj                           = proj * view;
    m_prevView                               = view;

    m_filterPass->m_params.frameBuffer->ReconstructIfNeeded({size.x, size.y, false, false});
    m_filterPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_params.ColorRt);

    m_quadPass->m_params.blendFunc        = BlendFunction::NONE;
    m_quadPass->m_params.clearFrameBuffer = GraphicBitFields::None;
    m_filterPass->m_params.blendFunc      = BlendFunction::NONE;
    m_filterPass->m_params.clearFrameBuffer = GraphicBitFields::None;

    // The depth pyramid the trace phase walks: level 1 stands on the g buffer and every level above
    // it on the level below, so a level covers m_hizTilePixels times the screen area of the previous
    // one. Sizes are rounded up so the whole frame stays covered.
    int divisor = 1;

    for (int i = 0; i < m_hizLevelCount; ++i)
    {
      divisor *= m_hizTilePixels;

      const int hizWidth  = glm::max(1, (size.x + divisor - 1) / divisor);
      const int hizHeight = glm::max(1, (size.y + divisor - 1) / divisor);

      TextureSettings hizSet = m_hizLevels[i]->Settings();
      m_hizLevels[i]->ReconstructIfNeeded(hizWidth, hizHeight, &hizSet);

      m_hizPasses[i]->m_params.frameBuffer->ReconstructIfNeeded({hizWidth, hizHeight, false, false});
      m_hizPasses[i]->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                               m_hizLevels[i]);
      m_hizPasses[i]->SetFragmentShader(i == 0 ? m_hizDepthShader : m_hizReduceShader, GetRenderer());
    }
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

    // Phase 0: build the min depth pyramid the trace phase walks. Level by level, because every
    // level is built from the one below it, and the source of a level is a different target than
    // the one it writes to.
    for (int i = 0; i < m_hizLevelCount; ++i)
    {
      m_requirements = PassRequirements();
      GatherRequirements(m_requirements);
      m_requirements.fragmentShader = i == 0 ? m_hizDepthShader : m_hizReduceShader;
      m_requirements.vertexShader   = m_hizPasses[i]->m_material->GetVertexShaderVal();
      m_requirements.program        = m_hizPasses[i]->GetProgram();
      m_requirements.frameBuffer    = m_hizPasses[i]->m_params.frameBuffer;

      if (i == 0)
      {
        m_requirements.semanticTextures["s_normalDepth"] = normalDepth;
      }
      else
      {
        m_requirements.semanticTextures["s_hiz"] = m_hizLevels[i - 1];
      }

      ApplyRequirements(renderer);
      RenderSubPass(m_hizPasses[i]);
    }

    // Phase 1: trace the reflection into its own target, carrying its weight in the alpha channel.
    m_requirements = PassRequirements();
    GatherRequirements(m_requirements);
    m_requirements.fragmentShader                     = m_ssrShader;
    m_requirements.vertexShader                       = m_quadPass->m_material->GetVertexShaderVal();
    m_requirements.program                            = m_quadPass->GetProgram();
    m_requirements.frameBuffer                        = m_quadPass->m_params.frameBuffer;
    m_requirements.semanticTextures["s_diffuseColor"] = m_copyTexture;
    m_requirements.semanticTextures["s_normalDepth"]  = normalDepth;
    m_requirements.semanticTextures["s_hiz1"]         = m_hizLevels[0];
    m_requirements.semanticTextures["s_hiz2"]         = m_hizLevels[1];
    m_requirements.semanticTextures["s_hiz3"]         = m_hizLevels[2];
    m_requirements.semanticTextures["s_hiz4"]         = m_hizLevels[3];
    m_requirements.customUbos[7]                      = &m_passDataBuffer.GetBuffer();

    ApplyRequirements(renderer);
    RenderSubPass(m_quadPass);

    // Phase 2: resolve that result in screen space and accumulate it over the last frames. A screen
    // space reflection is one sample per pixel per frame, so without reusing the last frames it is
    // resampled from scratch whenever the camera or the geometry moves, which reads as noise and
    // shimmer. Debug views are data rather than colour and are handed to the composite untouched.
    RenderTargetPtr resolved = m_traceTexture;

    if (!m_params.DebugView)
    {
      m_requirements = PassRequirements();
      GatherRequirements(m_requirements);
      m_requirements.fragmentShader                    = m_accumShader;
      m_requirements.vertexShader                      = m_accumPass->m_material->GetVertexShaderVal();
      m_requirements.program                           = m_accumPass->GetProgram();
      m_requirements.frameBuffer                       = m_accumPass->m_params.frameBuffer;
      m_requirements.semanticTextures["s_trace"]       = m_traceTexture;
      m_requirements.semanticTextures["s_history"]     = m_history[m_historyWrite ^ 1];
      m_requirements.semanticTextures["s_normalDepth"] = normalDepth;
      m_requirements.customUbos[7]                     = &m_passDataBuffer.GetBuffer();

      ApplyRequirements(renderer);
      RenderSubPass(m_accumPass);

      resolved       = m_history[m_historyWrite];
      m_historyValid = true;
      m_historyWrite ^= 1;
    }

    // Phase 3: composite the resolved reflection over the scene color. The requirements are rebuilt so
    // the sampler names of the earlier phases do not leak into this one.
    m_requirements = PassRequirements();
    GatherRequirements(m_requirements);
    m_requirements.fragmentShader                     = m_filterShader;
    m_requirements.vertexShader                       = m_filterPass->m_material->GetVertexShaderVal();
    m_requirements.program                            = m_filterPass->GetProgram();
    m_requirements.frameBuffer                        = m_filterPass->m_params.frameBuffer;
    m_requirements.semanticTextures["s_trace"]        = resolved;
    m_requirements.semanticTextures["s_sceneColor"]   = m_copyTexture;
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
