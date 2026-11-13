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

    /** Texels of the full resolution target one texel of the average carries. */
    constexpr int SSR_AVG_RADIANCE_DIVISOR = 8;

    /** Variance a pixel starts with when it has no usable history. */
    constexpr float SSR_DEFAULT_VARIANCE   = 1.0f;

    /** Taps the resolve's spatial window may use. The window is the reference's 15 tap Halton set. */
    constexpr int SSR_FILTER_TAPS_HIGH     = 15;
    constexpr int SSR_FILTER_TAPS_LOW      = 6;
  } // namespace

  SsrPass::SsrPass() : Pass("SsrPass")
  {
    m_quadPass                       = MakeNewPtr<FullQuadPass>();
    m_quadPass->m_params.frameBuffer = MakeNewPtr<Framebuffer>("SsrFB");
    m_ssrShader                      = GetShaderManager()->Create<Shader>(ShaderPath("ssrFrag.shader", true));
    m_copyTexture                    = MakeNewPtr<RenderTarget>("SsrSceneCopyRT");

    // The blue noise tile the sampler walks. It is an engine resource, so it is loaded like a shader and
    // lives next to them in the engine resource tree. Loaded by hand instead of through the manager: the
    // manager substitutes the class default when the file is not found, which would silently replace the
    // tile with the default image and leave the sampler reading a 1024 square of something else.
    m_blueNoise = MakeNewPtr<Texture>();
    m_blueNoise->SetFile(TexturePath("blueNoise.png", true));
    m_blueNoise->Load();
    m_blueNoise->Init();

    // The composite goes into a target of the pass's own, so the frame it copied stays read only for
    // the rest of the frame. The path reads that target through GetOutputFramebuffer.
    m_reflectionRt                   = MakeNewPtr<RenderTarget>("SsrReflectionRT");
    m_reflectionFb                   = MakeNewPtr<Framebuffer>("SsrReflectionFB");

    // Phase one writes one sample per pixel, phase two its 1/8 resolution average, phase three filters
    // it in screen space and accumulates it, phase four composites it over the frame.
    m_traceTexture                      = MakeNewPtr<RenderTarget>("SsrTraceRT");
    m_hitTexture[0]                     = MakeNewPtr<RenderTarget>("SsrHit0RT");
    m_hitTexture[1]                     = MakeNewPtr<RenderTarget>("SsrHit1RT");
    m_avgRadiance                       = MakeNewPtr<RenderTarget>("SsrAvgRadianceRT");
    m_history[0]                        = MakeNewPtr<RenderTarget>("SsrHistory0RT");
    m_history[1]                        = MakeNewPtr<RenderTarget>("SsrHistory1RT");
    m_historyCount[0]                   = MakeNewPtr<RenderTarget>("SsrHistoryCount0RT");
    m_historyCount[1]                   = MakeNewPtr<RenderTarget>("SsrHistoryCount1RT");
    m_prevNormalDepth                   = MakeNewPtr<RenderTarget>("SsrPrevNormalDepthRT");

    m_avgPass                           = MakeNewPtr<FullQuadPass>();
    m_avgPass->m_params.frameBuffer     = MakeNewPtr<Framebuffer>("SsrAvgFB");
    m_avgShader                         = GetShaderManager()->Create<Shader>(ShaderPath("ssrAvgRadianceFrag.shader", true));

    m_pyramidPass                       = MakeNewPtr<FullQuadPass>();
    m_pyramidPass->m_params.frameBuffer = MakeNewPtr<Framebuffer>("SsrPyramidFB");
    m_pyramidShader                     =
        GetShaderManager()->Create<Shader>(ShaderPath("ssrDepthPyramidFrag.shader", true));

    for (int level = 0; level < m_pyramidLevels; ++level)
    {
      m_depthPyramid[level] = MakeNewPtr<RenderTarget>("SsrDepthPyramid" + std::to_string(level));
    }

    m_resolvePass                       = MakeNewPtr<FullQuadPass>();
    m_resolvePass->m_params.frameBuffer = MakeNewPtr<Framebuffer>("SsrResolveFB");
    m_resolveShader                     = GetShaderManager()->Create<Shader>(ShaderPath("ssrResolveFrag.shader", true));

    m_compositePass                       = MakeNewPtr<FullQuadPass>();
    m_compositePass->m_params.frameBuffer = m_reflectionFb;
    m_compositeShader                     =
        GetShaderManager()->Create<Shader>(ShaderPath("ssrCompositeFrag.shader", true));
  }

  SsrPass::~SsrPass()
  {
    m_quadPass      = nullptr;
    m_ssrShader     = nullptr;
    m_copyTexture   = nullptr;
    m_blueNoise     = nullptr;

    m_avgPass       = nullptr;
    m_avgShader     = nullptr;

    m_pyramidPass   = nullptr;
    m_pyramidShader = nullptr;

    for (int level = 0; level < m_pyramidLevels; ++level)
    {
      m_depthPyramid[level] = nullptr;
    }

    m_resolvePass   = nullptr;
    m_resolveShader = nullptr;

    m_compositePass   = nullptr;
    m_compositeShader = nullptr;

    m_traceTexture       = nullptr;
    m_hitTexture[0]      = nullptr;
    m_hitTexture[1]      = nullptr;
    m_avgRadiance        = nullptr;
    m_history[0]         = nullptr;
    m_history[1]         = nullptr;
    m_historyCount[0]    = nullptr;
    m_historyCount[1]    = nullptr;
    m_prevNormalDepth    = nullptr;

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
      m_hasHistory    = false;
      return;
    }

    // A fragment shader that failed to compile keeps a null GpuResourceData, and handing that to the
    // program creation makes the backend dereference it. The backend has already logged the compile
    // error, so disable the pass instead of taking the renderer down with it.
    if (m_shaderUnavailable)
    {
      m_outputWritten = false;
      m_hasHistory    = false;
      return;
    }

    // Init first: it is what builds the define selection SetDefine edits, so a SetDefine that runs before it
    // fails with "Initialize the shader before setting a value for a define" and leaves the shader on its
    // first variant. The step count below was stored as if it had been applied, so the pass never asked
    // again, and the march ran the first variant's loop bound while the shader divided the screen by the
    // requested count: at 64 steps the reflection reached an eighth of the way to the screen edge and the
    // surfaces looked unreflective, at 8 it covered the whole screen. Init also keeps the rest of this
    // ordering: a shader with defines sets up its variants here and compiles them on demand, so the define
    // is still selected before the first compile.
    m_ssrShader->Init();
    m_avgShader->Init();
    m_pyramidShader->Init();
    m_resolveShader->Init();
    m_compositeShader->Init();

    const int steps    = glm::clamp(m_params.StepCount, 1, m_maxStepCount);
    const int maxSteps = ClampMaxSteps(steps);
    if (maxSteps != m_currentMaxSteps)
    {
      m_ssrShader->SetDefine("SSR_STEP_COUNT", std::to_string(maxSteps));
      m_currentMaxSteps = maxSteps;
      TK_LOG("SsrPass: step count %d, compile time loop bound %d.", steps, maxSteps);
    }

    // The shader sizes a step so that `stepCount` of them cross the screen diagonal, so the count it marches
    // by has to be one the loop can run. A request above the loop bound - the 256 default is above it -
    // divides the screen into more steps than the loop has and the rays stop a fraction of the way to the
    // edge; the marched count is the one the bound can honor.
    const int marchedSteps = glm::min(steps, m_currentMaxSteps);

    // A shader that carries defines leaves Init without gpu data on purpose: Init only sets up the variant
    // selection, and the variant itself is compiled when a program first asks for it. Judging this one by its
    // gpu data disabled the pass on every frame of every run, which is why screen space reflections never
    // appeared while the shader itself was fine. Only a shader without defines compiles inside Init.
    if (m_ssrShader->m_defineArray.empty() && m_ssrShader->m_gpuData == nullptr)
    {
      TK_ERR("SsrPass: the fragment shader did not compile, screen space reflections are disabled.");
      m_shaderUnavailable = true;
      m_outputWritten     = false;
      m_hasHistory        = false;
      return;
    }

    // The rest of the shaders carry no defines, so Init compiled them above and a null gpu data here is a
    // real compile error. Same rule as the trace: report once, keep the pass out of the frame.
    if (m_avgShader->m_gpuData == nullptr || m_resolveShader->m_gpuData == nullptr ||
        m_pyramidShader->m_gpuData == nullptr || m_compositeShader->m_gpuData == nullptr)
    {
      TK_ERR("SsrPass: the pyramid, average, resolve or composite shader did not compile, screen space "
             "reflections are disabled.");
      m_shaderUnavailable = true;
      m_outputWritten     = false;
      m_hasHistory        = false;
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
    m_avgPass->SetFragmentShader(m_avgShader, GetRenderer());
    m_pyramidPass->SetFragmentShader(m_pyramidShader, GetRenderer());
    m_resolvePass->SetFragmentShader(m_resolveShader, GetRenderer());
    m_compositePass->SetFragmentShader(m_compositeShader, GetRenderer());

    if (!m_passDataBufferInitialized)
    {
      m_passDataBuffer.Init(7);
      m_passDataBufferInitialized = true;
    }

    const Mat4& proj                           = m_params.Cam->GetProjectionMatrix();
    const Mat4& view                           = m_params.Cam->GetViewMatrix();
    const Mat4 viewProjection                  = proj * view;
    const IVec2 size(m_copyTexture->m_width, m_copyTexture->m_height);

    m_passDataBuffer.m_data.view               = view;
    m_passDataBuffer.m_data.inverseProjection  = glm::inverse(proj);
    m_passDataBuffer.m_data.inverseView        = glm::inverse(view);
    m_passDataBuffer.m_data.prevViewProjection = m_prevViewProjection;
    m_passDataBuffer.m_data.envRotation        = m_params.EnvRotation;

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

    // Step count: the count the loop bound can honor, as a uniform, because the shader divides the screen
    // diagonal by it. The bound itself was selected in PreRender, before the shader was built. The frame
    // index seeds the sample sequence, so consecutive frames sample different parts of the lobe.
    m_passDataBuffer.m_data.flags =
        Vec4(m_params.DebugView ? 1.0f : 0.0f, float(marchedSteps), float(m_params.DebugViewMode), m_frameIndex);

    // The three fades, each with the fraction it starts at. They live in the pass data rather than in the
    // shader so the border and the two lengths can be tuned per project without a recompile. The fourth
    // component flags a moving view, which the resolve reads to widen its kernel and lean on the history
    // less: a reprojected reflection follows the surface, not the reflected content, so it is stale exactly
    // while the camera moves.
    //
    // The tolerance is a tenth of a millimetre in world units, not the smallest float step a matrix cell
    // can carry: a host that recomputes the camera from its transform every frame ends up with cells that
    // differ in the last bits, and treating that as motion pins the flag on forever - the accumulation
    // never holds and a still surface keeps crawling.
    const float viewMotionEpsilon = 1e-4f;

    m_viewMoved                   = false;
    for (int column = 0; column < 4 && !m_viewMoved; ++column)
    {
      for (int row = 0; row < 4; ++row)
      {
        if (glm::abs(viewProjection[column][row] - m_prevViewProjection[column][row]) > viewMotionEpsilon)
        {
          m_viewMoved = true;
          break;
        }
      }
    }

    m_passDataBuffer.m_data.fades =
        Vec4(m_params.EdgeFade, m_params.ScreenFadeStart, m_params.RangeFadeStart, m_viewMoved ? 1.0f : 0.0f);

    // The resolution the reflection itself is built at. The targets are the bandwidth, so a level down is
    // a quarter of the traffic (a level is a halving in each axis). The composite stays at frame resolution
    // and upsamples what it reads, so the reflection keeps the full screen coverage; only its detail gets
    // coarser, and the mirror sharpness goes with it.
    const int   divisor = m_params.Quality >= 2 ? 1 : (m_params.Quality == 1 ? 2 : 4);
    const IVec2 ssrSize = IVec2(glm::max(size.x / divisor, 1), glm::max(size.y / divisor, 1));

    // History lifetime: a different camera, a resize or the first frame of the pass can not reuse the
    // accumulation, and neither can a frame that skipped the pass.
    const ObjectId cameraId = m_params.Cam->GetIdVal();
    bool resetHistory       = !m_hasHistory || m_historyCameraId != cameraId;

    if (m_historySize != ssrSize)
    {
      m_historySize = ssrSize;
      resetHistory  = true;
    }

    m_historyCameraId = cameraId;

    // The reflection targets: the trace carries one sample per pixel, the average the low frequency the
    // temporal clip leans on, and the history the accumulation with its sample count.
    TextureSettings traceSet = copySet;
    traceSet.GenerateMipMap  = false;
    traceSet.MinFilter       = GraphicTypes::SampleNearest;
    traceSet.MagFilter       = GraphicTypes::SampleNearest;

    // Nearest, always: GL ES 3.0 does not guarantee linear filtering of a float render target
    // (OES_texture_float_linear is optional), and a device without it samples such a target as black. The
    // composite does the upsample itself, in the shader, with four nearest taps.
    TextureSettings historyTargetSet = traceSet;

    // The sample count fits in 8 bits (the cap is well under 255), so the counter target can be a byte per
    // channel instead of eight.
    TextureSettings countSet = traceSet;
    if (m_params.Quality < 2)
    {
      // All three, not just the internal format: the pixel format and the type describe the same storage,
      // and a target whose internal format is a byte but whose type says float is an incomplete
      // framebuffer on GL ES - the resolve then writes nothing and the accumulation stays black.
      countSet.InternalFormat = GraphicTypes::FormatRGBA8;
      countSet.Format         = GraphicTypes::FormatRGBA;
      countSet.Type           = GraphicTypes::TypeUnsignedByte;
    }

    m_traceTexture->ReconstructIfNeeded(ssrSize.x, ssrSize.y, &traceSet);
    m_hitTexture[0]->ReconstructIfNeeded(ssrSize.x, ssrSize.y, &traceSet);
    m_hitTexture[1]->ReconstructIfNeeded(ssrSize.x, ssrSize.y, &traceSet);
    m_history[0]->ReconstructIfNeeded(ssrSize.x, ssrSize.y, &historyTargetSet);
    m_history[1]->ReconstructIfNeeded(ssrSize.x, ssrSize.y, &historyTargetSet);
    m_historyCount[0]->ReconstructIfNeeded(ssrSize.x, ssrSize.y, &countSet);
    m_historyCount[1]->ReconstructIfNeeded(ssrSize.x, ssrSize.y, &countSet);

    // The min depth hierarchy the march climbs. Level 0 is half the screen and every level after it half
    // of the one before, so a step taken at level k covers 2^k of level 0's tiles at once.
    TextureSettings pyramidSet = traceSet;
    pyramidSet.MinFilter       = GraphicTypes::SampleNearest;
    pyramidSet.MagFilter       = GraphicTypes::SampleNearest;
    pyramidSet.WarpS           = GraphicTypes::UVClampToEdge;
    pyramidSet.WarpT           = GraphicTypes::UVClampToEdge;

    for (int level = 0; level < m_pyramidLevels; ++level)
    {
      const int divisor = 1 << (level + 1);
      m_depthPyramid[level]->ReconstructIfNeeded(glm::max(ssrSize.x / divisor, 1),
                                                 glm::max(ssrSize.y / divisor, 1),
                                                 &pyramidSet);
    }

    const int avgWidth  = glm::max(ssrSize.x / SSR_AVG_RADIANCE_DIVISOR, 1);
    const int avgHeight = glm::max(ssrSize.y / SSR_AVG_RADIANCE_DIVISOR, 1);
    m_avgRadiance->ReconstructIfNeeded(avgWidth, avgHeight, &traceSet);

    // The g buffer the history was written for. Its format follows the source, so the disocclusion test
    // compares like with like whether the g buffer is 16 bit float or not.
    TextureSettings historySet = m_params.GNormalDepthBuffer->Settings();
    historySet.msaaCount       = MsaaSampleCount::x0;
    historySet.GenerateMipMap  = false;
    historySet.MinFilter       = GraphicTypes::SampleNearest;
    historySet.MagFilter       = GraphicTypes::SampleNearest;
    historySet.WarpS           = GraphicTypes::UVClampToEdge;
    historySet.WarpT           = GraphicTypes::UVClampToEdge;
    m_prevNormalDepth->ReconstructIfNeeded(m_params.GNormalDepthBuffer->m_width,
                                           m_params.GNormalDepthBuffer->m_height,
                                           &historySet);

    // The composite target: the frame with the reflections in it, at the frame's resolution and format so
    // everything behind this pass reads it instead of the frame. No mip chain and no multisampling, because
    // it is written by one full quad and then sampled one to one.
    TextureSettings reflectionSet = copySet;
    reflectionSet.GenerateMipMap  = false;
    reflectionSet.MinFilter       = GraphicTypes::SampleLinear;
    reflectionSet.MagFilter       = GraphicTypes::SampleLinear;
    m_reflectionRt->ReconstructIfNeeded(size.x, size.y, &reflectionSet);

    // The accumulation runs on every frame. What keeps a still surface from crawling is no longer a fixed
    // response: the resolve blends by how many samples a pixel holds, and the sample cap follows the
    // roughness. A reset (a different camera, a resize) travels as the fourth component and drops the
    // history for this frame.
    m_passDataBuffer.m_data.temporal = Vec4(glm::clamp(m_params.TemporalResponseMin, 0.0f, 1.0f),
                                            glm::clamp(m_params.TemporalResponseMax, 0.0f, 1.0f),
                                            glm::clamp(m_params.BrdfBias, 0.0f, 1.0f),
                                            resetHistory ? 1.0f : 0.0f);

    const bool hasVelocity = m_params.VelocityCurrent != nullptr && m_params.VelocityPrevious != nullptr;
    m_passDataBuffer.m_data.velocity = Vec4(hasVelocity ? 1.0f : 0.0f, float(size.y), 0.0f, 0.0f);

    // Environment: the sky's pre filtered radiance. The mip a sample reads is roughness scaled, so the
    // shader needs the highest mip the cubemap carries and whether it is there at all.
    const float envIntensity = m_params.EnvSpecular != nullptr ? glm::max(m_params.EnvIntensity, 0.0f) : 0.0f;
    const float envMaxMip    = float(RHIConstants::SpecularIBLLods - 1);
    m_passDataBuffer.m_data.environment = Vec4(envIntensity, envMaxMip, envIntensity > 0.0f ? 1.0f : 0.0f, 0.0f);

    m_passDataBuffer.m_data.denoiser = Vec4(glm::max(m_params.MaxSamples, 1.0f),
                                            glm::clamp(m_params.DisocclusionThreshold, 0.0f, 1.0f),
                                            glm::max(m_params.HistoryClipWeight, 0.0f),
                                            SSR_DEFAULT_VARIANCE);

    // Quality: how many taps the spatial filter may use, and the scale the reflection was traced at so a
    // shader that needs to know the grid can ask.
    const int   taps  = m_params.Quality >= 1 ? SSR_FILTER_TAPS_HIGH : SSR_FILTER_TAPS_LOW;
    const float scale = 1.0f / float(divisor);
    m_passDataBuffer.m_data.quality = Vec4(float(taps), scale, 0.0f, 0.0f);

    m_quadPass->m_params.frameBuffer->ReconstructIfNeeded({ssrSize.x, ssrSize.y, false, false});
    m_quadPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_traceTexture);
    m_quadPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment1,
                                                             m_hitTexture[m_hitIndex]);

    m_avgPass->m_params.frameBuffer->ReconstructIfNeeded({avgWidth, avgHeight, false, false});
    m_avgPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_avgRadiance);

    m_resolvePass->m_params.frameBuffer->ReconstructIfNeeded({ssrSize.x, ssrSize.y, false, false});

    m_compositePass->m_params.frameBuffer->ReconstructIfNeeded({size.x, size.y, false, false});
    m_compositePass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                              m_reflectionRt);

    m_quadPass->m_params.blendFunc            = BlendFunction::NONE;
    m_quadPass->m_params.clearFrameBuffer     = GraphicBitFields::None;
    m_avgPass->m_params.blendFunc             = BlendFunction::NONE;
    m_avgPass->m_params.clearFrameBuffer      = GraphicBitFields::None;
    m_resolvePass->m_params.blendFunc         = BlendFunction::NONE;
    m_resolvePass->m_params.clearFrameBuffer  = GraphicBitFields::None;
    m_compositePass->m_params.blendFunc       = BlendFunction::NONE;
    m_compositePass->m_params.clearFrameBuffer = GraphicBitFields::None;

    m_passDataBuffer.Invalidate();
    m_passDataBuffer.Map();
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
    m_avgPass->SetFragmentShader(m_avgShader, renderer);
    m_resolvePass->SetFragmentShader(m_resolveShader, renderer);
    m_compositePass->SetFragmentShader(m_compositeShader, renderer);

    // One history target is read and the other is written, and they swap at the end of the frame: a pass
    // can not sample the attachment it draws into.
    const int readIndex  = m_historyIndex;
    const int writeIndex = 1 - m_historyIndex;

    m_resolvePass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                            m_history[writeIndex]);
    m_resolvePass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment1,
                                                            m_historyCount[writeIndex]);

    // Phase 0: the min depth hierarchy the march climbs. One draw per level, each reading the level below
    // it - the g buffer for the first - so a level costs a quarter of the one below in pixels.
    for (int level = 0; level < m_pyramidLevels; ++level)
    {
      TexturePtr pyramidSource = level == 0 ? normalDepth : m_depthPyramid[level - 1];

      m_pyramidPass->m_params.frameBuffer->ReconstructIfNeeded(
          {m_depthPyramid[level]->m_width, m_depthPyramid[level]->m_height, false, false});
      m_pyramidPass->m_params.frameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                              m_depthPyramid[level]);

      m_requirements = PassRequirements();
      GatherRequirements(m_requirements);
      m_requirements.fragmentShader                     = m_pyramidShader;
      m_requirements.vertexShader                       = m_pyramidPass->m_material->GetVertexShaderVal();
      m_requirements.program                            = m_pyramidPass->GetProgram();
      m_requirements.frameBuffer                        = m_pyramidPass->m_params.frameBuffer;
      m_requirements.semanticTextures["s_pyramidDepth"] = pyramidSource;
      m_requirements.customUbos[7]                      = &m_passDataBuffer.GetBuffer();

      ApplyRequirements(renderer);
      RenderSubPass(m_pyramidPass);
    }

    // Phase 1: one ray per pixel. Where the march finds nothing the environment fills in, so every
    // reflective pixel leaves with a complete reflection and a coverage of one.
    m_requirements = PassRequirements();
    GatherRequirements(m_requirements);
    m_requirements.fragmentShader                     = m_ssrShader;
    m_requirements.vertexShader                       = m_quadPass->m_material->GetVertexShaderVal();
    m_requirements.program                            = m_quadPass->GetProgram();
    m_requirements.frameBuffer                        = m_quadPass->m_params.frameBuffer;
    m_requirements.semanticTextures["s_diffuseColor"] = m_copyTexture;
    m_requirements.semanticTextures["s_normalDepth"]  = normalDepth;
    if (m_params.EnvSpecular != nullptr)
    {
      m_requirements.semanticTextures["s_envSpecular"] = m_params.EnvSpecular;
    }
    if (m_blueNoise != nullptr)
    {
      m_requirements.semanticTextures["s_blueNoise"] = m_blueNoise;
    }
    for (int level = 0; level < m_pyramidLevels; ++level)
    {
      m_requirements.semanticTextures["s_pyramid" + std::to_string(level)] = m_depthPyramid[level];
    }
    m_requirements.customUbos[7]                      = &m_passDataBuffer.GetBuffer();

    ApplyRequirements(renderer);
    RenderSubPass(m_quadPass);

    // Phase 2: the 1/8 resolution average of the trace, which is the low frequency the temporal clip
    // leans on and what a pixel with few samples borrows its radiance from.
    m_requirements = PassRequirements();
    GatherRequirements(m_requirements);
    m_requirements.fragmentShader                    = m_avgShader;
    m_requirements.vertexShader                      = m_avgPass->m_material->GetVertexShaderVal();
    m_requirements.program                           = m_avgPass->GetProgram();
    m_requirements.frameBuffer                       = m_avgPass->m_params.frameBuffer;
    m_requirements.semanticTextures["s_trace"]       = m_traceTexture;
    m_requirements.customUbos[7]                     = &m_passDataBuffer.GetBuffer();

    ApplyRequirements(renderer);
    RenderSubPass(m_avgPass);

    // Phase 3: spatial filter, reprojection and the variance clipped accumulation. The requirements are
    // rebuilt so the sampler names of the trace phase do not leak into this one.
    m_requirements = PassRequirements();
    GatherRequirements(m_requirements);
    m_requirements.fragmentShader                        = m_resolveShader;
    m_requirements.vertexShader                          = m_resolvePass->m_material->GetVertexShaderVal();
    m_requirements.program                               = m_resolvePass->GetProgram();
    m_requirements.frameBuffer                           = m_resolvePass->m_params.frameBuffer;
    m_requirements.semanticTextures["s_trace"]           = m_traceTexture;
    m_requirements.semanticTextures["s_avgRadiance"]     = m_avgRadiance;
    m_requirements.semanticTextures["s_history"]         = m_history[readIndex];
    m_requirements.semanticTextures["s_historyCount"]    = m_historyCount[readIndex];
    m_requirements.semanticTextures["s_normalDepth"]     = normalDepth;
    m_requirements.semanticTextures["s_normalDepthPrev"] = m_prevNormalDepth;
    m_requirements.semanticTextures["s_hit"]             = m_hitTexture[m_hitIndex];

    if (m_params.VelocityCurrent != nullptr && m_params.VelocityPrevious != nullptr)
    {
      m_requirements.semanticTextures["s_velCur"]  = m_params.VelocityCurrent;
      m_requirements.semanticTextures["s_velPrev"] = m_params.VelocityPrevious;
    }
    m_requirements.customUbos[7] = &m_passDataBuffer.GetBuffer();

    ApplyRequirements(renderer);
    RenderSubPass(m_resolvePass);

    // Phase 4: composite the accumulation over the scene copy into the pass's own target. The coverage of
    // the trace decides which pixels take it: a reflective pixel takes the reflection, everything else
    // keeps the forward shaded colour.
    m_requirements = PassRequirements();
    GatherRequirements(m_requirements);
    m_requirements.fragmentShader                     = m_compositeShader;
    m_requirements.vertexShader                       = m_compositePass->m_material->GetVertexShaderVal();
    m_requirements.program                            = m_compositePass->GetProgram();
    m_requirements.frameBuffer                        = m_compositePass->m_params.frameBuffer;
    m_requirements.semanticTextures["s_trace"]        = m_history[writeIndex];
    m_requirements.semanticTextures["s_sceneColor"]   = m_copyTexture;
    m_requirements.semanticTextures["s_coverage"]     = m_traceTexture;
    m_requirements.semanticTextures["s_normalDepth"]  = normalDepth;
    m_requirements.customUbos[7]                      = &m_passDataBuffer.GetBuffer();

    ApplyRequirements(renderer);
    RenderSubPass(m_compositePass);

    // The quad above wrote every pixel of the target, so it now holds this frame's picture and the passes
    // behind this one may read it.
    m_outputWritten = true;
    m_hasHistory    = true;
    m_historyIndex  = writeIndex;
    m_hitIndex      = 1 - m_hitIndex;

    // The g buffer this frame reprojected against becomes the one the next frame compares to. A copy
    // after the resolve, so the frame that was just drawn is the history and not the other way around.
    renderer->CopyTexture(normalDepth, m_prevNormalDepth);

    // The reprojection of the next frame needs the view projection this frame was drawn with, and the
    // comparison against it is what told the resolve (in PreRender) whether this frame is a moving one.
    m_prevViewProjection = m_params.Cam->GetProjectionMatrix() * m_params.Cam->GetViewMatrix();

    // The sample sequence rolls every frame, whether the view moves or not, so a still camera keeps
    // integrating the lobe it shows rather than freezing one sample of it. The seed walks a bounded range,
    // so the offset stays exact in a float and the sequence repeats (which is invisible: it is a sample
    // sequence, not a state).
    m_frameIndex = m_frameIndex >= 32.0f ? 0.0f : m_frameIndex + 1.0f;
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
