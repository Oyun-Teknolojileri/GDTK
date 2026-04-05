/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Renderer.h"

#include "AABBOverrideComponent.h"
#include "Camera.h"
#include "DirectionComponent.h"
#include "Drawable.h"
#include "EngineSettings.h"
#include "EnvironmentComponent.h"
#include "Framebuffer.h"
#include "GradientSky.h"
#include "Logger.h"
#include "Material.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "Node.h"
#include "Pass.h"
#include "RHI.h"
#include "RenderSystem.h"
#include "Scene.h"
#include "Shader.h"
#include "Skeleton.h"
#include "Stats.h"
#include "Surface.h"
#include "TKAssert.h"
#include "TKOpenGL.h"
#include "Texture.h"
#include "ToolKit.h"
#include "UIManager.h"
#include "Viewport.h"

#include "DebugNew.h"

namespace ToolKit
{

  namespace DefaultTextureSlots
  {
    int constexpr COLOR_TEXTURE_SLOT                         = 0;
    int constexpr EMISSIVE_TEXTURE_SLOT                      = 1;
    int constexpr BLEND_WEIGHT_TEXTURE_SLOT                  = 2;
    int constexpr SKINNING_TEXTURE_SLOT                      = 3;
    int constexpr METALLIC_ROUGHNESS_TEXTURE_SLOT            = 4;
    int constexpr AO_TEXTURE_SLOT                            = 5;
    int constexpr CUBEMAP_TEXTURE_SLOT                       = 6;
    int constexpr IRRADIANCE_MAP_TEXTURE_SLOT                = 7;
    int constexpr SHADOW_ATLAS_TEXTURE_SLOT                  = 8;
    int constexpr NORMAL_MAP_TEXTURE_SLOT                    = 9;
    int constexpr IBL_SPECULAR_PRE_FILTERED_MAP_TEXTURE_SLOT = 15;
    int constexpr IBL_BRDF_LUT_TEXTURE_SLOT                  = 10;
    int constexpr SECONDARY_IRRADIANCE_MAP_TEXTURE_SLOT      = 11;
    int constexpr SECONDARY_IBL_SPECULAR_MAP_TEXTURE_SLOT    = 12;
  } // namespace DefaultTextureSlots

  Renderer::Renderer()
  {
    m_textureSlots.fill(-1);
    m_activePointLightIndices.fill(-1);
    m_activeSpotLightIndices.fill(-1);

    // Get global buffers.
    m_globalGpuBuffers = Main::GetInstance()->m_gpuBuffers;
  }

  void Renderer::BeginRenderFrame()
  {
    m_globalGpuBuffers->graphicConstantBuffer.Map();
    m_drawnFrameBufferStats.clear();
  }

  void Renderer::EndRenderFrame()
  {
    SetAmbientOcclusionTexture(nullptr);

    for (const auto& pair : m_drawnFrameBufferStats)
    {
      if (pair.second > 0)
      {
        Stats::IncrementStat(FrameStatType::RenderPass);
      }
    }
  }

  void Renderer::InvalidateGraphicsConstants()
  {
    const ShadowSettingsPtr shadows                   = GetEngineSettings().m_graphics->m_shadows;

    GraphicConstantsGpuBuffer& graphicConstantsBuffer = m_globalGpuBuffers->graphicConstantBuffer;
    graphicConstantsBuffer.m_data.shadowDistance      = shadows->GetShadowMaxDistance();
    graphicConstantsBuffer.m_data.cascadeCount        = shadows->GetCascadeCountVal();
    graphicConstantsBuffer.m_data.shadowAtlasSize     = (float) shadows->GetShadowAtlasResolution();
    graphicConstantsBuffer.m_data.iblMaxReflectionLod = RHIConstants::SpecularIBLLods;
    graphicConstantsBuffer.m_data.cascadeDistances    = *((Vec4*) &shadows->GetCascadeDistancesVal());
    graphicConstantsBuffer.Invalidate();
  }

  void Renderer::Init()
  {
    m_uiCamera                      = MakeNewPtr<Camera>();
    m_oneColorAttachmentFramebuffer = MakeNewPtr<Framebuffer>("RendererOneColorFB");
    m_copyFrameBuffer               = MakeNewPtr<Framebuffer>("RendererCopyFB");
    m_dummyDrawCube                 = MakeNewPtr<Cube>();

    m_gpuProgramManager             = GetGpuProgramManager();

    glGenQueries(1, &m_gpuTimerQuery);

    const char* renderer = (const char*) glGetString(GL_RENDERER);
    GetLogger()->Log(String("Graphics Card ") + renderer);

    // Default states.
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    // Validate sRGB automatic encoding on backbuffer if enabled.
    ValidateBackbufferSrgbEncoding();

    SrgbAutoEncoding(GetRenderSystem()->m_backbufferFormatIsSRGB);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  }

  Renderer::~Renderer()
  {
    m_oneColorAttachmentFramebuffer = nullptr;
    m_gaussianBlurMaterial          = nullptr;
    m_averageBlurMaterial           = nullptr;
    m_copyFrameBuffer               = nullptr;
    m_copyMaterial                  = nullptr;

    m_framebuffer                   = nullptr;
    m_shadowAtlas                   = nullptr;
  }

  void Renderer::SrgbAutoEncoding(bool enable)
  {
#ifdef GL_FRAMEBUFFER_SRGB
    const int glSrgbFlag = GL_FRAMEBUFFER_SRGB;
#elif defined(GL_FRAMEBUFFER_SRGB_EXT)
    const int glSrgbFlag = GL_FRAMEBUFFER_SRGB_EXT;
#else
    const int glSrgbFlag = 0;
#endif

    if constexpr (glSrgbFlag)
    {
      if (enable)
      {
        glEnable(glSrgbFlag);
      }
      else
      {
        glDisable(glSrgbFlag);
      }
    }
  }

