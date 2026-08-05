/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Renderer.h"
#include "InstanceDataBuffer.h"

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

#include <cstring>

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

    FinishPass();
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

    m_gpuProgramManager             = new GpuProgramManager();
    m_gpuProgramManager->SetBackend(m_backend);
    m_gpuProgramManager->SetGpuBuffers(m_globalGpuBuffers);

    String renderer = m_backend->GetBackendRendererString();
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

    // Phase 2a instance-data buffer — 1024-slot budget (per-profile sizing = Phase 3).
    m_instanceBuffer = std::make_unique<InstanceDataBuffer>();
    m_instanceBuffer->Init(1024);
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

    m_gaussianBlurBuffer.Destroy();
    m_gaussianBlurBufferInitialized = false;

    m_cubemapEquirectBuffer.Destroy();
    m_cubemapEquirectBufferInitialized = false;
    m_preFilterEnvMapBuffer.Destroy();
    m_preFilterEnvMapBufferInitialized = false;

    m_gradientSkyboxBuffer.Destroy();
    m_gradientSkyboxBufferInitialized = false;

    SafeDel(m_gpuProgramManager);

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

  void Renderer::SetPassState(const RenderState& state)
  {
    // Copy only passive fields; active fields are sourced from the material at draw time.
    m_passiveState.depthTestEnabled  = state.depthTestEnabled;
    m_passiveState.depthWriteEnabled = state.depthWriteEnabled;
    m_passiveState.depthFunction     = state.depthFunction;
    m_passiveState.stencilOperation  = state.stencilOperation;
    m_passiveState.colorMaskEnabled  = state.colorMaskEnabled;
    m_passiveState.depthClampEnabled = state.depthClampEnabled;
    m_passiveState.blendOverride     = state.blendOverride;
    m_passiveState.blendOverrideFunc = state.blendOverrideFunc;
    m_passiveState.cullOverride      = state.cullOverride;
    m_passiveState.cullOverrideMode  = state.cullOverrideMode;
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
          if (int slot = m_currentProgram->GetTextureSlot("s_skinningPose"); slot != -1)
            SetTexture((ubyte) slot, animTexture);
        }

        // animation to blend.
        if (job.animData.blendAnimation != nullptr)
        {
          animTexture = animPlayer->GetAnimationDataTexture(skel->GetIdVal(), job.animData.blendAnimation->GetIdVal());
          if (int slot = m_currentProgram->GetTextureSlot("s_blendWeights"); slot != -1)
            SetTexture((ubyte) slot, animTexture);
        }
      }
      else
      {
        if (int slot = m_currentProgram->GetTextureSlot("s_skinningPose"); slot != -1)
          SetTexture((ubyte) slot, skel->m_bindPoseTexture);
      }
    };

    // Make sure render data is initialized.
    job.Mesh->Init();
    job.Material->Init();

    // CPU-side state   no backend calls here, all read later by FeedUniforms.
    SetTransforms(job.WorldTransform);
    SetLights(job.lights);
    m_model                 = job.WorldTransform;

    // Compose the draw-time rasterizer state: passive bits come from the per-pass state set via
    // SetPassState, active bits come from the material itself. RenderState is no longer stored
    // on the material — it's transient pipeline data assembled here.
    RenderState state       = m_passiveState;
    state.cullMode          = job.Material->cullMode;
    state.blendFunction     = job.Material->blendFunction;
    state.drawType          = job.Material->drawType;
    state.alphaMaskTreshold = job.Material->alphaMaskTreshold;
    state.lineWidth         = job.Material->lineWidth;

    // Pass-level overrides win over the material's active fields. Used by shadow casters
    // (force blend=NONE) and two-sided translucent two-pass draws (force cull=Front then Back).
    if (m_passiveState.blendOverride)
    {
      state.blendFunction = m_passiveState.blendOverrideFunc;
    }
    if (m_passiveState.cullOverride)
    {
      state.cullMode = m_passiveState.cullOverrideMode;
    }

    if (job.requireCullFlip)
    {
      switch (state.cullMode)
      {
        case CullingType::Front:
          state.cullMode = CullingType::Back;
          break;
        case CullingType::Back:
          state.cullMode = CullingType::Front;
          break;
      }
    }

    m_backend->BindPipeline(m_currentProgram, &state);

    updateAndBindSkinningTextures();

    // Per-draw UBO (populated here, consumed by the legacy fragment path and — in 2a —
    // mirrored into the instance-data texture below). Must run before the Phase 2a block
    // (needs perDrawBuffer.m_data) and before SetMaterial (so SetMaterial can restore
    // slot 0 after Flush() temporarily clobbers it via UpdateTextureRegion).
    FeedUniforms(m_currentProgram, job);

    // Phase 2a instance-data transport proof (see rendering-roadmap.md §Phase 2a).
    // When the flag is on and the draw qualifies (default-PBR vertex shader), write the
    // same PerDrawUboLayout to the instance texture at slot 0, flush, and bind.
    // The vertex shader's TK_INSTANCED=1 variant reads it via LoadInstance(gl_InstanceID=0).
    // Skinned meshes (own skinning vertex path) and shader materials (own program) stay on
    // the legacy per-draw path until Phase 7 / tracked separately.
    //
    // IMPORTANT: Flush() → UpdateTextureRegion temporarily binds the instance texture to
    // slot 0 (GL_TEXTURE0) for the glTexSubImage2D upload. This block must run BEFORE
    // SetMaterial so the material texture bindings (which include s_diffuseColor at slot 0)
    // overwrite this temporary binding before Draw().
    if (m_instancedTransportEnabled &&
        !job.Mesh->IsSkinned() &&
        !job.Material->IsShaderMaterial())
    {
      m_instanceBuffer->Write(0, m_globalGpuBuffers->perDrawBuffer.m_data);

      // CPU-side transport proof: the bytes written to the instance texture must be identical
      // to the UBO bytes the fragment shader reads (both hold the same PerDrawUboLayout struct
      // — one memcpy'd to slot 0 of the RGBA32F texture, one bound as a std140 UBO at slot 2).
      // If this fires, the texture/UBO byte layouts have drifted (e.g. a compiler-padding change
      // that only affects one path, or `Write` wrote to a different offset than `FeedUniforms`).
      assert(memcmp(&m_globalGpuBuffers->perDrawBuffer.m_data,
                    &m_instanceBuffer->Record(0),
                    sizeof(InstanceRecord2a)) == 0 &&
             "Phase 2a transport: instance-texture bytes differ from per-draw UBO");

      m_instanceBuffer->Flush();

      // Phase 2a: full-buffer upload — track the actual bytes sent to the GPU so the
      // UploadedBytes stat reflects the true cost (slot 0 write + full texture re-upload).
      // The per-draw UBO bytes are tracked separately in FeedUniforms.
      Stats::AddStat(FrameStatType::UploadedBytes,
                     static_cast<uint64>(InstanceRecord2aStride) * 16u * 1024u);

      // Per-frame diagnostic: dump model[3] (world translation) from both UBO and instance
      // texture so we can verify the GPU sees the same values. Fires once per session.
      static bool s_dumpedOnce = false;
      if (!s_dumpedOnce)
      {
        s_dumpedOnce = true;
        const PerDrawUboLayout& ubo = m_globalGpuBuffers->perDrawBuffer.m_data;
        const InstanceRecord2a&  tex = m_instanceBuffer->Record(0);
        GetLogger()->Log(
            "[Phase 2a diag] ubo.model[3]=" + std::to_string(ubo.model[3].x) + "," +
            std::to_string(ubo.model[3].y) + "," + std::to_string(ubo.model[3].z) +
            " | tex.model[3]=" + std::to_string(tex.model[3].x) + "," +
            std::to_string(tex.model[3].y) + "," + std::to_string(tex.model[3].z));
      }

      SetTexture(14, m_instanceBuffer->GetTexture());
    }

    // Bind textures AFTER BindPipeline. On Vulkan, BindPipeline resets the per-draw descriptor
    // set, so texture writes before it would be lost. On GL the order is irrelevant (driver slots
    // are independent of the program binding), so this order is safe for both backends.
    // Must also run AFTER the Phase 2a block so it can restore slot 0 (s_diffuseColor) after
    // Flush() temporarily clobbered it via UpdateTextureRegion's BindTextureDirect(..., 0).
    SetMaterial(job.Material);
    SetDataTextures(job);

    // Apply post-pipeline explicit binding for utility passes. Must happen AFTER
    // SetMaterial / SetDataTextures so it can't be overwritten, but BEFORE Draw()
    // where FlushDescriptorState reads the state.
    for (const auto& pair : m_postPipelineTextures)
    {
      if (int slot = m_currentProgram->GetTextureSlot(pair.first.c_str()); slot != -1)
        SetTexture((ubyte) slot, pair.second);
    }

    const Mesh* mesh = job.Mesh;
    DrawDesc desc;
    desc.mesh         = mesh;
    desc.vertexLayout = mesh->m_vertexLayout;
    desc.indexed      = mesh->m_indexCount != 0;
    desc.elementCount = desc.indexed ? mesh->m_indexCount : mesh->m_vertexCount;
    desc.type         = state.drawType;
    m_backend->Draw(desc);

    if (m_framebuffer)
    {
      m_drawnFrameBufferStats[m_framebuffer->GetIdVal()]++;
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
    m_backend->StartPass(desc);

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

  void Renderer::FinishPass() { m_backend->FinishPass(); }

  void Renderer::StartTimerQuery() { m_backend->StartTimerQuery(); }

  void Renderer::EndTimerQuery() { m_backend->EndTimerQuery(); }

  void Renderer::GetElapsedTime(float& cpu, float& gpu) { m_backend->GetElapsedTime(cpu, gpu); }

  FramebufferPtr Renderer::GetFrameBuffer() { return m_framebuffer; }

  void Renderer::ClearColorBuffer(const Vec4& color) { m_backend->ClearColorBuffer(color); }

  void Renderer::ClearBuffer(GraphicBitFields fields, const Vec4& value) { m_backend->ClearBuffer(fields, value); }

  uint64 Renderer::GetNativeTextureHandle(const TexturePtr& tex)
  {
    if (tex == nullptr)
    {
      return 0;
    }
    void* id = GetRenderSystem()->GetBackend()->GetNativeTextureHandle(tex.get());
    return static_cast<uint64>(reinterpret_cast<uintptr_t>(id));
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

    // Renderer is not a Pass, but uses the same passive-state pattern via a function-local
    // static. Initialized once on first call: write nothing to depth, accept every fragment.
    static const RenderState fullQuadPassState = []
    {
      RenderState s;
      s.depthTestEnabled  = false;
      s.depthWriteEnabled = false;
      s.depthFunction     = CompareFunctions::FuncAlways;
      return s;
    }();

    SetPassState(fullQuadPassState);
    RenderWithProgramFromMaterial(jobs);
  }

  void Renderer::DrawCube(CameraPtr cam, MaterialPtr mat, const Mat4& transform)
  {
    TK_PROFILE_FUNCTION();

    m_dummyDrawCube->m_node->SetTransform(transform);
    m_dummyDrawCube->GetMaterialComponent()->SetFirstMaterial(mat);
    SetCamera(cam, true);

    RenderJobArray jobs;
    RenderJobProcessor::CreateRenderJobs(jobs, m_dummyDrawCube);

    // Used for cubemap convolution / equirect baking: accept every fragment.
    static const RenderState drawCubePassState = []
    {
      RenderState s;
      s.depthFunction = CompareFunctions::FuncAlways;
      return s;
    }();

    SetPassState(drawCubePassState);
    RenderWithProgramFromMaterial(jobs);
  }

  void Renderer::CopyTexture(TexturePtr src, TexturePtr dst, bool alphaToOne)
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

    // The copy shader exposes an ALPHA_TO_ONE define that forces the output alpha to 1.0
    // in the fragment stage. SetDefine on the shader recompiles the program (cached) only
    // when the value actually changes, so toggling per-call is cheap.
    ShaderPtr copyFrag = m_copyMaterial->GetFragmentShaderVal();
    copyFrag->SetDefine("ALPHA_TO_ONE", alphaToOne ? "1" : "0");

    DrawFullQuad(m_copyMaterial);
    FinishPass();

    Stats::EndGpuScope();
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
    if (!m_gaussianBlurBufferInitialized)
    {
      m_gaussianBlurBuffer.Init(7);
      m_gaussianBlurBufferInitialized = true;
    }

    m_gaussianBlurMaterial->SetDiffuseTextureVal(src);
    m_gaussianBlurBuffer.m_data.blurScaleAndLayer = Vec4(axis * amount, 0.0f);
    m_gaussianBlurBuffer.m_data.blurClampMinMax   = Vec4(0.0f);
    m_gaussianBlurBuffer.Invalidate();
    m_gaussianBlurBuffer.Map();

    m_oneColorAttachmentFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, dst);

    SetFramebuffer(m_oneColorAttachmentFramebuffer, GraphicBitFields::None);
    DrawFullQuad(m_gaussianBlurMaterial);
    FinishPass();
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
    if (!m_gaussianBlurBufferInitialized)
    {
      m_gaussianBlurBuffer.Init(7);
      m_gaussianBlurBufferInitialized = true;
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

        m_gaussianBlurBuffer.m_data.blurScaleAndLayer = Vec4(blurAmount, 0.0f, 0.0f, (float) layer);
        m_gaussianBlurBuffer.m_data.blurClampMinMax   = Vec4(clampMin, clampMax);
        m_gaussianBlurBuffer.Invalidate();
        m_gaussianBlurBuffer.Map();

        BindProgramOfMaterial(m_gaussianBlurMaterial.get());

        // Bind via the post-pipeline override. A direct m_backend->BindTexture here would be
        // wiped by VulkanBackend::BindPipeline underneath DrawFullQuad, leaving the slot to fall
        // back to the dummy texture. Render(job) re-applies this AFTER BindPipeline so it survives.
        m_postPipelineTextures["s_sourceArray"] = srcArray;

        DrawFullQuad(m_gaussianBlurMaterial);
        FinishPass();

        m_postPipelineTextures.clear();
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
        m_gaussianBlurBuffer.m_data.blurScaleAndLayer = Vec4(0.0f, blurAmount, 0.0f, 0.0f);
        m_gaussianBlurBuffer.m_data.blurClampMinMax   = Vec4(clampMin, clampMax);
        m_gaussianBlurBuffer.Invalidate();
        m_gaussianBlurBuffer.Map();

        DrawFullQuad(m_gaussianBlurMaterial);
        FinishPass();
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
      FinishPass();

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
      if (int slot = m_currentProgram->GetTextureSlot("s_diffuseColor"); slot != -1)
        SetTexture((ubyte) slot, mat->GetDiffuseTextureVal());
    }

    if (cache.EmissiveTextureInUse())
    {
      if (int slot = m_currentProgram->GetTextureSlot("s_emissiveColor"); slot != -1)
        SetTexture((ubyte) slot, mat->GetEmissiveTextureVal());
    }

    if (cache.MetallicRoughnessTextureInUse())
    {
      if (int slot = m_currentProgram->GetTextureSlot("s_metallicRoughness"); slot != -1)
        SetTexture((ubyte) slot, mat->GetMetallicRoughnessTextureVal());
    }

    if (cache.NormalTextureInUse())
    {
      if (int slot = m_currentProgram->GetTextureSlot("s_normalMap"); slot != -1)
        SetTexture((ubyte) slot, mat->GetNormalTextureVal());
    }

    if (mat->IsPBR())
    {
      if (int slot = m_currentProgram->GetTextureSlot("s_brdfLut"); slot != -1)
        SetTexture((ubyte) slot, m_brdfLut);
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

    // BindProgram only stages the program — the actual VkPipeline / GL pipeline binding
    // happens inside Render(job) where the per-job RenderState is known. Standalone draws
    // (e.g. DrawFullQuad) all go through Render(job) too.
    m_currentProgram = program;
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
      if (int slot = m_currentProgram->GetTextureSlot("s_cubeMap"); slot != -1)
        SetTexture((ubyte) slot, mat->m_cubeMap);
    }

    // Sky and Ibl data.
    m_drawCommand.SetIblInUse(false);
    m_drawCommand.SetSkyIntensity(0.0f);
    m_drawCommand.SetVolumeIntensity(0, 0.0f);
    m_drawCommand.SetVolumeIntensity(1, 0.0f);
    m_drawCommand.SetVolumeFadeDistance(0, 0.0f);

    bool anyIbl = false;

    // --- Sky (global fallback) ---
    if (m_sky != nullptr)
    {
      EnvironmentComponentPtr skyEnvCom = m_sky->GetComponent<EnvironmentComponent>();
      if (skyEnvCom != nullptr)
      {
        const HdriPtr& skyHdri = skyEnvCom->GetHdriVal();
        if (skyHdri != nullptr && skyHdri->m_diffuseEnvMap && skyHdri->m_specularEnvMap && m_brdfLut)
        {
          if (int slot = m_currentProgram->GetTextureSlot("s_skyIrradiance"); slot != -1)
            SetTexture((ubyte) slot, skyHdri->m_diffuseEnvMap);
          if (int slot = m_currentProgram->GetTextureSlot("s_skySpecular"); slot != -1)
            SetTexture((ubyte) slot, skyHdri->m_specularEnvMap);

          float skyIntensity = skyEnvCom->GetIlluminateVal() ? skyEnvCom->GetIntensityVal() : 0.0f;
          m_drawCommand.SetSkyIntensity(skyIntensity);
          m_iblRotation = Mat4(m_sky->m_node->GetOrientation());
          anyIbl        = true;
        }
      }
    }

    // --- Local volumes (per-object) ---
    EnvironmentComponent* envCom = job.EnvironmentVolume;
    if (envCom)
    {
      const HdriPtr& hdriPtr     = envCom->GetHdriVal();
      CubeMapPtr& diffuseEnvMap  = hdriPtr->m_diffuseEnvMap;
      CubeMapPtr& specularEnvMap = hdriPtr->m_specularEnvMap;

      if (diffuseEnvMap && specularEnvMap && m_brdfLut)
      {
        if (int slot = m_currentProgram->GetTextureSlot("s_irradianceMap"); slot != -1)
          SetTexture((ubyte) slot, diffuseEnvMap);
        if (int slot = m_currentProgram->GetTextureSlot("s_iblSpecular"); slot != -1)
          SetTexture((ubyte) slot, specularEnvMap);

        anyIbl = true;
        m_drawCommand.SetVolumeIntensity(0, envCom->GetIntensityVal());

        // Pass primary volume local-space BB for OBB per-pixel blend and Parallax Corrected Cubemaps.
        Vec3 offset = envCom->GetPositionOffsetVal();
        Vec3 half   = envCom->GetSizeVal() * 0.5f;
        bool isSky  = false;
        if (const EntityPtr& env = envCom->OwnerEntity())
        {
          isSky = env->IsA<SkyBase>();
        }

        m_drawCommand.SetVolumeMin(0, offset - half);
        m_drawCommand.SetVolumeMax(0, offset + half);
        m_drawCommand.SetVolumePccEnabled(0, envCom->GetParallaxCorrectionVal());
        m_drawCommand.SetVolumeInterior(0, !isSky);
        m_drawCommand.SetVolumeFadeDistance(0, glm::max(envCom->GetFadeVal(), 0.001f));

        // Sky: rotation applies to IBL image, no volume boundary.
        // Non-Sky: rotation applies to OBB volume, IBL image stays fixed.
        if (const EntityPtr& env = envCom->OwnerEntity())
        {
          if (isSky)
          {
            m_iblRotation = Mat4(env->m_node->GetOrientation());
            m_drawCommand.SetVolumeInverseTransform(0, Mat4(1.0f));
            m_drawCommand.SetVolumeWorldTransform(0, Mat4(1.0f));
          }
          else
          {
            m_iblRotation       = Mat4(1.0f);
            Mat4 worldTransform = env->m_node->GetTransform(TransformationSpace::TS_WORLD);
            m_drawCommand.SetVolumeInverseTransform(0, glm::inverse(worldTransform));
            m_drawCommand.SetVolumeWorldTransform(0, worldTransform);
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
            if (int slot = m_currentProgram->GetTextureSlot("s_secondaryIrradiance"); slot != -1)
              SetTexture((ubyte) slot, secDiffuse);
            if (int slot = m_currentProgram->GetTextureSlot("s_secondarySpecular"); slot != -1)
              SetTexture((ubyte) slot, secSpecular);
            m_drawCommand.SetVolumeIntensity(1, secEnvCom->GetIntensityVal());

            Vec3 secOffset = secEnvCom->GetPositionOffsetVal();
            Vec3 secHalf   = secEnvCom->GetSizeVal() * 0.5f;
            m_drawCommand.SetVolumeMin(1, secOffset - secHalf);
            m_drawCommand.SetVolumeMax(1, secOffset + secHalf);
            m_drawCommand.SetVolumePccEnabled(1, secEnvCom->GetParallaxCorrectionVal());
            m_drawCommand.SetVolumeInterior(1, secEnvCom->GetInteriorVal());
            m_drawCommand.SetVolumeFadeDistance(1, glm::max(secEnvCom->GetFadeVal(), 0.001f));

            if (const EntityPtr& secEnv = secEnvCom->OwnerEntity())
            {
              m_secondaryIblRotation = Mat4(1.0f);
              Mat4 secWorldTransform = secEnv->m_node->GetTransform(TransformationSpace::TS_WORLD);
              m_drawCommand.SetVolumeInverseTransform(1, glm::inverse(secWorldTransform));
              m_drawCommand.SetVolumeWorldTransform(1, secWorldTransform);
            }
          }
        }
      }
    }

    // Mark IBL in use if sky or any local volume contributed.
    if (anyIbl)
    {
      m_drawCommand.SetIblInUse(true);
      if (int slot = m_currentProgram->GetTextureSlot("s_brdfLut"); slot != -1)
        SetTexture((ubyte) slot, m_brdfLut);
    }

    // AO texture.
    if (m_ambientOcculusionInUse)
    {
      if (int slot = m_currentProgram->GetTextureSlot("s_ambientOcclusion"); slot != -1)
        SetTexture((ubyte) slot, m_aoTexture);
    }

    // Bind shadow map if activated.
    if (m_shadowAtlas != nullptr)
    {
      if (int slot = m_currentProgram->GetTextureSlot("s_shadowAtlas"); slot != -1)
        SetTexture((ubyte) slot, m_shadowAtlas);
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

    static_assert(RHIConstants::MaxPointLightPerObject == 24, "PerDrawUboLayout assumes 24 point indices");
    static_assert(RHIConstants::MaxSpotLightPerObject == 24, "PerDrawUboLayout assumes 24 spot indices");

    PerDrawUboLayout& ubo     = m_globalGpuBuffers->perDrawBuffer.m_data;
    ubo.model                 = m_model;
    ubo.modelWithoutTranslate = m_modelWithoutTranslate;
    ubo.inverseModel          = m_inverseModel;
    ubo.inverseTransposeModel = m_inverseTransposeModel;
    ubo.iblRotation           = m_iblRotation;
    ubo.iblSecondaryRotation  = m_secondaryIblRotation;
    ubo.viewportSizeAndPad    = Vec4((float) m_viewportRect.x, (float) m_viewportRect.y, 0.0f, 0.0f);
    ubo.drawCommand           = m_drawCommand;
    ubo.materialData          = job.Material->GetCacheItem().data;

    // Pack the 24 ints into 6 ivec4. std140 would otherwise grow each int to 16 bytes.
    std::memcpy(ubo.activePointLightIndices, m_activePointLightIndices.data(), sizeof(int) * 24);
    std::memcpy(ubo.activeSpotLightIndices, m_activeSpotLightIndices.data(), sizeof(int) * 24);
    ubo.lightCounts   = IVec4(m_activePointLightCount, m_activeSpotLightCount, 0, 0);

    // Animation / skinning
    Vec4 keyFrameData = Vec4(0.0f);
    if (job.animData.currentAnimation != nullptr)
    {
      keyFrameData = Vec4(job.animData.firstKeyFrame,
                          job.animData.secondKeyFrame,
                          job.animData.keyFrameInterpolationTime,
                          job.animData.keyFrameCount);
    }
    ubo.keyFrameData    = keyFrameData;

    Vec4 blendFrameData = Vec4(0.0f);
    if (job.animData.blendAnimation != nullptr)
    {
      blendFrameData = Vec4(job.animData.blendFirstKeyFrame,
                            job.animData.blendSecondKeyFrame,
                            job.animData.blendKeyFrameInterpolationTime,
                            job.animData.blendKeyFrameCount);
    }
    ubo.blendFrameData = blendFrameData;

    Vec4 skinParams    = Vec4(0.0f);
    if (job.Mesh->IsSkinned())
    {
      const SkeletonPtr& skel = static_cast<const SkinMesh*>(job.Mesh)->m_skeleton;
      if (skel)
      {
        float boneCount  = (float) skel->m_bones.size();
        float isAnimated = (job.animData.currentAnimation != nullptr) ? 1.0f : 0.0f;
        float hasBlend   = (job.animData.blendAnimation != nullptr) ? 1.0f : 0.0f;
        skinParams       = Vec4(boneCount, 1.0f, isAnimated, hasBlend);
      }
    }
    ubo.skinParams            = skinParams;
    ubo.animBlendFactorAndPad = Vec4(job.animData.animationBlendFactor, 0.0f, 0.0f, 0.0f);

    m_globalGpuBuffers->perDrawBuffer.Invalidate();
    m_globalGpuBuffers->perDrawBuffer.Map();

    // Hand the same blob to the backend. GL is a no-op (UBO is already bound + mapped); Vulkan
    // copies it into the per-frame dynamic-offset ring slot the descriptor set points at.
    m_backend->SubmitPerDrawData(&ubo, sizeof(ubo));

    // Phase 1: track per-draw UBO upload bytes (baseline; Phase 4 upload reduction target).
    Stats::AddStat(FrameStatType::UploadedBytes, sizeof(ubo));
  }

  void Renderer::SetTexture(ubyte slotIndx, TexturePtr texture)
  {
    assert(slotIndx < RHIConstants::TextureSlotCount && "You exceed texture slot count");
    m_backend->BindTexture(slotIndx, texture);
  }

  void Renderer::SetTexture(const char* semanticName, TexturePtr texture)
  {
    // The previous version silently no-op'd when m_currentProgram was null, which is exactly
    // what hid the SSAO bug: SetTexture("s_normalDepth", ...) ran before the program was
    // bound and the call vanished. Now we fail loudly — the new entry point is
    // PassRequirements::semanticTextures, which guarantees program-bind happens first.
    if (m_currentProgram == nullptr)
    {
      TK_ERR("Renderer::SetTexture(\"%s\") called with no current program. "
             "Use PassRequirements::semanticTextures or bind the program first.",
             semanticName);
      return;
    }
    int slot = m_currentProgram->GetTextureSlot(semanticName);
    if (slot == -1)
    {
      TK_WRN("Renderer::SetTexture: current program has no resource named \"%s\".", semanticName);
      return;
    }
    SetTexture((ubyte) slot, texture);
  }

  void Renderer::BindUniformBuffer(int slot, UniformBuffer* ubo)
  {
    assert(ubo != nullptr);
    assert(ubo->m_slot == slot && "UBO m_slot must match the requested binding slot");
    if (ubo->m_slot == ReservedUniformBufferSlots::PerDrawData)
    {
      // Per-draw UBO is uploaded through SubmitPerDrawData; backend reads it from the
      // dynamic-offset ring, not from BindUniformBuffer.
      return;
    }
    m_backend->BindUniformBuffer(ubo, slot);
  }

  void Renderer::SetShadowAtlas(TexturePtr shadowAtlas) { m_shadowAtlas = shadowAtlas; }

  CubeMapPtr Renderer::GenerateCubemapFrom2DTexture(TexturePtr texture,
                                                    uint size,
                                                    float exposure,
                                                    GraphicTypes minFilter)
  {
    Stats::BeginGpuScope("GenerateCubemapFrom2DTexture");

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
    mat->cullMode = CullingType::TwoSided;
    mat->Init();

    if (!m_cubemapEquirectBufferInitialized)
    {
      m_cubemapEquirectBuffer.Init(7);
      m_cubemapEquirectBufferInitialized = true;
    }
    m_cubemapEquirectBuffer.m_data.exposureAndPad = Vec4(exposure, 0.0f, 0.0f, 0.0f);
    m_cubemapEquirectBuffer.m_data.lodLevelAndPad = IVec4(0);
    m_cubemapEquirectBuffer.Invalidate();
    m_cubemapEquirectBuffer.Map();

    m_oneColorAttachmentFramebuffer->ReconstructIfNeeded({(int) size, (int) size, false, false});

    // Views for 6 different angles
    CameraPtr cam = MakeNewPtr<Camera>();
    cam->SetLens(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
#ifdef TK_VULKAN
    Mat4 views[] = {glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f))};
