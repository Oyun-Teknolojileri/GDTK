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

    if (TKStats* stats = GetTKStats())
    {
      for (const auto& pair : m_drawnFrameBufferStats)
      {
        if (pair.second > 0)
        {
          stats->m_renderPassCount++;
        }
      }
    }
  }

  void Renderer::InvalidateGraphicsConstants()
  {
    const ShadowSettingsPtr shadows                   = GetEngineSettings().m_graphics->m_shadows;

    GraphicConstantsGpuBuffer& graphicConstantsBuffer = m_globalGpuBuffers->graphicConstantBuffer;
    graphicConstantsBuffer.m_data.shadowDistance      = shadows->GetShadowMaxDistance();
    graphicConstantsBuffer.m_data.cascadeCount        = shadows->GetCascadeCountVal();
    graphicConstantsBuffer.m_data.shadowAtlasSize     = RHIConstants::ShadowAtlasTextureSize;
    graphicConstantsBuffer.m_data.iblMaxReflectionLod = RHIConstants::SpecularIBLLods;
    graphicConstantsBuffer.m_data.cascadeDistances    = *((Vec4*) &shadows->GetCascadeDistancesVal());
    graphicConstantsBuffer.Invalidate();
  }

  void Renderer::Init()
  {
    m_uiCamera                      = MakeNewPtr<Camera>();
    m_oneColorAttachmentFramebuffer = MakeNewPtr<Framebuffer>("RendererOneColorFB");
    m_dummyDrawCube                 = MakeNewPtr<Cube>();

    m_gpuProgramManager             = GetGpuProgramManager();

    glGenQueries(1, &m_gpuTimerQuery);

    const char* renderer = (const char*) glGetString(GL_RENDERER);
    GetLogger()->Log(String("Graphics Card ") + renderer);

    // Default states.
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    glClearDepthf(1.0f);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  }

  Renderer::~Renderer()
  {
    m_oneColorAttachmentFramebuffer = nullptr;
    m_gaussianBlurMaterial          = nullptr;
    m_averageBlurMaterial           = nullptr;
    m_copyFb                        = nullptr;
    m_copyMaterial                  = nullptr;

    m_framebuffer                   = nullptr;
    m_shadowAtlas                   = nullptr;
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

      if (TKStats* stats = GetTKStats())
      {
        stats->m_cameraUpdatePerFrame++;
      }
    }
  }

  void Renderer::Render(const RenderJob& job)
  {
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
          SetTexture(3, animTexture->m_textureId);
        }

        // animation to blend.
        if (job.animData.blendAnimation != nullptr)
        {
          animTexture = animPlayer->GetAnimationDataTexture(skel->GetIdVal(), job.animData.blendAnimation->GetIdVal());
          SetTexture(2, animTexture->m_textureId);
        }
      }
      else
      {
        SetTexture(3, skel->m_bindPoseTexture->m_textureId);
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
      GLint isSkinnedLoc = m_currentProgram->GetDefaultUniformLocation(Uniform::IS_SKINNED);
      bool isSkinned     = mesh->IsSkinned();
      if (isSkinned)
      {
        SkeletonPtr skel = static_cast<SkinMesh*>(job.Mesh)->m_skeleton;
        assert(skel != nullptr);

        GLint numBonesLoc = m_currentProgram->GetDefaultUniformLocation(Uniform::NUM_BONES);
        glUniform1ui(isSkinnedLoc, 1);

        GLuint boneCount = (GLuint) skel->m_bones.size();
        glUniform1f(numBonesLoc, (float) boneCount);
      }
      else
      {
        glUniform1ui(isSkinnedLoc, 0);
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

    Stats::AddDrawCall();
  }

  void Renderer::RenderWithProgramFromMaterial(const RenderJobArray& jobs)
  {
    for (int i = 0; i < jobs.size(); ++i)
    {
      RenderWithProgramFromMaterial(jobs[i]);
    }
  }

  void Renderer::RenderWithProgramFromMaterial(const RenderJob& job)
  {
    job.Material->Init();
    ShaderPtr vert        = job.Material->GetVertexShaderVal();
    ShaderPtr frag        = job.Material->GetFragmentShaderVal();

    GpuProgramPtr program = m_gpuProgramManager->CreateProgram(vert, frag);
    BindProgram(program);
    Render(job);
  }

  void Renderer::Render(const RenderJobArray& jobs)
  {
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
      glLineWidth(m_renderState.lineWidth);
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

  void Renderer::SetFramebuffer(FramebufferPtr fb,
                                GraphicBitFields attachmentsToClear,
                                const Vec4& clearColor,
                                GraphicFramebufferTypes fbType)
  {
    if (fb != nullptr)
    {
      RHI::SetFramebuffer((GLenum) fbType, fb->GetFboId());

      const FramebufferSettings& fbSet = fb->GetSettings();
      SetViewportSize(fbSet.width, fbSet.height);
    }
    else
    {
      // Backbuffer
      RHI::SetFramebuffer((GLenum) fbType, 0);

      SetViewportSize(m_windowSize.x, m_windowSize.y);
    }

    if (attachmentsToClear != GraphicBitFields::None)
    {
      ClearBuffer(attachmentsToClear, clearColor);
    }

    m_framebuffer = fb;
  }

  void Renderer::StartTimerQuery()
  {
    m_cpuTime               = GetElapsedMilliSeconds();
    GraphicSettingsPtr gset = GetEngineSettings().m_graphics;
    if (gset->GetEnableGpuTimerVal())
    {
      if constexpr (TK_PLATFORM == PLATFORM::TKWindows)
      {
#ifdef GL_TIME_ELAPSED_EXT
        glBeginQuery(GL_TIME_ELAPSED_EXT, m_gpuTimerQuery);
#endif
      }
    }
  }

  void Renderer::EndTimerQuery()
  {
    float cpuTime           = GetElapsedMilliSeconds();
    m_cpuTime               = cpuTime - m_cpuTime;

    GraphicSettingsPtr gset = GetEngineSettings().m_graphics;
    if (gset->GetEnableGpuTimerVal())
    {
      if constexpr (TK_PLATFORM == PLATFORM::TKWindows)
      {
#ifdef GL_TIME_ELAPSED_EXT
        glEndQuery(GL_TIME_ELAPSED_EXT);
#endif
      }
    }
  }

  void Renderer::GetElapsedTime(float& cpu, float& gpu)
  {
    cpu = m_cpuTime;
    gpu = 1.0f;
    if constexpr (TK_PLATFORM == PLATFORM::TKWindows)
    {
      GraphicSettingsPtr gset = GetEngineSettings().m_graphics;
      if (gset->GetEnableGpuTimerVal())
      {
        GLuint elapsedTime;
        glGetQueryObjectuiv(m_gpuTimerQuery, GL_QUERY_RESULT, &elapsedTime);

        gpu = glm::max(1.0f, (float) (elapsedTime) / 1000000.0f);
      }
    }
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

// By invalidating the frame buffers attachment, bandwith and performance saving is aimed,
// Nvidia driver issue makes the invalidate perform much worse. Clear will work the same in terms of bandwith saving
// with no performance penalty.
#define PREFER_CLEAR_OVER_INVALIDATE 1

  void Renderer::InvalidateFramebufferDepth(FramebufferPtr frameBuffer)
  {
#if PREFER_CLEAR_OVER_INVALIDATE
    SetFramebuffer(frameBuffer, GraphicBitFields::DepthBits);
#else
    constexpr GLenum invalidAttachments[1] = {GL_DEPTH_ATTACHMENT};

    SetFramebuffer(frameBuffer, GraphicBitFields::None);
    RHI::InvalidateFramebuffer(GL_FRAMEBUFFER, 1, invalidAttachments);
#endif
  }

  void Renderer::InvalidateFramebufferStencil(FramebufferPtr frameBuffer)
  {
#if PREFER_CLEAR_OVER_INVALIDATE
    SetFramebuffer(frameBuffer, GraphicBitFields::StencilBits);
#else
    constexpr GLenum invalidAttachments[1] = {GL_STENCIL_ATTACHMENT};

    SetFramebuffer(frameBuffer, GraphicBitFields::None);
    RHI::InvalidateFramebuffer(GL_FRAMEBUFFER, 1, invalidAttachments);
#endif
  }

  void Renderer::InvalidateFramebufferDepthStencil(FramebufferPtr frameBuffer)
  {
#if PREFER_CLEAR_OVER_INVALIDATE
    SetFramebuffer(frameBuffer, GraphicBitFields::DepthStencilBits);
#else
    constexpr GLenum invalidAttachments[2] = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};

    SetFramebuffer(frameBuffer, GraphicBitFields::None);
    RHI::InvalidateFramebuffer(GL_FRAMEBUFFER, 2, invalidAttachments);
#endif
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
    if (m_tempQuad == nullptr)
    {
      m_tempQuad = MakeNewPtr<Quad>();
    }
    m_tempQuad->GetMeshComponent()->GetMeshVal()->m_material = mat;

    RenderJobArray jobs;
    RenderJobProcessor::CreateRenderJobs(jobs, m_tempQuad);

    SetDepthTestFunc(CompareFunctions::FuncAlways);
    RenderWithProgramFromMaterial(jobs);

    SetDepthTestFunc(CompareFunctions::FuncLess);
  }

  void Renderer::DrawCube(CameraPtr cam, MaterialPtr mat, const Mat4& transform)
  {
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
    assert(src->m_initiated && dst->m_initiated && "Texture is not initialized.");
    assert(src->m_width == dst->m_width && src->m_height == dst->m_height && "Sizes of the textures are not the same.");

    if (m_copyFb == nullptr)
    {
      FramebufferSettings fbSettings = {src->m_width, src->m_height, false, false};
      m_copyFb                       = MakeNewPtr<Framebuffer>(fbSettings, "RendererCopyFB");
      m_copyFb->Init();
    }

    m_copyFb->ReconstructIfNeeded(src->m_width, src->m_height);

    FramebufferPtr lastFb = m_framebuffer;

    RenderTargetPtr rt    = Cast<RenderTarget>(dst);
    m_copyFb->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, rt);
    SetFramebuffer(m_copyFb, GraphicBitFields::AllBits);

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
    SetFramebuffer(lastFb, GraphicBitFields::None);
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

  void Renderer::EnableDepthWrite(bool enable) { glDepthMask(enable); }

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

  void Renderer::Apply7x1GaussianBlur(const TexturePtr src, RenderTargetPtr dst, const Vec3& axis, const float amount)
  {
    m_oneColorAttachmentFramebuffer->ReconstructIfNeeded({dst->m_width, dst->m_height, false, false});

    if (m_gaussianBlurMaterial == nullptr)
    {
      ShaderPtr vert         = GetShaderManager()->Create<Shader>(ShaderPath("gausBlur7x1Vert.shader", true));
      ShaderPtr frag         = GetShaderManager()->Create<Shader>(ShaderPath("gausBlur7x1Frag.shader", true));

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

  void Renderer::ApplyAverageBlur(const TexturePtr src, RenderTargetPtr dst, const Vec3& axis, const float amount)
  {
    m_oneColorAttachmentFramebuffer->ReconstructIfNeeded({dst->m_width, dst->m_height, false, false});

    if (m_averageBlurMaterial == nullptr)
    {
      ShaderPtr vert        = GetShaderManager()->Create<Shader>(ShaderPath("avgBlurVert.shader", true));
      ShaderPtr frag        = GetShaderManager()->Create<Shader>(ShaderPath("avgBlurFrag.shader", true));

      m_averageBlurMaterial = MakeNewPtr<Material>();
      m_averageBlurMaterial->SetVertexShaderVal(vert);
      m_averageBlurMaterial->SetFragmentShaderVal(frag);
      m_averageBlurMaterial->SetDiffuseTextureVal(nullptr);
      m_averageBlurMaterial->Init();
    }

    m_averageBlurMaterial->SetDiffuseTextureVal(src);
    m_averageBlurMaterial->UpdateProgramUniform("BlurScale", axis * amount);

    m_oneColorAttachmentFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, dst);

    SetFramebuffer(m_oneColorAttachmentFramebuffer, GraphicBitFields::None);
    DrawFullQuad(m_averageBlurMaterial);
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
      set.GenerateMipMap = false;

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

      QuadPtr quad     = MakeNewPtr<Quad>();
      MeshPtr mesh     = quad->GetMeshComponent()->GetMeshVal();
      mesh->m_material = material;
      mesh->Init();

      SetFramebuffer(utilFramebuffer, GraphicBitFields::AllBits);

      CameraPtr camera = MakeNewPtr<Camera>();
      SetCamera(camera, true);

      RenderJobArray jobs;
      RenderJobProcessor::CreateRenderJobs(jobs, quad);
      RenderWithProgramFromMaterial(jobs);

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
    const MaterialCacheItem& cache = mat->GetCacheItem();
    if (cache.DiffuseTextureInUse())
    {
      SetTexture(0, mat->GetDiffuseTextureVal()->m_textureId);
    }

    if (cache.EmissiveTextureInUse())
    {
      SetTexture(1, mat->GetEmissiveTextureVal()->m_textureId);
    }

    if (cache.MetallicRoughnessTextureInUse())
    {
      SetTexture(2, mat->GetMetallicRoughnessTextureVal()->m_textureId);
    }

    m_normalMapInUse = false;
    if (cache.NormalTextureInUse())
    {
      SetTexture(9, mat->GetNormalTextureVal()->m_textureId);
      m_normalMapInUse = true;
    }
  }

  void Renderer::SetLights(const LightRawPtrArray& lights)
  {
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
      if (TKStats* stats = GetTKStats())
      {
        stats->m_lightCacheInvalidationPerFrame++;
      }
    }

    if (spotCache.Map())
    {
      if (TKStats* stats = GetTKStats())
      {
        stats->m_lightCacheInvalidationPerFrame++;
      }
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
    if (m_currentProgram == nullptr || m_currentProgram->m_handle != program->m_handle)
    {
      m_currentProgram = program;
      glUseProgram(program->m_handle);
    }
  }

  void Renderer::ResetUsedTextureSlots()
  {
    for (int i = 0; i < 17; i++)
    {
      SetTexture(i, 0);
    }
  }

  void Renderer::SetDirectionalLights(const LightRawPtrArray& lights)
  {
    m_globalGpuBuffers->directionalLightBuffer.Map(lights);
    m_drawCommand.SetActiveDirectionalLightCount((int) lights.size());
  }

  void Renderer::SetDataTextures(const RenderJob& job)
  {
    // Cube map data.
    Material* mat = job.Material;
    if (mat && mat->m_cubeMap)
    {
      SetTexture(6, mat->m_cubeMap->m_textureId);
    }

    // Sky and Ibl data.
    m_drawCommand.SetIblInUse(false);
    const EnvironmentComponent* envCom = job.EnvironmentVolume;
    if (envCom)
    {
      const HdriPtr& hdriPtr     = envCom->GetHdriVal();
      CubeMapPtr& diffuseEnvMap  = hdriPtr->m_diffuseEnvMap;
      CubeMapPtr& specularEnvMap = hdriPtr->m_specularEnvMap;

      if (diffuseEnvMap && specularEnvMap && m_brdfLut)
      {
        SetTexture(7, diffuseEnvMap->m_textureId);
        SetTexture(15, specularEnvMap->m_textureId);
        SetTexture(16, m_brdfLut->m_textureId);

        m_drawCommand.SetIblInUse(true);
        m_drawCommand.SetIblIntensity(envCom->GetIntensityVal());
        if (const EntityPtr& env = envCom->OwnerEntity())
        {
          m_iblRotation = Mat4(env->m_node->GetOrientation());
        }
      }
    }

    // ao texture.
    if (m_ambientOcculusionInUse)
    {
      SetTexture(5, m_aoTexture->m_textureId);
    }

    // Bind shadow map if activated.
    if (m_shadowAtlas != nullptr)
    {
      SetTexture(8, m_shadowAtlas->m_textureId);
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
        case Uniform::NORMAL_MAP_IN_USE:
          glUniform1i(loc, m_normalMapInUse);
          break;
        default:
          break;
        }
      }
    }

    // Built-in array uniforms.
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
    // Send if its animated or not.
    int uniformLoc = program->GetDefaultUniformLocation(Uniform::IS_ANIMATED);
    if (uniformLoc != -1)
    {
      glUniform1ui(uniformLoc, job.animData.currentAnimation != nullptr);
    }

    if (job.animData.currentAnimation == nullptr)
    {
      // If not animated, just skip the rest.
      return;
    }

    // Send key frames.
    uniformLoc = program->GetDefaultUniformLocation(Uniform::KEY_FRAME_COUNT);
    if (uniformLoc != -1)
    {
      glUniform1f(uniformLoc, job.animData.keyFrameCount);
    }

    if (job.animData.keyFrameCount > 0)
    {
      uniformLoc = program->GetDefaultUniformLocation(Uniform::KEY_FRAME_1);
      if (uniformLoc != -1)
      {
        glUniform1f(uniformLoc, job.animData.firstKeyFrame);
      }

      uniformLoc = program->GetDefaultUniformLocation(Uniform::KEY_FRAME_2);
      if (uniformLoc != -1)
      {
        glUniform1f(uniformLoc, job.animData.secondKeyFrame);
      }

      uniformLoc = program->GetDefaultUniformLocation(Uniform::KEY_FRAME_INT_TIME);
      if (uniformLoc != -1)
      {
        glUniform1f(uniformLoc, job.animData.keyFrameInterpolationTime);
      }
    }

    // Send blend data.
    uniformLoc = program->GetDefaultUniformLocation(Uniform::BLEND_ANIMATION);
    if (uniformLoc != -1)
    {
      glUniform1i(uniformLoc, job.animData.blendAnimation != nullptr);
    }

    if (job.animData.blendAnimation != nullptr)
    {
      uniformLoc = program->GetDefaultUniformLocation(Uniform::BLEND_FACTOR);
      if (uniformLoc != -1)
      {
        glUniform1f(uniformLoc, job.animData.animationBlendFactor);
      }

      uniformLoc = program->GetDefaultUniformLocation(Uniform::BLEND_KEY_FRAME_1);
      if (uniformLoc != -1)
      {
        glUniform1f(uniformLoc, job.animData.blendFirstKeyFrame);
      }

      uniformLoc = program->GetDefaultUniformLocation(Uniform::BLEND_KEY_FRAME_2);
      if (uniformLoc != -1)
      {
        glUniform1f(uniformLoc, job.animData.blendSecondKeyFrame);
      }

      uniformLoc = program->GetDefaultUniformLocation(Uniform::BLEND_KEY_FRAME_INT_TIME);
      if (uniformLoc != -1)
      {
        glUniform1f(uniformLoc, job.animData.blendKeyFrameInterpolationTime);
      }

      uniformLoc = program->GetDefaultUniformLocation(Uniform::BLEND_KEY_FRAME_COUNT);
      if (uniformLoc != -1)
      {
        glUniform1f(uniformLoc, job.animData.blendKeyFrameCount);
      }
    }
  }

  void Renderer::SetTexture(ubyte slotIndx, uint textureId)
  {
    assert(slotIndx < 17 && "You exceed texture slot count");

    static const GLenum textureTypeLut[17] = {
        GL_TEXTURE_2D,       // 0 -> Color Texture
        GL_TEXTURE_2D,       // 1 -> Emissive Texture
        GL_TEXTURE_2D,       // 2 -> EMPTY
        GL_TEXTURE_2D,       // 3 -> Skinning informatison
        GL_TEXTURE_2D,       // 4 -> Metallic Roughness Texture
        GL_TEXTURE_2D,       // 5 -> AO Texture
        GL_TEXTURE_CUBE_MAP, // 6 -> Cubemap
        GL_TEXTURE_CUBE_MAP, // 7 -> Irradiance Map
        GL_TEXTURE_2D_ARRAY, // 8 -> Shadow Atlas
        GL_TEXTURE_2D,       // 9 -> Normal map, gbuffer position
        GL_TEXTURE_2D,       // 10 -> gBuffer normal texture
        GL_TEXTURE_2D,       // 11 -> gBuffer color texture
        GL_TEXTURE_2D,       // 12 -> gBuffer emissive texture
        GL_TEXTURE_2D,       // 13 -> EMPTY
        GL_TEXTURE_2D,       // 14 -> gBuffer metallic roughness texture
        GL_TEXTURE_CUBE_MAP, // 15 -> IBL Specular Pre-Filtered Map
        GL_TEXTURE_2D        // 16 -> IBL BRDF Lut
    };

    RHI::SetTexture(textureTypeLut[slotIndx], textureId, slotIndx);
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
                                 1,
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
                                 0,
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
                                 0,
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

} // namespace ToolKit
