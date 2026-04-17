/*
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
#include "ForwardSceneRenderPath.h"
#include "Framebuffer.h"
#include "GradientSky.h"
#include "Logger.h"
#include "Material.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "Node.h"
#include "Pass.h"
#include "PerDrawUniforms.h"
#include "RHI.h"
#include "RenderSystem.h"
#include "Scene.h"
#include "Shader.h"
#include "Skeleton.h"
#include "Stats.h"
#include "Surface.h"
#include "TKAssert.h"
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

    // Reset volatile state to defaults for each frame.
    // This prevents state leaks from passes that don't clean up (e.g. StencilPass, ShadowPass).
    m_renderState.colorMaskEnabled  = true;
    m_renderState.depthTestEnabled  = true;
    m_renderState.depthWriteEnabled = true;
    m_renderState.depthFunction     = CompareFunctions::FuncLess;
    m_renderState.stencilOperation  = StencilOperation::None;
    m_renderState.depthClampEnabled = false;
    m_renderState.blendOverride     = false;

    m_backend->BeginFrame();
  }

  void Renderer::EndRenderFrame()
  {
    SetAmbientOcclusionTexture(nullptr);
    m_sky = nullptr;

    for (const auto& pair : m_drawnFrameBufferStats)
    {
      if (pair.second > 0)
      {
        Stats::IncrementStat(FrameStatType::RenderPass);
      }
    }

    EndPass();
  }

  void Renderer::InvalidateGraphicsConstants()
  {
    const ShadowSettingsPtr shadows                   = GetEngineSettings().m_graphics->m_shadows;

    GraphicConstantsGpuBuffer& graphicConstantsBuffer = m_globalGpuBuffers->graphicConstantBuffer;
    graphicConstantsBuffer.m_data.shadowDistance      = shadows->GetShadowMaxDistance();
    graphicConstantsBuffer.m_data.cascadeCount        = shadows->GetCascadeCountVal();
    graphicConstantsBuffer.m_data.shadowAtlasSize     = (float) shadows->GetShadowAtlasResolution();
    graphicConstantsBuffer.m_data.iblMaxReflectionLod = RHIConstants::SpecularIBLLods - 1;
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

    String renderer                 = m_backend->GetBackendRendererString();
    GetLogger()->Log(String("Graphics Card ") + renderer);

    // Validate sRGB automatic encoding on backbuffer if enabled.
    RenderSystem* rsys = GetRenderSystem();
    if (rsys && rsys->m_backbufferFormatIsSRGB)
    {
      if (!m_backend->ValidateBackbufferSrgbEncoding())
      {
        rsys->m_backbufferFormatIsSRGB = false;
      }
    }

    m_backend->SetSrgbAutoEncoding(GetRenderSystem()->m_backbufferFormatIsSRGB);
    m_backend->SetDefaultClearColor(Vec4(0.0f, 0.0f, 0.0f, 1.0f));
  }

  Renderer::~Renderer()
  {
    // Release all GPU resource references before destroying backend.
    m_oneColorAttachmentFramebuffer = nullptr;
    m_gaussianBlurMaterial          = nullptr;
    m_averageBlurMaterial           = nullptr;
    m_copyFrameBuffer               = nullptr;
    m_copyMaterial                  = nullptr;
    m_framebuffer                   = nullptr;
    m_shadowAtlas                   = nullptr;
    m_brdfLut                       = nullptr;
    m_aoTexture                     = nullptr;
    m_tempQuad                      = nullptr;
    m_tempQuadMaterial              = nullptr;
    m_dummyDrawCube                 = nullptr;
    m_uiCamera                      = nullptr;
    m_sky                           = nullptr;
    m_currentProgram                = nullptr;

    SafeDel(m_backend);
  }

  void Renderer::SrgbAutoEncoding(bool enable) { m_backend->SetSrgbAutoEncoding(enable); }

  int Renderer::GetMaxArrayTextureLayers()
  {
    if (m_maxArrayTextureLayers == -1)
    {
      m_maxArrayTextureLayers = m_backend->GetMaxArrayTextureLayers();
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
        float width     = m_viewportRect.x * 0.5f;
        float height    = m_viewportRect.y * 0.5f;

        float camWidth  = camera->Right();
        float camHeight = camera->Top();

        if (glm::notEqual(camWidth, width) || glm::notEqual(camHeight, height))
        {
          camera->SetLens(-width, width, -height, height, camera->Near(), camera->Far());
        }
      }
      else
      {
        float aspect    = (float) m_viewportRect.x / (float) m_viewportRect.y;
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

    m_model                    = job.WorldTransform;

    // Compose state.
    RenderState composed       = *job.Material->GetRenderState();
    composed.depthTestEnabled  = m_renderState.depthTestEnabled;
    composed.depthWriteEnabled = m_renderState.depthWriteEnabled;
    composed.depthFunction     = m_renderState.depthFunction;
    composed.stencilOperation  = m_renderState.stencilOperation;
    composed.colorMaskEnabled  = m_renderState.colorMaskEnabled;
    composed.depthClampEnabled = m_renderState.depthClampEnabled;
    if (m_renderState.blendOverride)
    {
      composed.blendFunction = m_renderState.blendOverrideFunc;
    }

    if (job.requireCullFlip)
    {
      switch (composed.cullMode)
      {
        case CullingType::Front:
          composed.cullMode = CullingType::Back;
          break;
        case CullingType::Back:
          composed.cullMode = CullingType::Front;
          break;
      }
    }

    m_backend->BindPipeline(m_currentProgram, &composed);

    auto activateSkinning = [&](const Mesh* mesh)
    {
      int skinParamsLoc = m_currentProgram->GetDefaultUniformLocation(Uniform::SKIN_PARAMS);
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
        m_backend->SetUniform4f(skinParamsLoc, Vec4(boneCount, 1.0f, isAnimated, hasBlend));
      }
      else
      {
        m_backend->SetUniform4f(skinParamsLoc, Vec4(0.0f));
      }
    };

    const Mesh* mesh = job.Mesh;
    activateSkinning(mesh);

    FeedAnimationUniforms(m_currentProgram, job);
    FeedUniforms(m_currentProgram, job);

    DrawDesc desc;
    desc.mesh         = mesh;
    desc.vertexLayout = mesh->m_vertexLayout;
    desc.indexed      = mesh->m_indexCount != 0;
    desc.elementCount = desc.indexed ? mesh->m_indexCount : mesh->m_vertexCount;
    desc.type         = composed.drawType;
    m_backend->Draw(desc);

    if (m_framebuffer)
    {
      int& drawCount = m_drawnFrameBufferStats[reinterpret_cast<uintptr_t>(m_framebuffer.get())];
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

  void Renderer::SetStencilOperation(StencilOperation op) { m_renderState.stencilOperation = op; }

  void Renderer::SetFramebuffer(FramebufferPtr frameBuffer,
                                GraphicBitFields attachmentsToClear,
                                const Vec4& clearColor,
                                GraphicBitFields discardBits)
  {
    TK_PROFILE_FUNCTION();

    PassDesc desc;
    desc.target      = frameBuffer;
    desc.clearBits   = attachmentsToClear;
    desc.clearColor  = clearColor;
    desc.discardBits = discardBits;
    m_backend->BeginPass(desc);

    if (frameBuffer != nullptr)
    {
      const FramebufferSettings& fbSet = frameBuffer->GetSettings();
      SetViewportRect(0, 0, fbSet.width, fbSet.height);
    }
    else
    {
      SetViewportRect(0, 0, m_windowSize.x, m_windowSize.y);
    }

    m_framebuffer = frameBuffer;
  }

  void Renderer::EndPass() { m_backend->EndPass(); }

  void Renderer::StartTimerQuery() { m_backend->StartTimerQuery(); }

  void Renderer::EndTimerQuery() { m_backend->EndTimerQuery(); }

  void Renderer::GetElapsedTime(float& cpu, float& gpu) { m_backend->GetElapsedTime(cpu, gpu); }

  FramebufferPtr Renderer::GetFrameBuffer() { return m_framebuffer; }

  void Renderer::ClearColorBuffer(const Vec4& color) { m_backend->ClearColorBuffer(color); }

  void Renderer::ClearBuffer(GraphicBitFields fields, const Vec4& value) { m_backend->ClearBuffer(fields, value); }

  void Renderer::ColorMask(bool r, bool g, bool b, bool a) { m_renderState.colorMaskEnabled = r && g && b && a; }

  uint Renderer::GetNativeTextureHandle(const TexturePtr& tex)
  {
    void* id = GetRenderSystem()->GetRenderer()->GetBackend()->GetNativeTextureHandle(tex.get());
    return static_cast<uint>(reinterpret_cast<intptr_t>(id));
  }

  void Renderer::CopyFrameBuffer(FramebufferPtr src, FramebufferPtr dest, GraphicBitFields fields)
  {
    TK_PROFILE_FUNCTION();

    m_backend->CopyFramebuffer(src, dest, fields);
  }

  void Renderer::ResolveFramebuffer(FramebufferPtr source, FramebufferPtr target, const IntArray& attachments)
  {
    TK_PROFILE_FUNCTION();

    assert(source->Initialized() && "Source framebuffer is not initialized.");
    assert(target->Initialized() && "Target framebuffer is not initialized.");

    m_backend->ResolveFramebuffer(source, target, attachments);
  }

  void Renderer::SetViewport(Viewport* viewport) { SetFramebuffer(viewport->m_framebuffer, GraphicBitFields::AllBits); }

  void Renderer::SetViewportRect(uint x, uint y, uint width, uint height)
  {
    if (width == m_viewportRect.x && height == m_viewportRect.y && m_viewportRect.z == x && m_viewportRect.w == y)
    {
      return;
    }

    m_viewportRect = UVec4(width, height, x, y);
    m_backend->SetViewport(x, y, width, height);
  }

  void Renderer::SetScissor(uint x, uint y, uint width, uint height) { m_backend->SetScissor(x, y, width, height); }

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
    EndPass();

    Stats::EndGpuScope();
  }

  void Renderer::OverrideBlendState(bool enableOverride, BlendFunction func)
  {
    m_renderState.blendOverride     = enableOverride;
    m_renderState.blendOverrideFunc = func;
  }

  void Renderer::EnableBlending(bool enable)
  {
    if (enable)
    {
      m_renderState.blendOverride = false;
    }
    else
    {
      m_renderState.blendOverride     = true;
      m_renderState.blendOverrideFunc = BlendFunction::NONE;
    }
  }

  void Renderer::EnableDepthWrite(bool enable) { m_renderState.depthWriteEnabled = enable; }

  void Renderer::EnableDepthTest(bool enable) { m_renderState.depthTestEnabled = enable; }

  void Renderer::SetDepthTestFunc(CompareFunctions func) { m_renderState.depthFunction = func; }

  bool Renderer::EnableDepthClamp(bool enable)
  {
    m_renderState.depthClampEnabled = enable;
    return true;
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
    EndPass();
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

    m_backend->EnableScissorTest(true);

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
        SetViewportRect(0, 0, texSize, texSize);
        SetScissor(sx, sy, slotSize, slotSize);

        m_gaussianBlurMaterial->UpdateProgramUniform("BlurScale", Vec3(blurAmount, 0.0f, 0.0f));
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurLayer", (float) layer);
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurClampMin", clampMin);
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurClampMax", clampMax);

        BindProgramOfMaterial(m_gaussianBlurMaterial.get());

        m_backend->BindTexture(1, srcArray);

        DrawFullQuad(m_gaussianBlurMaterial);
        EndPass();
      }

      // Vertical pass: temp 2D RT -> array texture layer
      {
        vert->SetDefine("TextureArray", "0");
        frag->SetDefine("TextureArray", "0");
        frag->SetDefine("KernelSize", kernelStr);
        frag->SetDefine("BlurClampEnabled", "1");

        framebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, srcArray, 0, layer);
        SetFramebuffer(framebuffer, GraphicBitFields::None);
        SetViewportRect(0, 0, texSize, texSize);
        SetScissor(sx, sy, slotSize, slotSize);

        m_gaussianBlurMaterial->SetDiffuseTextureVal(tempRT);
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurScale", Vec3(0.0f, blurAmount, 0.0f));
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurClampMin", clampMin);
        m_gaussianBlurMaterial->UpdateProgramUniform("BlurClampMax", clampMax);

        DrawFullQuad(m_gaussianBlurMaterial);
        EndPass();
      }
    }

    m_backend->EnableScissorTest(false);

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
      EndPass();

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

    if (m_currentProgram == nullptr || m_currentProgram->m_gpuData.get() != program->m_gpuData.get())
    {
      m_currentProgram = program;
      m_backend->BindPipeline(program, &m_renderState);
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

        // Pass primary volume local-space BB for OBB per-pixel blend and Parallax Corrected Cubemaps.
        Vec3 offset = envCom->GetPositionOffsetVal();
        Vec3 half   = envCom->GetSizeVal() * 0.5f;
        bool isSky  = false;
        if (const EntityPtr& env = envCom->OwnerEntity())
        {
          isSky = env->IsA<SkyBase>();
        }

        m_drawCommand.SetPrimaryVolumeMin(offset - half, !isSky);
        m_drawCommand.SetPrimaryVolumeMax(offset + half);
        m_drawCommand.SetIblFadeDistance(glm::max(envCom->GetFadeVal(), 0.001f));

        // Sky: rotation applies to IBL image, no volume boundary.
        // Non-Sky: rotation applies to OBB volume, IBL image stays fixed.
        if (const EntityPtr& env = envCom->OwnerEntity())
        {
          if (isSky)
          {
            m_iblRotation = Mat4(env->m_node->GetOrientation());
            m_drawCommand.SetIblInverseVolumeTransform(Mat4(1.0f));
            m_drawCommand.SetIblVolumeTransform(Mat4(1.0f));
          }
          else
          {
            m_iblRotation       = Mat4(1.0f);
            Mat4 worldTransform = env->m_node->GetTransform(TransformationSpace::TS_WORLD);
            m_drawCommand.SetIblInverseVolumeTransform(glm::inverse(worldTransform));
            m_drawCommand.SetIblVolumeTransform(worldTransform);
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

    PerDrawUniforms pdu;
    pdu.model                 = m_model;
    pdu.modelWithoutTranslate = m_modelWithoutTranslate;
    pdu.inverseModel          = m_inverseModel;
    pdu.inverseTransposeModel = m_inverseTransposeModel;
    pdu.iblRotation           = m_iblRotation;
    pdu.iblSecondaryRotation  = m_secondaryIblRotation;
    pdu.viewportSize          = Vec2((float) m_viewportRect.x, (float) m_viewportRect.y);
    pdu.drawCommand           = m_drawCommand;
    pdu.materialData          = job.Material->GetCacheItem().data;

    std::copy(m_activePointLightIndices.begin(), m_activePointLightIndices.end(), pdu.activePointLightIndices);
    std::copy(m_activeSpotLightIndices.begin(), m_activeSpotLightIndices.end(), pdu.activeSpotLightIndices);
    pdu.activePointLightCount = m_activePointLightCount;
    pdu.activeSpotLightCount  = m_activeSpotLightCount;

    // Animation / Skinning
    pdu.keyFrameData          = Vec4(0.0f);
    if (job.animData.currentAnimation != nullptr)
    {
      pdu.keyFrameData = Vec4(job.animData.firstKeyFrame,
                              job.animData.secondKeyFrame,
                              job.animData.keyFrameInterpolationTime,
                              job.animData.keyFrameCount);
    }

    pdu.blendFrameData = Vec4(0.0f);
    if (job.animData.blendAnimation != nullptr)
    {
      pdu.blendFrameData = Vec4(job.animData.blendFirstKeyFrame,
                                job.animData.blendSecondKeyFrame,
                                job.animData.blendKeyFrameInterpolationTime,
                                job.animData.blendKeyFrameCount);
    }

    pdu.animationBlendFactor = job.animData.animationBlendFactor;

    pdu.skinParams           = Vec4(0.0f);
    if (job.Mesh->IsSkinned())
    {
      const SkeletonPtr& skel = static_cast<const SkinMesh*>(job.Mesh)->m_skeleton;
      if (skel)
      {
        float boneCount  = (float) skel->m_bones.size();
        float isAnimated = (job.animData.currentAnimation != nullptr) ? 1.0f : 0.0f;
        float hasBlend   = (job.animData.blendAnimation != nullptr) ? 1.0f : 0.0f;
        pdu.skinParams   = Vec4(boneCount, 1.0f, isAnimated, hasBlend);
      }
    }

    m_backend->SubmitPerDrawData(&pdu, sizeof(pdu));

    // Custom shader uniforms — dispatched through backend.
    m_backend->SubmitCustomUniforms(program, program->m_customUniforms);
  }

  void Renderer::FeedAnimationUniforms(const GpuProgramPtr& program, const RenderJob& job)
  {
    // Now handled by FeedUniforms -> SubmitPerDrawData
  }

  void Renderer::SetTexture(ubyte slotIndx, TexturePtr texture)
  {
    assert(slotIndx < RHIConstants::TextureSlotCount && "You exceed texture slot count");
    m_backend->BindTexture(slotIndx, texture);
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
      EndPass();
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
    EndPass();

    if (pixels != nullptr)
    {
      uint64 requiredSize = mipWidth * mipHeight * 4 * sizeof(float);
      *pixels             = new float[requiredSize];
      m_backend->ReadPixels(0, 0, mipWidth, mipHeight, GraphicTypes::FormatRGBA, GraphicTypes::TypeFloat, *pixels);
    }

    SetFramebuffer(prevBuffer, GraphicBitFields::None);
    EndPass();

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

      m_backend->CopyCubemapFaceFromFramebuffer(dst.get(),
                                                i,
                                                mipLevel,
                                                src->m_width,
                                                src->m_height,
                                                readBuffer.get(),
                                                writeBuffer.get());
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
      EndPass();
    }

    SetFramebuffer(nullptr, GraphicBitFields::None);
    EndPass();

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

        mat->UpdateProgramUniform("roughness", (float) mip / (float) (mipMaps - 1));
        mat->UpdateProgramUniform("resPerFace", (float) mipSize);

        m_backend->BindTexture(0, cubemap);

        DrawCube(cam, mat);
        EndPass();

        // Copy color attachment to cubemap's correct mip level and face.
        m_backend->CopyCubemapFaceFromFramebuffer(cubemapRt.get(), i, mip, mipSize, mipSize, nullptr, nullptr);
      }
    }

    SetFramebuffer(nullptr, GraphicBitFields::None);
    EndPass();

    // Clamp texture max mip level to the last bake level.
    m_backend->SetTextureMaxMipLevel(cubemapRt.get(), mipMaps - 1);

    CubeMapPtr newCubeMap = MakeNewPtr<CubeMap>();
    newCubeMap->Consume(cubemapRt);

    return newCubeMap;
  }

  CubeMapPtr Renderer::RenderToCubeMap(ForwardSceneRenderPath* renderPath,
                                       const Vec3& position,
                                       float near,
                                       float far,
                                       uint resolution,
                                       const float* perFaceClipDist)
  {
    TK_PROFILE_FUNCTION();

    Stats::BeginGpuScope("RenderToCubeMap");

    // Create cubemap render target.
    const TextureSettings cubeMapSettings = {GraphicTypes::TargetCubeMap,
                                             GraphicTypes::UVClampToEdge,
                                             GraphicTypes::UVClampToEdge,
                                             GraphicTypes::UVClampToEdge,
                                             GraphicTypes::SampleLinearMipmapLinear,
                                             GraphicTypes::SampleLinear,
                                             GraphicTypes::FormatRGBA16F,
                                             GraphicTypes::FormatRGBA,
                                             GraphicTypes::TypeFloat,
                                             MsaaSampleCount::x0,
                                             0,
                                             false};

    RenderTargetPtr cubeMapRt = MakeNewPtr<RenderTarget>(resolution, resolution, cubeMapSettings, "RenderToCubeMapRT");
    cubeMapRt->Init();

    // Create framebuffer for rendering each face.
    FramebufferPtr cubeFb = MakeNewPtr<Framebuffer>("RenderToCubeMapFB");
    cubeFb->ReconstructIfNeeded({(int) resolution, (int) resolution, false, true});

    // Create camera for cubemap capture.
    CameraPtr cam = MakeNewPtr<Camera>();
    cam->SetLens(glm::radians(90.0f), 1.0f, near, far);

    // 6 cubemap face view matrices.
    Mat4 views[]                         = {glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                                            glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                                            glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                                            glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                                            glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
                                            glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};
    // Save original render path params.
    CameraPtr origCam                    = renderPath->m_params.Cam;
    FramebufferPtr origFramebuffer       = renderPath->m_params.MainFramebuffer;

    // Disable all post-processing for cubemap capture.
    // Post-process passes (gamma/tonemap/FXAA, bloom, DoF, SSAO) are incompatible
    // with cubemap face render targets and would corrupt the output.
    PostProcessingSettingsPtr origPPS    = renderPath->m_params.postProcessSettings;
    PostProcessingSettingsPtr capturePPS = MakeNewPtr<PostProcessingSettings>();
    capturePPS->SetTonemappingEnabledVal(false);
    capturePPS->SetGammaCorrectionEnabledVal(false);
    capturePPS->SetFXAAEnabledVal(false);
    capturePPS->SetBloomEnabledVal(false);
    capturePPS->SetSSAOEnabledVal(false);
    capturePPS->SetDepthOfFieldEnabledVal(false);

    renderPath->m_params.postProcessSettings = capturePPS;

    for (int i = 0; i < 6; i++)
    {
      Vec3 pos;
      Quaternion rot;
      Vec3 sca(1.0f);
      DecomposeMatrix(views[i], &pos, &rot, &sca);

      cam->m_node->SetTranslation(position);
      cam->m_node->SetOrientation(rot);
      cam->m_node->SetScale(sca);

      // Set color attachment to the corresponding cubemap face.
      cubeFb->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                 cubeMapRt,
                                 0,
                                 -1,
                                 (Framebuffer::CubemapFace) i);

      // Set per-face far clip distance on the camera.
      if (perFaceClipDist != nullptr)
      {
        cam->SetLens(glm::radians(90.0f), 1.0f, near, perFaceClipDist[i]);
      }

      // Override render path params for this face.
      renderPath->m_params.Cam             = cam;
      renderPath->m_params.MainFramebuffer = cubeFb;

      // Render the scene for this face.
      renderPath->Render(this);
    }

    // Restore original render path params.
    renderPath->m_params.Cam                 = origCam;
    renderPath->m_params.MainFramebuffer     = origFramebuffer;
    renderPath->m_params.postProcessSettings = origPPS;

    // Create the cubemap from the render target.
    CubeMapPtr cubemap                       = MakeNewPtr<CubeMap>();
    cubemap->Consume(cubeMapRt);

    // Generate mip maps for the captured cubemap so that mipmap-filtered sampling
    // in irradiance generation shaders works correctly (incomplete mip chain causes black).
    cubemap->GenerateMipMaps();

    Stats::EndGpuScope();

    return cubemap;
  }

} // namespace ToolKit