#else // opengl
    Mat4 views[] = {glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};
#endif

    for (int i = 0; i < 6; i++)
    {
      Vec3 pos, sca;
      Quaternion rot;

      Mat4 invView = glm::inverse(views[i]);
      DecomposeMatrix(invView, &pos, &rot, &sca);

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
      FinishPass();
    }

    CubeMapPtr cubeMap = MakeNewPtr<CubeMap>();
    cubeMap->Consume(cubeMapRt);

    Stats::EndGpuScope();

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

    if (!m_cubemapEquirectBufferInitialized)
    {
      m_cubemapEquirectBuffer.Init(7);
      m_cubemapEquirectBufferInitialized = true;
    }
    m_cubemapEquirectBuffer.m_data.exposureAndPad = Vec4(exposure, 0.0f, 0.0f, 0.0f);
    m_cubemapEquirectBuffer.m_data.lodLevelAndPad = IVec4(level, 0, 0, 0);
    m_cubemapEquirectBuffer.Invalidate();
    m_cubemapEquirectBuffer.Map();

    DrawFullQuad(cubeToEquiRect);
    FinishPass();

    if (pixels != nullptr)
    {
      uint64 requiredSize = mipWidth * mipHeight * 4 * sizeof(float);
      *pixels             = new float[requiredSize];
      m_backend->ReadPixels(0, 0, mipWidth, mipHeight, GraphicTypes::FormatRGBA, GraphicTypes::TypeFloat, *pixels);
    }

    SetFramebuffer(prevBuffer, GraphicBitFields::None);
    FinishPass();

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
    Stats::BeginGpuScope("GenerateDiffuseEnvMap");

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
#ifdef TK_VULKAN
    Mat4 views[] = {glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f))};