  int Renderer::GetMaxArrayTextureLayers()
  {
    if (m_maxArrayTextureLayers == -1)
    {
      glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &m_maxArrayTextureLayers);
    }
    return m_maxArrayTextureLayers;
  }

  void Renderer::SetCamera(CameraPtr camera, bool setLens)
  {
    TK_PROFILE_FUNCTION();

    if (setLens)
    {
      if (camera->IsOrtographic())
      {
        float width     = m_viewportSize.x * 0.5f;
        float height    = m_viewportSize.y * 0.5f;

        float camWidth  = camera->Right();
        float camHeight = camera->Top();

        if (glm::notEqual(camWidth, width) || glm::notEqual(camHeight, height))
        {
          camera->SetLens(-width, width, -height, height, camera->Near(), camera->Far());
        }
      }
      else
      {
        float aspect    = (float) m_viewportSize.x / (float) m_viewportSize.y;
        float camAspect = camera->Aspect();
        if (glm::notEqual(aspect, camAspect))
        {
          camera->SetLens(camera->Fov(), aspect, camera->Near(), camera->Far());
        }
      }
    }

    const CameraCacheItem& cameraCacheItem  = camera->GetCacheItem();

    // Check if buffer update is needed.
    bool updateGpuBuffer                    = cameraCacheItem.id != m_cameraCacheItem.id;
    updateGpuBuffer                        |= cameraCacheItem.version != m_cameraCacheItem.version;

    if (updateGpuBuffer)
    {
      m_cameraCacheItem                = cameraCacheItem;

      // Update gpu buffer.
      CameraGpuBuffer& cameraGpuBuffer = m_globalGpuBuffers->cameraGpuBuffer;
      cameraGpuBuffer.m_data           = m_cameraCacheItem.data;
      cameraGpuBuffer.Invalidate();
      cameraGpuBuffer.Map();

      Stats::IncrementStat(FrameStatType::CameraUpdate);
    }
  }

  void Renderer::Render(const RenderJob& job)
  {
    TK_PROFILE_FUNCTION();

    // Skeleton Component is used by all meshes of an entity.
    const auto& updateAndBindSkinningTextures = [&]()
    {
      if (!job.Mesh->IsSkinned())
      {
        return;
      }

      const SkeletonPtr& skel = static_cast<SkinMesh*>(job.Mesh)->m_skeleton;
      if (skel == nullptr)
      {
        return;
      }

      if (job.animData.currentAnimation != nullptr)
      {
        // animation.
        AnimationPlayer* animPlayer = GetAnimationPlayer();
        DataTexturePtr animTexture =
            animPlayer->GetAnimationDataTexture(skel->GetIdVal(), job.animData.currentAnimation->GetIdVal());

        if (animTexture != nullptr)
        {
          SetTexture(DefaultTextureSlots::SKINNING_TEXTURE_SLOT, animTexture);
        }

        // animation to blend.
        if (job.animData.blendAnimation != nullptr)
        {
          animTexture = animPlayer->GetAnimationDataTexture(skel->GetIdVal(), job.animData.blendAnimation->GetIdVal());
          SetTexture(DefaultTextureSlots::BLEND_WEIGHT_TEXTURE_SLOT, animTexture);
        }
      }
      else
      {
        SetTexture(DefaultTextureSlots::SKINNING_TEXTURE_SLOT, skel->m_bindPoseTexture);
      }
    };

    updateAndBindSkinningTextures();

    // Make sure render data is initialized.
    job.Mesh->Init();
    job.Material->Init();

    // Set render data.
    SetTransforms(job.WorldTransform);
    SetMaterial(job.Material);
    SetDataTextures(job);
    SetLights(job.lights);

    m_model                  = job.WorldTransform;

    // Set state.
    RenderState* renderState = job.Material->GetRenderState();
    SetRenderState(renderState, job.requireCullFlip);

    auto activateSkinning = [&](const Mesh* mesh)
    {
      GLint skinParamsLoc = m_currentProgram->GetDefaultUniformLocation(Uniform::SKIN_PARAMS);
      if (skinParamsLoc == -1)
      {
        return;
      }

      bool isSkinned = mesh->IsSkinned();
      if (isSkinned)
      {
        SkeletonPtr skel = static_cast<SkinMesh*>(job.Mesh)->m_skeleton;
        assert(skel != nullptr);

        float boneCount  = (float) skel->m_bones.size();
        float isAnimated = (job.animData.currentAnimation != nullptr) ? 1.0f : 0.0f;
        float hasBlend   = (job.animData.blendAnimation != nullptr) ? 1.0f : 0.0f;
        glUniform4f(skinParamsLoc, boneCount, 1.0f, isAnimated, hasBlend);
      }
      else
      {
        glUniform4f(skinParamsLoc, 0.0f, 0.0f, 0.0f, 0.0f);
      }
    };

    const Mesh* mesh = job.Mesh;
    activateSkinning(mesh);

    FeedAnimationUniforms(m_currentProgram, job);
    FeedUniforms(m_currentProgram, job);

    RHI::BindVertexArray(mesh->m_vaoId);

    if (mesh->m_indexCount != 0)
    {
      glDrawElements((GLenum) renderState->drawType, mesh->m_indexCount, GL_UNSIGNED_INT, nullptr);
    }
    else
    {
      glDrawArrays((GLenum) renderState->drawType, 0, mesh->m_vertexCount);
    }

    if (m_framebuffer)
    {
      int& drawCount = m_drawnFrameBufferStats[m_framebuffer->GetFboId()];
      drawCount++;
    }

    Stats::IncrementStat(FrameStatType::DrawCall);
  }

  void Renderer::RenderWithProgramFromMaterial(const RenderJobArray& jobs)
  {
    TK_PROFILE_FUNCTION();

    for (int i = 0; i < jobs.size(); ++i)
    {
      RenderWithProgramFromMaterial(jobs[i]);
    }
  }

  void Renderer::RenderWithProgramFromMaterial(const RenderJob& job)
  {
    TK_PROFILE_FUNCTION();

    job.Material->Init();
    ShaderPtr vert        = job.Material->GetVertexShaderVal();
    ShaderPtr frag        = job.Material->GetFragmentShaderVal();

    GpuProgramPtr program = m_gpuProgramManager->CreateProgram(vert, frag);
    BindProgram(program);
    Render(job);
  }

  void Renderer::Render(const RenderJobArray& jobs)
  {
    TK_PROFILE_FUNCTION();

    for (const RenderJob& job : jobs)
    {
      Render(job);
    }
  }

  void Renderer::SetRenderState(const RenderState* const state, bool cullFlip)
  {
    CullingType targetMode = state->cullMode;
    if (cullFlip)
    {
      switch (state->cullMode)
      {
        case CullingType::Front:
          targetMode = CullingType::Back;
          break;
        case CullingType::Back:
          targetMode = CullingType::Front;
          break;
      }
    }

    if (m_renderState.cullMode != targetMode)
    {
      if (targetMode == CullingType::TwoSided)
      {
        glDisable(GL_CULL_FACE);
      }

      if (targetMode == CullingType::Front)
      {
        if (m_renderState.cullMode == CullingType::TwoSided)
        {
          glEnable(GL_CULL_FACE);
        }

        glCullFace(GL_FRONT);
      }

      if (targetMode == CullingType::Back)
      {
        if (m_renderState.cullMode == CullingType::TwoSided)
        {
          glEnable(GL_CULL_FACE);
        }

        glCullFace(GL_BACK);
      }

      m_renderState.cullMode = targetMode;
    }

    if (m_renderState.blendFunction != state->blendFunction)
    {
      // Only update blend state, if blend state is not overridden.
      if (!m_blendStateOverrideEnable)
      {
        switch (state->blendFunction)
        {
          case BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA:
          {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          }
          break;
          case BlendFunction::ONE_TO_ONE:
          {
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_FUNC_ADD);
          }
          break;
          default:
          {
            glDisable(GL_BLEND);
          }
          break;
        }

        m_renderState.blendFunction = state->blendFunction;
      }
    }

    m_renderState.alphaMaskTreshold = state->alphaMaskTreshold;

    if (m_renderState.lineWidth != state->lineWidth)
    {
      m_renderState.lineWidth = state->lineWidth;
      // glLineWidth(m_renderState.lineWidth);
    }
  }

  void Renderer::SetStencilOperation(StencilOperation op)
  {
    switch (op)
    {
      case StencilOperation::None:
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0x00);
        break;
      case StencilOperation::AllowAllPixels:
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glStencilFunc(GL_ALWAYS, 0xFF, 0xFF);
        break;
      case StencilOperation::AllowPixelsPassingStencil:
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_EQUAL, 0xFF, 0xFF);
        glStencilMask(0x00);
        break;
      case StencilOperation::AllowPixelsFailingStencil:
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_NOTEQUAL, 0xFF, 0xFF);
        glStencilMask(0x00);
        break;
    }
  }

  void Renderer::SetFramebuffer(FramebufferPtr frameBuffer,
                                GraphicBitFields attachmentsToClear,
                                const Vec4& clearColor,
                                GraphicFramebufferTypes frameBufferType)
  {
    TK_PROFILE_FUNCTION();

    if (frameBuffer != nullptr)
    {
      RHI::SetFramebuffer((GLenum) frameBufferType, frameBuffer->GetFboId());
      if (m_framebuffer != frameBuffer)
      {
        frameBuffer->SetDrawBuffers();
      }

      const FramebufferSettings& fbSet = frameBuffer->GetSettings();
      SetViewportSize(fbSet.width, fbSet.height);
    }
    else
    {
      // Backbuffer
      RHI::SetFramebuffer((GLenum) frameBufferType, 0);
      SetViewportSize(m_windowSize.x, m_windowSize.y);
    }

    if (attachmentsToClear != GraphicBitFields::None)
    {
      ClearBuffer(attachmentsToClear, clearColor);
    }

    m_framebuffer = frameBuffer;
  }

  void Renderer::StartTimerQuery()
  {
    m_cpuTime = GetElapsedMilliSeconds();
#ifdef GL_TIME_ELAPSED_EXT
    if constexpr (TK_PLATFORM == PLATFORM::TKWindows)
    {
      // Only start a new query if the previous one has been read
      if (!m_timerQueryActive && !m_timerQueryWaiting)
      {
        glBeginQuery(GL_TIME_ELAPSED_EXT, m_gpuTimerQuery);
        m_timerQueryActive = true;
      }
    }
#endif
  }

  void Renderer::EndTimerQuery()
  {
    float cpuTime = GetElapsedMilliSeconds();
    m_cpuTime     = cpuTime - m_cpuTime;

#ifdef GL_TIME_ELAPSED_EXT
    if constexpr (TK_PLATFORM == PLATFORM::TKWindows)
    {
      if (m_timerQueryActive)
      {
        glEndQuery(GL_TIME_ELAPSED_EXT);
        m_timerQueryActive  = false;
        m_timerQueryWaiting = true;
      }

      if (m_timerQueryWaiting)
      {
        GLuint available = 0;
        glGetQueryObjectuiv(m_gpuTimerQuery, GL_QUERY_RESULT_AVAILABLE, &available);

        if (available)
        {
          GLuint elapsedTime;
          glGetQueryObjectuiv(m_gpuTimerQuery, GL_QUERY_RESULT, &elapsedTime);

          m_gpuTime           = glm::max(1.0f, (float) (elapsedTime) / 1000000.0f);
          m_timerQueryWaiting = false; // Query ready for next frame.
        }
      }
    }
#endif
  }

  void Renderer::GetElapsedTime(float& cpu, float& gpu)
  {
    cpu = m_cpuTime;
    gpu = m_gpuTime;
  }

  FramebufferPtr Renderer::GetFrameBuffer() { return m_framebuffer; }

  void Renderer::ClearColorBuffer(const Vec4& color)
  {
    glClearColor(color.x, color.y, color.z, color.w);
    glClear((GLbitfield) GraphicBitFields::ColorBits);
  }

  void Renderer::ClearBuffer(GraphicBitFields fields, const Vec4& value)
  {
    glClearColor(value.x, value.y, value.z, value.w);
    glClear((GLbitfield) fields);
  }

  void Renderer::ColorMask(bool r, bool g, bool b, bool a) { glColorMask(r, g, b, a); }

  void Renderer::CopyFrameBuffer(FramebufferPtr src, FramebufferPtr dest, GraphicBitFields fields)
  {
    TK_PROFILE_FUNCTION();

    FramebufferPtr lastFb = m_framebuffer;

    uint width            = m_windowSize.x;
    uint height           = m_windowSize.y;

    uint srcId            = 0;
    if (src)
    {
      const FramebufferSettings& fbs = src->GetSettings();
      width                          = fbs.width;
      height                         = fbs.height;
      srcId                          = src->GetFboId();
    }

    RHI::SetFramebuffer(GL_READ_FRAMEBUFFER, srcId);

    uint destId = 0;
    if (dest)
    {
      dest->ReconstructIfNeeded(width, height);
      destId = dest->GetFboId();
    }
    RHI::SetFramebuffer(GL_DRAW_FRAMEBUFFER, destId);

    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, (GLbitfield) fields, GL_NEAREST);

    SetFramebuffer(lastFb, GraphicBitFields::None);
  }

  void Renderer::InvalidateFramebuffer(GraphicBitFields bits, FramebufferPtr frameBuffer)
  {
    GLenum attachments[3];
    int count = 0;

    if (RenderTargetPtr colorAttachment = frameBuffer->GetColorAttachment(Framebuffer::Attachment::ColorAttachment0))
    {
      if ((int) bits & (int) GraphicBitFields::ColorBits)
      {
        attachments[count++] = GL_COLOR_ATTACHMENT0;
      }
    }

    if (DepthTexturePtr depthTexture = frameBuffer->GetDepthTexture())
    {
      if ((int) bits & (int) GraphicBitFields::DepthBits)
      {
        bool hasStencil      = (int) bits & (int) GraphicBitFields::StencilBits;
        hasStencil           = hasStencil && depthTexture->m_stencil;
        attachments[count++] = hasStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
      }
      else if ((int) bits & (int) GraphicBitFields::StencilBits)
      {
        if (depthTexture->m_stencil)
        {
          attachments[count++] = GL_STENCIL_ATTACHMENT;
        }
      }
    }

    if (count == 0)
    {
      return;
    }

#ifdef TK_GL_ES_3_0
    RHI::SetFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->GetFboId());
    glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, count, attachments);