#else // opengl
    Mat4 views[] = {glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};
#endif

    // Create material
    MaterialPtr mat = MakeNewPtr<Material>();
    ShaderPtr vert  = GetShaderManager()->Create<Shader>(ShaderPath("irradianceGenerateVert.shader", true));
    ShaderPtr frag  = GetShaderManager()->Create<Shader>(ShaderPath("irradianceGenerateFrag.shader", true));

    mat->m_cubeMap  = cubemap;
    mat->SetFragmentShaderVal(frag);
    mat->SetVertexShaderVal(vert);
    mat->cullMode = CullingType::TwoSided;
    mat->Init();

    m_oneColorAttachmentFramebuffer->ReconstructIfNeeded({size, size, false, false});

    for (int i = 0; i < 6; i++)
    {
      Vec3 pos;
      Quaternion rot;
      Vec3 sca;
      Mat4 invView = glm::inverse(views[i]);
      DecomposeMatrix(invView, &pos, &rot, &sca);

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
      FinishPass();
    }

    SetFramebuffer(nullptr, GraphicBitFields::None);
    FinishPass();

    CubeMapPtr newCubeMap = MakeNewPtr<CubeMap>();
    newCubeMap->Consume(cubeMapRt);

    Stats::EndGpuScope();

    return newCubeMap;
  }

  CubeMapPtr Renderer::GenerateSpecularEnvMap(CubeMapPtr cubemap, int size, int mipMaps)
  {
    Stats::BeginGpuScope("GenerateSpecularEnvMap");

    const TextureSettings set  = {GraphicTypes::TargetCubeMap,
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
    size                       = glm::min(size, cubemap->m_width);

    TextureSettings cubemapSet = set;
    cubemapSet.GenerateMipMap  = true;
    RenderTargetPtr cubemapRt  = MakeNewPtr<RenderTarget>(size, size, cubemapSet);
    cubemapRt->Init();

    // Intentionally creating space to fill later. ( mip maps will be calculated for specular ibl )
    cubemapRt->GenerateMipMaps();

    // Views for 6 different angles
    CameraPtr cam = MakeNewPtr<Camera>();
    cam->SetLens(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
#ifdef TK_VULKAN
    Mat4 views[] = {glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f))};
#else
    Mat4 views[] = {glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};
#endif

    // Create material
    MaterialPtr mat = MakeNewPtr<Material>();
    ShaderPtr vert  = GetShaderManager()->Create<Shader>(ShaderPath("positionVert.shader", true));
    ShaderPtr frag  = GetShaderManager()->Create<Shader>(ShaderPath("preFilterEnvMapFrag.shader", true));

    mat->m_cubeMap  = cubemap;
    mat->SetFragmentShaderVal(frag);
    mat->SetVertexShaderVal(vert);
    mat->cullMode = CullingType::TwoSided;
    mat->Init();

    assert(size >= 128 && "Due to RHIConstants::SpecularIBLLods, it can't be lower than this resolution.");
    for (int mip = 0; mip < mipMaps; mip++)
    {
      Stats::BeginGpuScope("SpecularEnvMapMip" + std::to_string(mip));

      int mipSize = (int) (size * std::powf(0.5f, (float) mip));

      m_oneColorAttachmentFramebuffer->ReconstructIfNeeded({mipSize, mipSize, false, false});

      // Create a temporary cubemap for each mipmap level
      RenderTargetPtr mipCubeRt = MakeNewPtr<RenderTarget>(mipSize, mipSize, set);
      mipCubeRt->Init();

      for (int i = 0; i < 6; ++i)
      {
        Vec3 pos;
        Quaternion rot;
        Vec3 sca;
        Mat4 invView = glm::inverse(views[i]);
        DecomposeMatrix(invView, &pos, &rot, &sca);

        cam->m_node->SetTranslation(ZERO, TransformationSpace::TS_WORLD);
        cam->m_node->SetOrientation(rot, TransformationSpace::TS_WORLD);
        cam->m_node->SetScale(sca);

        m_oneColorAttachmentFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                                            mipCubeRt,
                                                            0,
                                                            -1,
                                                            (Framebuffer::CubemapFace) i);

        SetFramebuffer(m_oneColorAttachmentFramebuffer, GraphicBitFields::None);

        if (!m_preFilterEnvMapBufferInitialized)
        {
          m_preFilterEnvMapBuffer.Init(7);
          m_preFilterEnvMapBufferInitialized = true;
        }
        m_preFilterEnvMapBuffer.m_data.params = Vec4((float) mipSize, (float) mip / (float) (mipMaps - 1), 0.0f, 0.0f);
        m_preFilterEnvMapBuffer.Invalidate();
        m_preFilterEnvMapBuffer.Map();

        m_backend->BindTexture(0, cubemap);

        DrawCube(cam, mat);
        FinishPass();

        // Copy color attachment to cubemap's correct mip level and face.
        m_backend->CopyCubemapFaceFromFramebuffer(cubemapRt.get(),
                                                  i,
                                                  mip,
                                                  mipSize,
                                                  mipSize,
                                                  m_oneColorAttachmentFramebuffer.get(),
                                                  nullptr);
      }

      Stats::EndGpuScope();
    }

    SetFramebuffer(nullptr, GraphicBitFields::None);
    FinishPass();

    // Clamp texture max mip level to the last bake level.
    m_backend->SetTextureMaxMipLevel(cubemapRt.get(), mipMaps - 1);

    CubeMapPtr newCubeMap = MakeNewPtr<CubeMap>();
    newCubeMap->Consume(cubemapRt);

    Stats::EndGpuScope();

    return newCubeMap;
  }

  CubeMapPtr Renderer::RenderToCubeMap(ForwardSceneRenderPath* renderPath,
                                       const Mat4& worldTransform,
                                       const Vec3& originOffset,
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
#ifdef TK_VULKAN
    Mat4 views[] = {glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f))};