#else
    if (glInvalidateFramebufferEXT != nullptr)
    {
      RHI::SetFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->GetFboId());
      glInvalidateFramebufferEXT(GL_DRAW_FRAMEBUFFER, count, attachments);
    }
#endif
  }

  void Renderer::ResolveFramebuffer(FramebufferPtr source, FramebufferPtr target, const IntArray& attachments)
  {
    TK_PROFILE_FUNCTION();

    assert(source->Initialized() && "Source framebuffer is not initialized.");
    assert(target->Initialized() && "Target framebuffer is not initialized.");

    const int srcWidth  = source->GetSettings().width;
    const int srcHeight = source->GetSettings().height;
    const int dstWidth  = target->GetSettings().width;
    const int dstHeight = target->GetSettings().height;

    for (int atc : attachments)
    {
      // Sanity check.
      using Attachment      = Framebuffer::Attachment;
      Attachment atcEnum    = (Attachment) ((int) Attachment::ColorAttachment0 + atc);

      RenderTargetPtr srcRt = source->GetColorAttachment(atcEnum);
      assert(srcRt && "Trying to resolve a non existing attachment.");

      RenderTargetPtr targetRt = target->GetColorAttachment(atcEnum);
      if (targetRt == nullptr)
      {
        TextureSettings settings = srcRt->Settings();
        settings.msaaCount       = MsaaSampleCount::x0;
        targetRt                 = MakeNewPtr<RenderTarget>();
        targetRt->ReconstructIfNeeded(srcRt->m_width, srcRt->m_height, &settings);
        target->SetColorAttachment(atcEnum, targetRt);
      }

      srcRt->m_resolvedTexture = targetRt;

      // Bind read/draw after SetColorAttachment, which may have changed the
      // framebuffer bindings via RHI::SetFramebuffer(GL_FRAMEBUFFER, ...).
      RHI::SetFramebuffer(GL_READ_FRAMEBUFFER, source->GetFboId());
      RHI::SetFramebuffer(GL_DRAW_FRAMEBUFFER, target->GetFboId());

      GLenum attachment = GL_COLOR_ATTACHMENT0 + atc;

      // Read from the specific source attachment.
      glReadBuffer(attachment);

      // Write only to the corresponding target attachment.
      glDrawBuffers(1, &attachment);

      glBlitFramebuffer(0, 0, srcWidth, srcHeight, 0, 0, dstWidth, dstHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    // Restore target framebuffer's original draw buffer configuration.
    target->SetDrawBuffers();
  }

  void Renderer::SetViewport(Viewport* viewport) { SetFramebuffer(viewport->m_framebuffer, GraphicBitFields::AllBits); }

  void Renderer::SetViewportSize(uint width, uint height)
  {
    if (width == m_viewportSize.x && height == m_viewportSize.y)
    {
      return;
    }

    m_viewportSize.x = width;
    m_viewportSize.y = height;
    glViewport(0, 0, width, height);
  }

  void Renderer::SetViewportSize(uint x, uint y, uint width, uint height)
  {
    m_viewportSize.x = width;
    m_viewportSize.y = height;
    glViewport(x, y, width, height);
  }

  void Renderer::DrawFullQuad(ShaderPtr fragmentShader)
  {
    TK_PROFILE_FUNCTION();

    if (m_tempQuadMaterial == nullptr)
    {
      m_tempQuadMaterial = MakeNewPtr<Material>();
    }
    m_tempQuadMaterial->UnInit();

    ShaderPtr fullQuadVert = GetShaderManager()->Create<Shader>(ShaderPath("fullQuadVert.shader", true));
    m_tempQuadMaterial->SetVertexShaderVal(fullQuadVert);
    m_tempQuadMaterial->SetFragmentShaderVal(fragmentShader);
    m_tempQuadMaterial->Init();

    DrawFullQuad(m_tempQuadMaterial);
  }

  void Renderer::DrawFullQuad(MaterialPtr mat)
  {
    TK_PROFILE_FUNCTION();

    if (m_tempQuad == nullptr)
    {
      m_tempQuad = MakeNewPtr<Quad>();
    }
    m_tempQuad->GetMeshComponent()->GetMeshVal()->m_material = mat;

    RenderJobArray jobs;
    RenderJobProcessor::CreateRenderJobs(jobs, m_tempQuad);

    bool prevDepthWriteState = m_renderState.depthWriteEnabled;
    EnableDepthWrite(false);
    SetDepthTestFunc(CompareFunctions::FuncAlways);
    RenderWithProgramFromMaterial(jobs);

    SetDepthTestFunc(CompareFunctions::FuncLess);
    EnableDepthWrite(prevDepthWriteState);
  }

  void Renderer::DrawCube(CameraPtr cam, MaterialPtr mat, const Mat4& transform)
  {
    TK_PROFILE_FUNCTION();

    m_dummyDrawCube->m_node->SetTransform(transform);
    m_dummyDrawCube->GetMaterialComponent()->SetFirstMaterial(mat);
    SetCamera(cam, true);

    RenderJobArray jobs;
    RenderJobProcessor::CreateRenderJobs(jobs, m_dummyDrawCube);

    SetDepthTestFunc(CompareFunctions::FuncAlways);
    RenderWithProgramFromMaterial(jobs);

    SetDepthTestFunc(CompareFunctions::FuncLess);
  }

  void Renderer::CopyTexture(TexturePtr src, TexturePtr dst)
  {
    TK_PROFILE_FUNCTION();

    Stats::BeginGpuScope("CopyTexture");

    assert(src->m_initiated && dst->m_initiated && "Texture is not initialized.");
    assert(src->m_width == dst->m_width && src->m_height == dst->m_height && "Sizes of the textures are not the same.");

    FramebufferSettings copyBuffer = {src->m_width, src->m_height, false, false, dst->Settings().msaaCount};
    m_copyFrameBuffer->ReconstructIfNeeded(copyBuffer);

    RenderTargetPtr rt = Cast<RenderTarget>(dst);
    m_copyFrameBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, rt);
    SetFramebuffer(m_copyFrameBuffer, GraphicBitFields::None);

    // Render to texture
    if (m_copyMaterial == nullptr)
    {
      m_copyMaterial = MakeNewPtr<Material>();
      ShaderPtr frag = GetShaderManager()->Create<Shader>(ShaderPath("copyTextureFrag.shader", true));
      ShaderPtr vert = GetShaderManager()->Create<Shader>(ShaderPath("copyTextureVert.shader", true));
      m_copyMaterial->SetVertexShaderVal(vert);
      m_copyMaterial->SetFragmentShaderVal(frag);
    }

    m_copyMaterial->SetDiffuseTextureVal(src);
    m_copyMaterial->Init();

    DrawFullQuad(m_copyMaterial);

    Stats::EndGpuScope();
  }

  void Renderer::OverrideBlendState(bool enableOverride, BlendFunction func)
  {
    RenderState stateCpy       = m_renderState;
    stateCpy.blendFunction     = func;

    m_blendStateOverrideEnable = false;

    SetRenderState(&stateCpy);

    m_blendStateOverrideEnable = enableOverride;
  }

  void Renderer::EnableBlending(bool enable)
  {
    if (enable)
    {
      glEnable(GL_BLEND);
    }
    else
    {
      glDisable(GL_BLEND);
    }
  }

  void Renderer::EnableDepthWrite(bool enable)
  {
    if (m_renderState.depthWriteEnabled != enable)
    {
      m_renderState.depthWriteEnabled = enable;
      glDepthMask(enable);
    }
  }

  void Renderer::EnableDepthTest(bool enable)
  {
    if (m_renderState.depthTestEnabled != enable)
    {
      if (enable)
      {
        glEnable(GL_DEPTH_TEST);
      }
      else
      {
        glDisable(GL_DEPTH_TEST);
      }
      m_renderState.depthTestEnabled = enable;
    }
  }

  void Renderer::SetDepthTestFunc(CompareFunctions func)
  {
    if (m_renderState.depthFunction != func)
    {
      m_renderState.depthFunction = func;
      glDepthFunc((int) func);
    }
  }

  bool Renderer::EnableDepthClamp(bool enable)
  {
    if (TK_GL_EXT_depth_clamp)
    {
      if (enable)
      {
        glEnable(GL_DEPTH_CLAMP_EXT);
      }
      else
      {
        glDisable(GL_DEPTH_CLAMP_EXT);
      }

      return true;
    }

    return false;
  }

  void Renderer::ApplyGaussianBlur(const TexturePtr src, RenderTargetPtr dst, const Vec3& axis, const float amount)
  {
    TK_PROFILE_FUNCTION();

    m_oneColorAttachmentFramebuffer->ReconstructIfNeeded({dst->m_width, dst->m_height, false, false});

    if (m_gaussianBlurMaterial == nullptr)
    {
      ShaderPtr vert         = GetShaderManager()->Create<Shader>(ShaderPath("gausBlurVert.shader", true));
      ShaderPtr frag         = GetShaderManager()->Create<Shader>(ShaderPath("gausBlurFrag.shader", true));

      m_gaussianBlurMaterial = MakeNewPtr<Material>();
      m_gaussianBlurMaterial->SetVertexShaderVal(vert);
      m_gaussianBlurMaterial->SetFragmentShaderVal(frag);
      m_gaussianBlurMaterial->SetDiffuseTextureVal(nullptr);
      m_gaussianBlurMaterial->Init();
    }

    m_gaussianBlurMaterial->SetDiffuseTextureVal(src);
    m_gaussianBlurMaterial->UpdateProgramUniform("BlurScale", axis * amount);

    m_oneColorAttachmentFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, dst);

    SetFramebuffer(m_oneColorAttachmentFramebuffer, GraphicBitFields::None);
    DrawFullQuad(m_gaussianBlurMaterial);
  }

  void Renderer::ApplyGaussianBlurToArrayLayerSlot(RenderTargetPtr srcArray,
                                                   RenderTargetPtr tempRT,
                                                   FramebufferPtr framebuffer,
                                                   int layer,
                                                   int kernelSize,
                                                   int tapCount,
                                                   float amount,
                                                   const Vec2& slotCoord,
                                                   int slotSize)
  {
    TK_PROFILE_FUNCTION();

    int texSize = srcArray->m_width;

    // Create blur material if needed (uses shared shaders).
    if (m_gaussianBlurMaterial == nullptr)
    {
      ShaderPtr vert         = GetShaderManager()->Create<Shader>(ShaderPath("gausBlurVert.shader", true));
      ShaderPtr frag         = GetShaderManager()->Create<Shader>(ShaderPath("gausBlurFrag.shader", true));

      m_gaussianBlurMaterial = MakeNewPtr<Material>();
      m_gaussianBlurMaterial->SetVertexShaderVal(vert);
      m_gaussianBlurMaterial->SetFragmentShaderVal(frag);
      m_gaussianBlurMaterial->SetDiffuseTextureVal(nullptr);
      m_gaussianBlurMaterial->Init();
    }

    ShaderPtr frag   = m_gaussianBlurMaterial->GetFragmentShaderVal();
    ShaderPtr vert   = m_gaussianBlurMaterial->GetVertexShaderVal();

    String kernelStr = std::to_string(kernelSize);
    float blurAmount = amount / (float) texSize;

    // Compute normalized UV clamp bounds for this slot with half-pixel inset.
    // This matches the half-pixel trick in shadow.shader (beginCoord + halfPixel, endCoord - halfPixel)
    // to prevent bilinear filtering from sampling into adjacent slots.
    float invSize    = 1.0f / (float) texSize;
    float halfPixel  = invSize * 0.5f;
    Vec2 clampMin    = slotCoord * invSize + halfPixel;
    Vec2 clampMax    = (slotCoord + Vec2((float) slotSize)) * invSize - halfPixel;

    // Scissor rect in pixels.
    int sx           = (int) slotCoord.x;
    int sy           = (int) slotCoord.y;

    glEnable(GL_SCISSOR_TEST);

    for (int tap = 0; tap < tapCount; tap++)
    {
      // Horizontal pass: array texture layer -> temp 2D RT
      {
        vert->SetDefine("TextureArray", "1");
        frag->SetDefine("TextureArray", "1");
        frag->SetDefine("KernelSize", kernelStr);
        frag->SetDefine("BlurClampEnabled", "1");

        framebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, tempRT, 0, -1);
        SetFramebuffer(framebuffer, GraphicBitFields::None);
        SetViewportSize(0, 0, texSize, texSize);
        glScissor(sx, sy, slotSize, slotSize);

        m_gaussianBlurMaterial->UpdateProgramUniform("BlurScale", Vec3(blurAmount, 0.0f, 0.0f));
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurLayer", (float) layer);
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurClampMin", clampMin);
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurClampMax", clampMax);

        BindProgramOfMaterial(m_gaussianBlurMaterial.get());

        RHI::SetTexture(GL_TEXTURE_2D_ARRAY, srcArray->m_textureId, 1);

        DrawFullQuad(m_gaussianBlurMaterial);
      }

      // Vertical pass: temp 2D RT -> array texture layer
      {
        vert->SetDefine("TextureArray", "0");
        frag->SetDefine("TextureArray", "0");
        frag->SetDefine("KernelSize", kernelStr);
        frag->SetDefine("BlurClampEnabled", "1");

        framebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, srcArray, 0, layer);
        SetFramebuffer(framebuffer, GraphicBitFields::None);
        SetViewportSize(0, 0, texSize, texSize);
        glScissor(sx, sy, slotSize, slotSize);

        m_gaussianBlurMaterial->SetDiffuseTextureVal(tempRT);
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurScale", Vec3(0.0f, blurAmount, 0.0f));
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurClampMin", clampMin);
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurClampMax", clampMax);

        DrawFullQuad(m_gaussianBlurMaterial);
      }
    }

    glDisable(GL_SCISSOR_TEST);

    // Restore default define state.
    vert->SetDefine("TextureArray", "0");
    frag->SetDefine("TextureArray", "0");
    frag->SetDefine("BlurClampEnabled", "0");
  }

  void Renderer::GenerateBRDFLutTexture()
  {
    if (!GetTextureManager()->Exist(TKBrdfLutTexture))
    {
      FramebufferPtr prevFrameBuffer = GetFrameBuffer();

      TextureSettings set;
      set.InternalFormat = GraphicTypes::FormatRG16F;
      set.Format         = GraphicTypes::FormatRG;
      set.Type           = GraphicTypes::TypeFloat;
      set.MinFilter      = GraphicTypes::SampleLinear;
      set.MagFilter      = GraphicTypes::SampleLinear;
      set.WarpS = set.WarpT = GraphicTypes::UVClampToEdge;
      set.GenerateMipMap    = false;

      RenderTargetPtr brdfLut =
          MakeNewPtr<RenderTarget>(RHIConstants::BrdfLutTextureSize, RHIConstants::BrdfLutTextureSize, set);
      brdfLut->Init();

      FramebufferSettings fbSettings = {RHIConstants::BrdfLutTextureSize,
                                        RHIConstants::BrdfLutTextureSize,
                                        false,
                                        false};

      FramebufferPtr utilFramebuffer = MakeNewPtr<Framebuffer>(fbSettings, "RendererLUTFB");
      utilFramebuffer->Init();
      utilFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, brdfLut);

      MaterialPtr material = MakeNewPtr<Material>();
      ShaderPtr vert       = GetShaderManager()->Create<Shader>(ShaderPath("fullQuadVert.shader", true));
      ShaderPtr frag       = GetShaderManager()->Create<Shader>(ShaderPath("BRDFLutFrag.shader", true));

      material->SetVertexShaderVal(vert);
      material->SetFragmentShaderVal(frag);
      material->Init();

      SetFramebuffer(utilFramebuffer, GraphicBitFields::AllBits);

      DrawFullQuad(material);

      brdfLut->SetFile(TKBrdfLutTexture);
      GetTextureManager()->Manage(brdfLut);
      m_brdfLut = brdfLut;

      SetFramebuffer(prevFrameBuffer, GraphicBitFields::None);
    }
  }

  void Renderer::SetAmbientOcclusionTexture(TexturePtr aoTexture)
  {
    m_aoTexture              = aoTexture;
    m_ambientOcculusionInUse = (aoTexture != nullptr);
    m_drawCommand.SetAmbientOcclusionInUse(m_ambientOcculusionInUse);
  }

  void Renderer::SetMaterial(Material* mat)
  {
    TK_PROFILE_FUNCTION();

    const MaterialCacheItem& cache = mat->GetCacheItem();
    if (cache.DiffuseTextureInUse())
    {
      SetTexture(DefaultTextureSlots::COLOR_TEXTURE_SLOT, mat->GetDiffuseTextureVal());
    }

    if (cache.EmissiveTextureInUse())
    {
      SetTexture(DefaultTextureSlots::EMISSIVE_TEXTURE_SLOT, mat->GetEmissiveTextureVal());
    }

    if (cache.MetallicRoughnessTextureInUse())
    {
      SetTexture(DefaultTextureSlots::METALLIC_ROUGHNESS_TEXTURE_SLOT, mat->GetMetallicRoughnessTextureVal());
    }

    if (cache.NormalTextureInUse())
    {
      SetTexture(DefaultTextureSlots::NORMAL_MAP_TEXTURE_SLOT, mat->GetNormalTextureVal());
    }

    if (mat->IsPBR())
    {
      SetTexture(DefaultTextureSlots::IBL_BRDF_LUT_TEXTURE_SLOT, m_brdfLut);
    }
  }

  void Renderer::SetLights(const LightRawPtrArray& lights)
  {
    TK_PROFILE_FUNCTION();

    SpotLightCache& spotCache   = m_globalGpuBuffers->spotLightBuffer;
    PointLightCache& pointCache = m_globalGpuBuffers->pointLighBuffer;

    // Update directional light cache.
    IDArray activePoint, activeSpot;
    for (Light* light : lights)
    {
      if (light->GetLightType() == Light::Point)
      {
        PointLight* pl                   = static_cast<PointLight*>(light);
        const PointLightCacheItem& cache = pl->GetCacheItem();
        pointCache.AddOrUpdateItem(cache);
        activePoint.push_back(cache.id);
      }
      else if (light->GetLightType() == Light::Spot)
      {
        SpotLight* sl                   = static_cast<SpotLight*>(light);
        const SpotLightCacheItem& cache = sl->GetCacheItem();
        spotCache.AddOrUpdateItem(cache);
        activeSpot.push_back(cache.id);
      }
    }

    if (pointCache.Map())
    {
      Stats::IncrementStat(FrameStatType::LightCacheInvalidation);
    }

    if (spotCache.Map())
    {
      Stats::IncrementStat(FrameStatType::LightCacheInvalidation);
    }

    // Look up indexes from cache.
    auto updateData = [](int& updateCount, IntArray& cpuIndex, int* gpuIndex) -> void
    {
      int limit   = (int) cpuIndex.size();
      updateCount = limit;

      for (int i = 0; i < limit; i++)
      {
        gpuIndex[i] = cpuIndex[i];
      }
    };

    IntArray indexes = pointCache.LookUp(activePoint, RHIConstants::MaxPointLightPerObject);
    updateData(m_activePointLightCount, indexes, m_activePointLightIndices.data());
    m_drawCommand.SetActivePointLightCount(m_activePointLightCount);

    indexes = spotCache.LookUp(activeSpot, RHIConstants::MaxSpotLightPerObject);
    updateData(m_activeSpotLightCount, indexes, m_activeSpotLightIndices.data());
    m_drawCommand.SetActiveSpotLightCount(m_activeSpotLightCount);
  }

  void Renderer::BindProgramOfMaterial(Material* material)
  {
    material->Init();
    ShaderPtr frag        = material->GetFragmentShaderVal();
    ShaderPtr vert        = material->GetVertexShaderVal();

    GpuProgramPtr program = m_gpuProgramManager->CreateProgram(vert, frag);
    BindProgram(program);
  }

  void Renderer::BindProgram(const GpuProgramPtr& program)
  {
    TK_PROFILE_FUNCTION();

    if (m_currentProgram == nullptr || m_currentProgram->m_handle != program->m_handle)
    {
      m_currentProgram = program;
      glUseProgram(program->m_handle);
    }
  }

  void Renderer::ResetUsedTextureSlots()
  {
    for (int i = 0; i < RHIConstants::TextureSlotCount; i++)
    {
      SetTexture(i, nullptr);
    }
  }

  void Renderer::SetDirectionalLights(const LightRawPtrArray& lights)
  {
    m_globalGpuBuffers->directionalLightBuffer.Map(lights);
    m_drawCommand.SetActiveDirectionalLightCount((int) lights.size());
  }

  void Renderer::SetDataTextures(const RenderJob& job)
  {
    TK_PROFILE_FUNCTION();

    // Cube map data.
    Material* mat = job.Material;
    if (mat && mat->m_cubeMap)
    {
      SetTexture(DefaultTextureSlots::CUBEMAP_TEXTURE_SLOT, mat->m_cubeMap);
    }

    // Sky and Ibl data.
    m_drawCommand.SetIblInUse(false);
    m_drawCommand.SetSecondaryIblIntensity(0.0f);
    m_drawCommand.SetIblFadeDistance(0.0f);
    EnvironmentComponent* envCom = job.EnvironmentVolume;
    if (envCom)
    {
      const HdriPtr& hdriPtr     = envCom->GetHdriVal();
      CubeMapPtr& diffuseEnvMap  = hdriPtr->m_diffuseEnvMap;
      CubeMapPtr& specularEnvMap = hdriPtr->m_specularEnvMap;

      if (diffuseEnvMap && specularEnvMap && m_brdfLut)
      {
        SetTexture(DefaultTextureSlots::IRRADIANCE_MAP_TEXTURE_SLOT, diffuseEnvMap);
        SetTexture(DefaultTextureSlots::IBL_SPECULAR_PRE_FILTERED_MAP_TEXTURE_SLOT, specularEnvMap);
        SetTexture(DefaultTextureSlots::IBL_BRDF_LUT_TEXTURE_SLOT, m_brdfLut);

        m_drawCommand.SetIblInUse(true);
        m_drawCommand.SetIblIntensity(envCom->GetIntensityVal());

        // Sky: rotation applies to IBL image, no volume boundary.
        // Non-Sky: rotation applies to OBB volume, IBL image stays fixed.
        if (const EntityPtr& env = envCom->OwnerEntity())
        {
          if (env->IsA<SkyBase>())
          {
            m_iblRotation = Mat4(env->m_node->GetOrientation());
            m_drawCommand.SetIblVolumeTransform(Mat4(1.0f));
          }
          else
          {
            m_iblRotation       = Mat4(1.0f);
            Mat4 worldTransform = env->m_node->GetTransform(TransformationSpace::TS_WORLD);
            m_drawCommand.SetIblVolumeTransform(glm::inverse(worldTransform));
          }
        }

        // Secondary IBL for per-pixel blending.
        EnvironmentComponent* secEnvCom = job.SecondaryEnvironmentVolume;
        if (secEnvCom)
        {
          const HdriPtr& secHdri  = secEnvCom->GetHdriVal();
          CubeMapPtr& secDiffuse  = secHdri->m_diffuseEnvMap;
          CubeMapPtr& secSpecular = secHdri->m_specularEnvMap;

          if (secDiffuse && secSpecular)
          {
            SetTexture(DefaultTextureSlots::SECONDARY_IRRADIANCE_MAP_TEXTURE_SLOT, secDiffuse);
            SetTexture(DefaultTextureSlots::SECONDARY_IBL_SPECULAR_MAP_TEXTURE_SLOT, secSpecular);
            m_drawCommand.SetSecondaryIblIntensity(secEnvCom->GetIntensityVal());

            // Pass primary volume local-space BB for OBB per-pixel blend.
            Vec3 offset = envCom->GetPositionOffsetVal();
            Vec3 half   = envCom->GetSizeVal() * 0.5f;
            m_drawCommand.SetPrimaryVolumeMin(offset - half);
            m_drawCommand.SetPrimaryVolumeMax(offset + half);
            m_drawCommand.SetIblFadeDistance(glm::max(envCom->GetFadeVal(), 0.001f));

            if (const EntityPtr& secEnv = secEnvCom->OwnerEntity())
            {
              if (secEnv->IsA<SkyBase>())
              {
                m_secondaryIblRotation = Mat4(secEnv->m_node->GetOrientation());
              }
              else
              {
                m_secondaryIblRotation = Mat4(1.0f);
              }
            }
          }
        }
      }
    }

    // AO texture.
    if (m_ambientOcculusionInUse)
    {
      SetTexture(DefaultTextureSlots::AO_TEXTURE_SLOT, m_aoTexture);
    }

    // Bind shadow map if activated.
    if (m_shadowAtlas != nullptr)
    {
      SetTexture(DefaultTextureSlots::SHADOW_ATLAS_TEXTURE_SLOT, m_shadowAtlas);
    }
  }

  void Renderer::SetTransforms(const Mat4& model)
  {
    m_model                       = model;
    m_inverseModel                = glm::inverse(model);
    m_inverseTransposeModel       = glm::transpose(m_inverseModel);

    m_modelWithoutTranslate       = model;
    m_modelWithoutTranslate[0][3] = 0.0f;
    m_modelWithoutTranslate[1][3] = 0.0f;
    m_modelWithoutTranslate[2][3] = 0.0f;
    m_modelWithoutTranslate[3][3] = 1.0f;
    m_modelWithoutTranslate[3][0] = 0.0f;
    m_modelWithoutTranslate[3][1] = 0.0f;
    m_modelWithoutTranslate[3][2] = 0.0f;
  }

  void Renderer::FeedUniforms(const GpuProgramPtr& program, const RenderJob& job)
  {
    TK_PROFILE_FUNCTION();

    // Built-in shader uniforms.
    for (auto& uniform : program->m_defaultUniformLocation)
    {
      int loc = program->GetDefaultUniformLocation(uniform.first);
      if (loc != -1)
      {
        switch (uniform.first)
        {
          case Uniform::MODEL:
            glUniformMatrix4fv(loc, 1, false, reinterpret_cast<float*>(&m_model));
            break;
          case Uniform::MODEL_WITHOUT_TRANSLATE:
            glUniformMatrix4fv(loc, 1, false, reinterpret_cast<float*>(&m_modelWithoutTranslate));
            break;
          case Uniform::INVERSE_MODEL:
            glUniformMatrix4fv(loc, 1, false, reinterpret_cast<float*>(&m_inverseModel));
            break;
          case Uniform::INVERSE_TRANSPOSE_MODEL:
            glUniformMatrix4fv(loc, 1, false, reinterpret_cast<float*>(&m_inverseTransposeModel));
            break;
          case Uniform::IBL_ROTATION:
            glUniformMatrix4fv(loc, 1, false, reinterpret_cast<float*>(&m_iblRotation));
            break;
          case Uniform::IBL_SECONDARY_ROTATION:
            glUniformMatrix4fv(loc, 1, false, reinterpret_cast<float*>(&m_secondaryIblRotation));
            break;
          case Uniform::VIEWPORT_SIZE:
            glUniform2f(loc, (float) m_viewportSize.x, (float) m_viewportSize.y);
            break;
          default:
            break;
        }
      }
    }

    // Built-in array uniforms.
    for (auto& arrayUniform : program->m_defaultArrayUniformLocations)
    {
      switch (arrayUniform.first)
      {
        case Uniform::DRAW_COMMAND:
        {
          int loc = program->GetDefaultUniformLocation(Uniform::DRAW_COMMAND, 0);
          if (loc != -1)
          {
            glUniform4fv(loc, sizeof(DrawCommand) / sizeof(Vec4), (float*) &m_drawCommand);
          }
        }
        break;
        case Uniform::ACTIVE_POINT_LIGHT_INDEXES:
        {
          int loc = program->GetDefaultUniformLocation(Uniform::ACTIVE_POINT_LIGHT_INDEXES, 0);
          if (loc != -1)
          {
            if (m_activePointLightCount > 0)
            {
              glUniform1iv(loc, m_activePointLightCount, m_activePointLightIndices.data());
            }
          }
        }
        break;
        case Uniform::ACTIVE_SPOT_LIGHT_INDEXES:
        {
          int loc = program->GetDefaultUniformLocation(Uniform::ACTIVE_SPOT_LIGHT_INDEXES, 0);
          if (loc != -1)
          {
            if (m_activeSpotLightCount > 0)
            {
              glUniform1iv(loc, m_activeSpotLightCount, m_activeSpotLightIndices.data());
            }
          }
        }
        break;
        case Uniform::MATERIAL_CACHE:
        {
          int loc = program->GetDefaultUniformLocation(Uniform::MATERIAL_CACHE, 0);
          if (loc != -1)
          {
            // Material data.
            const MaterialCacheItem& cache = job.Material->GetCacheItem();
            if (cache.id == program->m_cachedMaterial.id)
            {
              if (cache.version == program->m_cachedMaterial.version)
              {
                // Material data is already set.
                break;
              }
            }

            program->m_cachedMaterial = cache;
            glUniform4fv(loc, sizeof(MaterialCacheItem::Data) / sizeof(Vec4), (float*) &cache.data);
          }
        }
        break;
        default:
          break;
      }
    }

    // Custom shader uniforms.
    for (auto& uniform : program->m_customUniforms)
    {
      GLint loc = program->GetCustomUniformLocation(uniform.second);
      switch (uniform.second.GetType())
      {
        case ShaderUniform::UniformType::Bool:
          glUniform1ui(loc, uniform.second.GetVal<bool>());
          break;
        case ShaderUniform::UniformType::Float:
          glUniform1f(loc, uniform.second.GetVal<float>());
          break;
        case ShaderUniform::UniformType::Int:
          glUniform1i(loc, uniform.second.GetVal<int>());
          break;
        case ShaderUniform::UniformType::UInt:
          glUniform1ui(loc, uniform.second.GetVal<uint>());
          break;
        case ShaderUniform::UniformType::Vec2:
          glUniform2fv(loc, 1, reinterpret_cast<float*>(&uniform.second.GetVal<Vec2>()));
          break;
        case ShaderUniform::UniformType::Vec3:
          glUniform3fv(loc, 1, reinterpret_cast<float*>(&uniform.second.GetVal<Vec3>()));
          break;
        case ShaderUniform::UniformType::Vec4:
          glUniform4fv(loc, 1, reinterpret_cast<float*>(&uniform.second.GetVal<Vec4>()));
          break;
        case ShaderUniform::UniformType::Mat3:
          glUniformMatrix3fv(loc, 1, false, reinterpret_cast<float*>(&uniform.second.GetVal<Mat3>()));
          break;
        case ShaderUniform::UniformType::Mat4:
          glUniformMatrix4fv(loc, 1, false, reinterpret_cast<float*>(&uniform.second.GetVal<Mat4>()));
          break;
        default:
          assert(false && "Invalid type.");
          break;
      }
    }
  }

  void Renderer::FeedAnimationUniforms(const GpuProgramPtr& program, const RenderJob& job)
  {
    TK_PROFILE_FUNCTION();

    if (job.animData.currentAnimation == nullptr)
    {
      return;
    }

    // Send keyFrameData: (kf1, kf2, interpTime, kfCount)
    int uniformLoc = program->GetDefaultUniformLocation(Uniform::KEY_FRAME_DATA);
    if (uniformLoc != -1)
    {
      glUniform4f(uniformLoc,
                  job.animData.firstKeyFrame,
                  job.animData.secondKeyFrame,
                  job.animData.keyFrameInterpolationTime,
                  job.animData.keyFrameCount);
    }

    // Send blend data.
    if (job.animData.blendAnimation != nullptr)
    {
      uniformLoc = program->GetDefaultUniformLocation(Uniform::BLEND_FACTOR);
      if (uniformLoc != -1)
      {
        glUniform1f(uniformLoc, job.animData.animationBlendFactor);
      }

      // Send blendFrameData: (blendKf1, blendKf2, blendInterpTime, blendKfCount)
      uniformLoc = program->GetDefaultUniformLocation(Uniform::BLEND_FRAME_DATA);
      if (uniformLoc != -1)
      {
        glUniform4f(uniformLoc,
                    job.animData.blendFirstKeyFrame,
                    job.animData.blendSecondKeyFrame,
                    job.animData.blendKeyFrameInterpolationTime,
                    job.animData.blendKeyFrameCount);
      }
    }
  }

  void Renderer::SetTexture(ubyte slotIndx, TexturePtr texture)
  {
    assert(slotIndx < RHIConstants::TextureSlotCount && "You exceed texture slot count");

    if (texture != nullptr)
    {
      RHI::SetTexture((GLenum) texture->Settings().Target, texture->m_textureId, slotIndx);
    }
    else
    {
      RHI::SetTexture(GL_TEXTURE_2D, 0, slotIndx);
    }
  }

  void Renderer::SetShadowAtlas(TexturePtr shadowAtlas) { m_shadowAtlas = shadowAtlas; }

  CubeMapPtr Renderer::GenerateCubemapFrom2DTexture(TexturePtr texture,
                                                    uint size,
                                                    float exposure,
                                                    GraphicTypes minFilter)
  {
    const TextureSettings set = {GraphicTypes::TargetCubeMap,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::UVClampToEdge,
                                 minFilter,
                                 GraphicTypes::SampleLinear,
                                 GraphicTypes::FormatRGBA16F,
                                 GraphicTypes::FormatRGBA,
                                 GraphicTypes::TypeFloat,
                                 MsaaSampleCount::x0,
                                 0,
                                 false};

    RenderTargetPtr cubeMapRt = MakeNewPtr<RenderTarget>(size, size, set, "EquirectToCubeMapRT");
    cubeMapRt->Init();

    // Create material
    MaterialPtr mat = MakeNewPtr<Material>();
    ShaderPtr vert  = GetShaderManager()->Create<Shader>(ShaderPath("equirectToCubeVert.shader", true));
    ShaderPtr frag  = GetShaderManager()->Create<Shader>(ShaderPath("equirectToCubeFrag.shader", true));

    mat->SetDiffuseTextureVal(texture);
    mat->SetVertexShaderVal(vert);
    mat->SetFragmentShaderVal(frag);
    mat->GetRenderState()->cullMode = CullingType::TwoSided;
    mat->Init();

    mat->UpdateProgramUniform("Exposure", exposure);

    m_oneColorAttachmentFramebuffer->ReconstructIfNeeded({(int) size, (int) size, false, false});

    // Views for 6 different angles
    CameraPtr cam = MakeNewPtr<Camera>();
    cam->SetLens(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    Mat4 views[] = {glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};

    for (int i = 0; i < 6; i++)
    {
      Vec3 pos, sca;
      Quaternion rot;

      DecomposeMatrix(views[i], &pos, &rot, &sca);

      cam->m_node->SetTranslation(ZERO, TransformationSpace::TS_WORLD);
      cam->m_node->SetOrientation(rot, TransformationSpace::TS_WORLD);
      cam->m_node->SetScale(sca);

      m_oneColorAttachmentFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                          cubeMapRt,
                                                          0,
                                                          -1,
                                                          (Framebuffer::CubemapFace) i);

      SetFramebuffer(m_oneColorAttachmentFramebuffer, GraphicBitFields::None);
      DrawCube(cam, mat);
    }

    CubeMapPtr cubeMap = MakeNewPtr<CubeMap>();
    cubeMap->Consume(cubeMapRt);

    return cubeMap;
  }

  TexturePtr Renderer::GenerateEquiRectengularProjection(CubeMapPtr cubemap, int level, float exposure, void** pixels)
  {
    UVec2 rectSize = cubemap->GetEquiRectengularMapSize();
    int mipWidth   = rectSize.x >> level;
    int mipHeight  = rectSize.y >> level;

    // Enlarge the cube map to a single texture.
    RenderTargetPtr euqiRectTexture =
        MakeNewPtr<RenderTarget>(mipWidth, mipHeight, TextureSettings(), "CubemapToEquiRectRT");
    euqiRectTexture->Init();

    // Store current frame buffer.
    FramebufferPtr prevBuffer = GetFrameBuffer();
    m_oneColorAttachmentFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, euqiRectTexture);
    SetFramebuffer(m_oneColorAttachmentFramebuffer, GraphicBitFields::AllBits);

    ShaderManager* shaderMan   = GetShaderManager();
    MaterialPtr cubeToEquiRect = MakeNewPtr<Material>();
    ShaderPtr frag             = shaderMan->Create<Shader>(ShaderPath("cubemapToEquirectFrag.shader", true));
    ShaderPtr vert             = shaderMan->Create<Shader>(ShaderPath("fullQuadVert.shader", true));
    cubeToEquiRect->SetFragmentShaderVal(frag);
    cubeToEquiRect->SetVertexShaderVal(vert);
    cubeToEquiRect->m_cubeMap = cubemap;
    cubeToEquiRect->Init();

    cubeToEquiRect->UpdateProgramUniform("lodLevel", level);
    cubeToEquiRect->UpdateProgramUniform("Exposure", exposure);

    DrawFullQuad(cubeToEquiRect);

    if (pixels != nullptr)
    {
      uint64 requiredSize = mipWidth * mipHeight * 4 * sizeof(float);
      *pixels             = new float[requiredSize];
      glReadPixels(0, 0, mipWidth, mipHeight, GL_RGBA, GL_FLOAT, *pixels);
    }

    SetFramebuffer(prevBuffer, GraphicBitFields::None);

    return euqiRectTexture;
  }

  void Renderer::CopyCubeMapToMipLevel(CubeMapPtr src, CubeMapPtr dst, int mipLevel)
  {
    FramebufferSettings fbs;
    fbs.width                  = dst->m_width;
    fbs.height                 = dst->m_height;
    fbs.useDefaultDepth        = false;

    FramebufferPtr writeBuffer = MakeNewPtr<Framebuffer>(fbs);
    writeBuffer->Init();

    fbs.width                 = src->m_width;
    fbs.height                = src->m_height;

    FramebufferPtr readBuffer = MakeNewPtr<Framebuffer>(fbs);
    readBuffer->Init();

    RHI::SetTexture((GLenum) dst->Settings().Target, dst->m_textureId);

    for (int i = 0; i < 6; i++)
    {
      writeBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                      dst->m_consumedRT,
                                      mipLevel,
                                      -1,
                                      Framebuffer::CubemapFace(i));

      readBuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                     src->m_consumedRT,
                                     0,
                                     -1,
                                     Framebuffer::CubemapFace(i));

      RHI::SetFramebuffer(GL_DRAW_FRAMEBUFFER, writeBuffer->GetFboId());
      RHI::SetFramebuffer(GL_READ_FRAMEBUFFER, readBuffer->GetFboId());

      glCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mipLevel, 0, 0, 0, 0, src->m_width, src->m_height);
    }
  }

  CubeMapPtr Renderer::GenerateDiffuseEnvMap(CubeMapPtr cubemap, int size)
  {
    const TextureSettings set = {GraphicTypes::TargetCubeMap,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::SampleNearest,
                                 GraphicTypes::SampleNearest,
                                 GraphicTypes::FormatRGBA16F,
                                 GraphicTypes::FormatRGBA,
                                 GraphicTypes::TypeFloat,
                                 MsaaSampleCount::x0,
                                 false};

    // Don't allow caches bigger than the actual image.
    size                      = glm::min(size, cubemap->m_width);

    RenderTargetPtr cubeMapRt = MakeNewPtr<RenderTarget>(size, size, set, "DiffuseIRCacheRT");
    cubeMapRt->Init();

    // Views for 6 different angles
    CameraPtr cam = MakeNewPtr<Camera>();
    cam->SetLens(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    Mat4 views[]    = {glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                       glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                       glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                       glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                       glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
                       glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};

    // Create material
    MaterialPtr mat = MakeNewPtr<Material>();
    ShaderPtr vert  = GetShaderManager()->Create<Shader>(ShaderPath("irradianceGenerateVert.shader", true));
    ShaderPtr frag  = GetShaderManager()->Create<Shader>(ShaderPath("irradianceGenerateFrag.shader", true));

    mat->m_cubeMap  = cubemap;
    mat->SetFragmentShaderVal(frag);
    mat->SetVertexShaderVal(vert);
    mat->GetRenderState()->cullMode = CullingType::TwoSided;
    mat->Init();

    m_oneColorAttachmentFramebuffer->ReconstructIfNeeded({size, size, false, false});

    for (int i = 0; i < 6; i++)
    {
      Vec3 pos;
      Quaternion rot;
      Vec3 sca;
      DecomposeMatrix(views[i], &pos, &rot, &sca);

      cam->m_node->SetTranslation(ZERO, TransformationSpace::TS_WORLD);
      cam->m_node->SetOrientation(rot, TransformationSpace::TS_WORLD);
      cam->m_node->SetScale(sca);

      m_oneColorAttachmentFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                          cubeMapRt,
                                                          0,
                                                          -1,
                                                          (Framebuffer::CubemapFace) i);

      SetFramebuffer(m_oneColorAttachmentFramebuffer, GraphicBitFields::None);
      DrawCube(cam, mat);
    }

    SetFramebuffer(nullptr, GraphicBitFields::None);

    CubeMapPtr newCubeMap = MakeNewPtr<CubeMap>();
    newCubeMap->Consume(cubeMapRt);

    return newCubeMap;
  }

  CubeMapPtr Renderer::GenerateSpecularEnvMap(CubeMapPtr cubemap, int size, int mipMaps)
  {
    const TextureSettings set = {GraphicTypes::TargetCubeMap,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::UVClampToEdge,
                                 GraphicTypes::SampleLinearMipmapLinear,
                                 GraphicTypes::SampleLinear,
                                 GraphicTypes::FormatRGBA16F,
                                 GraphicTypes::FormatRGBA,
                                 GraphicTypes::TypeFloat,
                                 MsaaSampleCount::x0,
                                 false};

    // Don't allow caches bigger than the actual image.
    size                      = glm::min(size, cubemap->m_width);

    RenderTargetPtr cubemapRt = MakeNewPtr<RenderTarget>(size, size, set);
    cubemapRt->Init();

    // Intentionally creating space to fill later. ( mip maps will be calculated for specular ibl )
    cubemapRt->GenerateMipMaps();

    // Views for 6 different angles
    CameraPtr cam = MakeNewPtr<Camera>();
    cam->SetLens(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
    Mat4 views[]    = {glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                       glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                       glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                       glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                       glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
                       glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};

    // Create material
    MaterialPtr mat = MakeNewPtr<Material>();
    ShaderPtr vert  = GetShaderManager()->Create<Shader>(ShaderPath("positionVert.shader", true));
    ShaderPtr frag  = GetShaderManager()->Create<Shader>(ShaderPath("preFilterEnvMapFrag.shader", true));

    mat->m_cubeMap  = cubemap;
    mat->SetFragmentShaderVal(frag);
    mat->SetVertexShaderVal(vert);
    mat->GetRenderState()->cullMode = CullingType::TwoSided;
    mat->Init();

    m_oneColorAttachmentFramebuffer->ReconstructIfNeeded({size, size, false, false});

    UVec2 lastViewportSize = m_viewportSize;

    assert(size >= 128 && "Due to RHIConstants::SpecularIBLLods, it can't be lower than this resolution.");
    for (int mip = 0; mip < mipMaps; mip++)
    {
      int mipSize               = (int) (size * std::powf(0.5f, (float) mip));

      // Create a temporary cubemap for each mipmap level
      RenderTargetPtr mipCubeRt = MakeNewPtr<RenderTarget>(mipSize, mipSize, set);
      mipCubeRt->Init();

      for (int i = 0; i < 6; ++i)
      {
        Vec3 pos;
        Quaternion rot;
        Vec3 sca;
        DecomposeMatrix(views[i], &pos, &rot, &sca);

        cam->m_node->SetTranslation(ZERO, TransformationSpace::TS_WORLD);
        cam->m_node->SetOrientation(rot, TransformationSpace::TS_WORLD);
        cam->m_node->SetScale(sca);

        m_oneColorAttachmentFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                            mipCubeRt,
                                                            0,
                                                            -1,
                                                            (Framebuffer::CubemapFace) i);

        SetFramebuffer(m_oneColorAttachmentFramebuffer, GraphicBitFields::None);

        mat->UpdateProgramUniform("roughness", (float) mip / (float) mipMaps);
        mat->UpdateProgramUniform("resPerFace", (float) mipSize);

        RHI::SetTexture((GLenum) GraphicTypes::TargetCubeMap, cubemap->m_textureId, 0);

        DrawCube(cam, mat);

        // Copy color attachment to cubemap's correct mip level and face.
        RHI::SetTexture((GLenum) GraphicTypes::TargetCubeMap, cubemapRt->m_textureId, 0);
        glCopyTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, mip, 0, 0, 0, 0, mipSize, mipSize);
      }
    }

    SetFramebuffer(nullptr, GraphicBitFields::None);

    CubeMapPtr newCubeMap = MakeNewPtr<CubeMap>();
    newCubeMap->Consume(cubemapRt);

    return newCubeMap;
  }

  void Renderer::ValidateBackbufferSrgbEncoding()
  {
    RenderSystem* rsys = GetRenderSystem();
    if (!rsys)
    {
      return;
    }

    if (!rsys->m_backbufferFormatIsSRGB)
    {
      // Nothing to validate if backbuffer not sRGB.
      return;
    }

    // Work on backbuffer
    RHI::SetFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    RHI::SetFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glViewport(0, 0, (GLsizei) 100, (GLsizei) 100);

    // Clear with linear 0.5 gray
    const float testLinear = 0.5f;
    glClearColor(testLinear, testLinear, testLinear, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish(); // Make sure clear completed

    // Read back a single pixel
    ubyte rgba[4] = {0, 0, 0, 0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    // Expected sRGB encoding value
    ubyte expected               = 188;

    // Allow small tolerance due to driver rounding
    int tolerance                = 2;
    bool matchR                  = std::abs((int) rgba[0] - (int) expected) <= tolerance;
    bool matchG                  = std::abs((int) rgba[1] - (int) expected) <= tolerance;
    bool matchB                  = std::abs((int) rgba[2] - (int) expected) <= tolerance;

    bool backbufferIsSrgbEncoded = matchR && matchG && matchB;
    if (!backbufferIsSrgbEncoded)
    {
      rsys->m_backbufferFormatIsSRGB = false;
    }
  }

} // namespace ToolKit