#else // opengl
    Mat4 views[] = {glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
                    glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};
#endif
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
      Mat4 invView = glm::inverse(views[i]);
      DecomposeMatrix(invView, &pos, &rot, &sca);

      Vec3 capturePos = Vec3(worldTransform * Vec4(originOffset, 1.0f));
      cam->m_node->SetTranslation(capturePos);
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

  // GlobalGpuBuffers
  //////////////////////////////////////////

  void GlobalGpuBuffers::InitGlobalGpuBuffers()
  {
    graphicConstantBuffer.Init();
    cameraGpuBuffer.Init();
    directionalLightBuffer.Init();
    pointLighBuffer.Init();
    spotLightBuffer.Init();
    perDrawBuffer.Init();

    // Set global buffers to reserved slots.
    cameraGpuBuffer.GetBuffer().m_slot              = ReservedUniformBufferSlots::CameraData;
    graphicConstantBuffer.GetBuffer().m_slot        = ReservedUniformBufferSlots::GraphicConstantsData;
    perDrawBuffer.GetBuffer().m_slot                = ReservedUniformBufferSlots::PerDrawData;
    directionalLightBuffer.m_lightDataBuffer.m_slot = ReservedUniformBufferSlots::DirectionalLightBuffer;
    directionalLightBuffer.m_pvms.m_slot            = ReservedUniformBufferSlots::DirectionalLightPVMBuffer;
    pointLighBuffer.m_gpuBuffer.m_slot              = ReservedUniformBufferSlots::PointLightCache;
    spotLightBuffer.m_gpuBuffer.m_slot              = ReservedUniformBufferSlots::SpotLightCache;

    // Fill lookup table.
    m_bufferTable[0] = {"CameraData", ReservedUniformBufferSlots::CameraData, &cameraGpuBuffer.GetBuffer()};
    m_bufferTable[1] = {"GraphicConstatsData",
                        ReservedUniformBufferSlots::GraphicConstantsData,
                        &graphicConstantBuffer.GetBuffer()};
    m_bufferTable[2] = {"PerDrawData", ReservedUniformBufferSlots::PerDrawData, &perDrawBuffer.GetBuffer()};
    m_bufferTable[3] = {"DirectionalLightBuffer",
                        ReservedUniformBufferSlots::DirectionalLightBuffer,
                        &directionalLightBuffer.m_lightDataBuffer};
    m_bufferTable[4] = {"DirectionalLightPVMBuffer",
                        ReservedUniformBufferSlots::DirectionalLightPVMBuffer,
                        &directionalLightBuffer.m_pvms};
    m_bufferTable[5] = {"PointLightCache", ReservedUniformBufferSlots::PointLightCache, &pointLighBuffer.m_gpuBuffer};
    m_bufferTable[6] = {"SpotLightCache", ReservedUniformBufferSlots::SpotLightCache, &spotLightBuffer.m_gpuBuffer};
  }

  const GlobalBufferInfo* GlobalGpuBuffers::FindGlobalBufferInfo(const char* blockName) const
  {
    for (int i = 0; i < ReservedUniformBufferSlots::GlobalBufferCount; ++i)
    {
      if (strcmp(blockName, m_bufferTable[i].name) == 0)
        return &m_bufferTable[i];
    }
    return nullptr;
  }

} // namespace ToolKit
