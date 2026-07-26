/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "ShadowPass.h"

#include "Camera.h"
#include "DirectionComponent.h"
#include "Light.h"
#include "Logger.h"
#include "Material.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "RHI.h"
#include "RenderSystem.h"
#include "Scene.h"
#include "Stats.h"
#include "ToolKit.h"

#include "DebugNew.h"

namespace ToolKit
{

  ShadowPass::ShadowPass() : Pass("ShadowPass")
  {
    // Order must match with TextureUtil.shader::UVWToUVLayer
    Mat4 views[CubemapFaceCount];
    GetCubemapViews(ZERO, views);

    for (int i = 0; i < CubemapFaceCount; i++)
    {
      DecomposeMatrix(views[i], nullptr, &m_cubeMapRotations[i], nullptr);
    }

    m_shadowAtlas               = MakeNewPtr<RenderTarget>("ShadowAtlassRT");
    m_shadowFramebuffer         = MakeNewPtr<Framebuffer>("ShadowPassFB");

    // Create shadow material
    auto createShadowMaterialFn = [](StringView vertexShader, StringView fragmentShader) -> MaterialPtr
    {
      ShaderPtr vert       = GetShaderManager()->Create<Shader>(ShaderPath(vertexShader.data(), true));
      ShaderPtr frag       = GetShaderManager()->Create<Shader>(ShaderPath(fragmentShader.data(), true));

      MaterialPtr material = MakeNewPtr<Material>();
      material->SetFragmentShaderVal(frag);
      material->SetVertexShaderVal(vert);
      material->blendFunction = BlendFunction::NONE;
      material->Init();

      return material;
    };

    m_shadowMatOrtho             = createShadowMaterialFn("orthogonalDepthVert.shader", "orthogonalDepthFrag.shader");
    m_shadowMatPersp             = createShadowMaterialFn("perspectiveDepthVert.shader", "perspectiveDepthFrag.shader");

    // Pass-owned passive defaults. depthClampEnabled is refreshed per camera type before the
    // caster loop in RenderShadowCasters (true for directional, false otherwise).
    m_passState.depthTestEnabled = true;
    m_passState.depthWriteEnabled = true;

    // Shadow casters always render with blending off, regardless of what the material declares.
    // Pass-level override is the clean replacement for the old per-job blendFunction mutation.
    m_passState.blendOverride     = true;
    m_passState.blendOverrideFunc = BlendFunction::NONE;
  }

  ShadowPass::ShadowPass(const ShadowPassParams& params) : ShadowPass() { m_params = params; }

  ShadowPass::~ShadowPass() {}

  void ShadowPass::Render()
  {
    TK_PROFILE_FUNCTION();

    if (m_lights.empty())
    {
      return;
    }

    Renderer* renderer        = GetRenderer();
    const Vec4 lastClearColor = renderer->m_clearColor;

    // Clear every atlas layer to the VSM neutral value. Per-layer FB + LOAD_OP_CLEAR variant
    // — each iteration opens an empty render pass for that layer's view, the GPU clears at RP
    // entry (free with HiZ/HiS), no SetColorAttachment churn.
    for (int layer = 0; layer < ShadowAtlas::LayerCount; ++layer)
    {
      renderer->SetFramebuffer(m_shadowFramebuffers[layer], GraphicBitFields::ColorBits, m_shadowClearColor);
      renderer->FinishPass();
    }

    // Main render loop, grouped by the first atlas layer each light occupies.
    for (int layerIndex = 0; layerIndex < (int) m_atlasLayerSwitchIndices.size(); layerIndex++)
    {
      int begin = m_atlasLayerSwitchIndices[layerIndex];
      int end   = (int) m_lights.size();
      if (layerIndex + 1 < (int) m_atlasLayerSwitchIndices.size())
      {
        end = m_atlasLayerSwitchIndices[layerIndex + 1];
      }

      // Open the pass at the first cascade's layer + clear the shared depth via LOAD_OP_CLEAR.
      // Subsequent cascades that live in a different layer trigger a pass switch inside
      // RenderShadowMaps (depth is preserved across switches via LOAD_OP_LOAD since the same
      // depth attachment is shared by every per-layer FB).
      const int firstLayer = m_lights[begin]->m_shadowAtlasLayers[0];
      renderer->SetFramebuffer(m_shadowFramebuffers[firstLayer], GraphicBitFields::DepthBits);
      m_currentRenderLayer = firstLayer;

      for (int i = begin; i < end; i++)
      {
        m_lights[i]->UpdateShadowCamera();
        RenderShadowMaps(m_lights[i]);
      }

      renderer->FinishPass();
      m_currentRenderLayer = -1;
    }

    // Apply blur to the shadow atlas.
    BlurShadowAtlas();

    renderer->m_clearColor = lastClearColor;

    // Phase 1: track shadow redraw count (baseline; Phase 5 caching will drop this).
    Stats::AddStat(FrameStatType::ShadowRedrawCount, (uint64) m_lights.size());
  }

  void ShadowPass::PreRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PreRender();

    ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    if (shadows->GetUseParallelSplitPartitioningVal())
    {
      float minDistance     = shadows->GetShadowMinDistanceVal();
      float maxDistance     = shadows->GetShadowMaxDistance();
      float lambda          = shadows->GetParallelSplitLambdaVal();

      float nearClip        = m_params.viewCamera->Near();
      float farClip         = m_params.viewCamera->Far();
      float clipRange       = farClip - nearClip;

      float minZ            = nearClip + minDistance * clipRange;
      float maxZ            = nearClip + maxDistance * clipRange;

      float range           = maxZ - minZ;
      float ratio           = maxZ / minZ;

      int cascadeCount      = shadows->GetCascadeCountVal();
      Vec4 cascadeDistances = shadows->GetCascadeDistancesVal();
      for (int i = 0; i < cascadeCount; i++)
      {
        float p             = (i + 1) / (float) (cascadeCount);
        float log           = minZ * std::pow(ratio, p);
        float uniform       = minZ + range * p;
        float d             = lambda * (log - uniform) + uniform;
        cascadeDistances[i] = (d - nearClip) / clipRange;
      }
      shadows->SetCascadeDistancesVal(cascadeDistances);
    }

    Renderer* renderer = GetRenderer();

    // Dropout non shadow casting lights.
    m_lights           = m_params.lights;
    erase_if(m_lights, [](Light* light) -> bool { return !light->GetCastShadowVal(); });

    InitShadowAtlas();

    erase_if(m_lights, [](Light* light) -> bool { return !light->HasValidShadowSlot(); });

    // Group lights by their first atlas layer and record layer switch indices.
    m_atlasLayerSwitchIndices.clear();
    auto it = m_lights.begin();
    for (int layer = 0; layer < ShadowAtlas::LayerCount; layer++)
    {
      auto next = std::partition(it, m_lights.end(), [layer](Light* l) { return l->m_shadowAtlasLayers[0] == layer; });
      if (next != it)
      {
        m_atlasLayerSwitchIndices.push_back((int) std::distance(m_lights.begin(), it));
      }
      it = next;
    }
  }

  void ShadowPass::PostRender()
  {
    TK_PROFILE_FUNCTION();

    Pass::PostRender();

    // Remap due to updated shadow matrices.
    LightRawPtrArray dlights;
    for (Light* l : m_params.lights)
    {
      if (l->GetLightType() == Light::Directional)
      {
        dlights.push_back(l);
      }
    }
    Renderer* renderer = GetRenderer();
    renderer->SetDirectionalLights(dlights);
    renderer->FinishPass();
  }

  RenderTargetPtr ShadowPass::GetShadowAtlas() { return m_shadowAtlas; }

  void ShadowPass::RenderShadowMaps(Light* light)
  {
    TK_PROFILE_FUNCTION();

    // Skip lights that didn't get valid atlas slots.
    if (!light->HasValidShadowSlot())
    {
      return;
    }

    Renderer* renderer        = GetRenderer();
    ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    uint resolution           = (uint) light->m_shadowResolution;

    // Switches the active pass to the per-layer framebuffer matching @p targetLayer if it isn't
    // already current. Depth attachment is shared across all per-layer FBs, so closing and
    // reopening preserves the in-progress depth content (LOAD_OP_LOAD).
    auto switchToLayer        = [&](int targetLayer)
    {
      if (targetLayer == m_currentRenderLayer)
      {
        return;
      }
      renderer->FinishPass();
      renderer->SetFramebuffer(m_shadowFramebuffers[targetLayer], GraphicBitFields::None);
      m_currentRenderLayer = targetLayer;
    };

    if (light->GetLightType() == Light::LightType::Directional)
    {
      Stats::BeginGpuScope("Directioal Shadow Map");
      DirectionalLight* dLight = static_cast<DirectionalLight*>(light);
      dLight->UpdateShadowFrustum(m_params.viewCamera, m_params.scene);

      int cascadeCount = shadows->GetCascadeCountVal();
      for (int i = 0; i < cascadeCount; i++)
      {
        switchToLayer(dLight->m_shadowAtlasLayers[i]);

        UVec2 coord = dLight->m_shadowAtlasCoords[i];
        renderer->SetViewportRect(coord.x, coord.y, resolution, resolution);

        RenderShadowCasters(light, dLight->m_cascadeShadowCameras[i], dLight->m_cascadeCullCameras[i]);
      }

      Stats::EndGpuScope();
    }
    else if (light->GetLightType() == Light::LightType::Point)
    {
      Stats::BeginGpuScope("Point Shadow Map");
      for (int i = 0; i < 6; i++)
      {
        switchToLayer(light->m_shadowAtlasLayers[i]);

        light->m_shadowCamera->m_node->SetTranslation(light->m_node->GetTranslation());
        light->m_shadowCamera->m_node->SetOrientation(m_cubeMapRotations[i]);

        UVec2 coord = light->m_shadowAtlasCoords[i];
        renderer->SetViewportRect(coord.x, coord.y, resolution, resolution);

        RenderShadowCasters(light, light->m_shadowCamera, light->m_shadowCamera);
      }
      Stats::EndGpuScope();
    }
    else
    {
      assert(light->GetLightType() == Light::LightType::Spot);

      Stats::BeginGpuScope("Spot Shadow Map");
      switchToLayer(light->m_shadowAtlasLayers[0]);

      UVec2 coord = light->m_shadowAtlasCoords[0];

      renderer->SetViewportRect(coord.x, coord.y, resolution, resolution);
      RenderShadowCasters(light, light->m_shadowCamera, light->m_shadowCamera);
      Stats::EndGpuScope();
    }
  }

  void ShadowPass::RenderShadowCasters(Light* light, CameraPtr shadowCamera, CameraPtr cullCamera)
  {
    TK_PROFILE_FUNCTION();

    Renderer* renderer = GetRenderer();

    // Adjust light's camera.
    renderer->SetCamera(shadowCamera, false);

    Light::LightType lightType = light->GetLightType();
    if (lightType == Light::LightType::Directional)
    {
      // Here we will try to find a distance that covers all shadow casters.
      // Shadow camera placed at the outer bounds of the scene to find all shadow casters.
      // The frustum is only used to find potential shadow casters.
      // The tight bounds of the shadow camera which is used to create the shadow map is preserved.
      // The casters that will fall behind the camera will still cast shadows, this is why all the fuss for.
      // In the shader, the objects that fall behind the camera is "pancaked" to shadow camera's front plane.
      const BoundingBox& sceneBox = m_params.scene->GetSceneBoundary();
      Vec3 dir                    = cullCamera->Direction();
      Vec3 pos                    = cullCamera->Position(); // Backup pos.
      Vec3 outerPoint             = pos - dir * glm::distance(sceneBox.min, sceneBox.max) * 0.5f;

      cullCamera->m_node->SetTranslation(outerPoint); // Set the camera position.
      cullCamera->SetNearClipVal(0.0f);

      // New far clip is calculated. Its the distance newly calculated outer poi
      cullCamera->SetFarClipVal(glm::distance(outerPoint, pos) + cullCamera->Far());
    }

    // Create render jobs for shadow map generation.
    RenderData renderData;

    Frustum frustum            = ExtractFrustum(cullCamera->GetProjectViewMatrix(), false);
    EntityRawPtrArray entities = m_params.scene->m_aabbTree.VolumeQuery(frustum);

    // Remove non shadow casters.
    erase_if(entities,
             [](Entity* ntt) -> bool
             {
               if (MeshComponent* mc = ntt->GetComponentFast<MeshComponent>())
               {
                 return !mc->GetCastShadowVal();
               }

               return false;
             });

    RenderJobProcessor::CreateRenderJobs(renderData.jobs, entities);
    RenderJobProcessor::SeperateRenderData(renderData);

    // Set material and program.
    bool orthogonalShadowMap   = lightType == Light::LightType::Directional;
    MaterialPtr shadowMaterial = orthogonalShadowMap ? m_shadowMatOrtho : m_shadowMatPersp;
    ShaderPtr frag             = shadowMaterial->GetFragmentShaderVal();
    frag->SetDefine("DrawAlphaMasked", "0");
    ShaderPtr vert = shadowMaterial->GetVertexShaderVal();
    vert->SetDefine("DrawAlphaMasked", "0");

    GpuProgramManager* gpuProgramManager = renderer->GetGpuProgramManager();
    m_program                            = gpuProgramManager->CreateProgram(vert, frag);
    renderer->BindProgram(m_program);

    if (orthogonalShadowMap)
    {
      if (renderer->IsDepthClampSupported())
      {
        vert->SetDefine("Pancake", "0");
        frag->SetDefine("Pancake", "0");
      }
      else
      {
        vert->SetDefine("Pancake", "1");
        frag->SetDefine("Pancake", "1");
      }
    }

    RenderJobItr forwardBegin       = renderData.GetForwardOpaqueBegin();
    RenderJobItr forwardMaskedBegin = renderData.GetForwardAlphaMaskedBegin();
    RenderJobItr translucentBegin   = renderData.GetForwardTranslucentBegin();

    // Refresh the param-dependent passive field on the pass-owned state before the loop:
    // depth clamp is on only for orthogonal (directional) shadow cameras.
    m_passState.depthClampEnabled   = orthogonalShadowMap;
    renderer->SetPassState(m_passState);

    // Draw opaque.
    for (RenderJobItr jobItr = forwardBegin; jobItr < forwardMaskedBegin; jobItr++)
    {
      renderer->Render(*jobItr);
    }

    // Draw alpha masked.
    frag->SetDefine("DrawAlphaMasked", "1");
    vert->SetDefine("DrawAlphaMasked", "1");
    m_program = gpuProgramManager->CreateProgram(vert, frag);
    renderer->BindProgram(m_program);

    for (RenderJobItr jobItr = forwardMaskedBegin; jobItr < translucentBegin; jobItr++)
    {
      renderer->Render(*jobItr);
    }

    // Translucent shadow is not supported.
  }

  void ShadowPass::BlurShadowAtlas()
  {
    TK_PROFILE_FUNCTION();
    Stats::BeginGpuScope("Shadow Blur");

    Renderer* renderer = GetRenderer();

    // Note: depth-write is disabled per-fullscreen-quad inside DrawFullQuad, so no global
    // toggle is needed here.

    // Create temp RT for blur ping-pong if needed.
    if (m_shadowBlurTempRT == nullptr)
    {
      m_shadowBlurTempRT = MakeNewPtr<RenderTarget>("ShadowBlurTempRT");
    }

    GraphicTypes bufferComponents = GraphicTypes::FormatRG;
    GraphicTypes bufferFormat     = GraphicTypes::FormatRG16F;

    GraphicTypes sampler          = GraphicTypes::SampleLinear;
    if (!renderer->GetBackend()->SupportsFloatTextureLinearFilter())
    {
      sampler = GraphicTypes::SampleNearest;
    }

    const TextureSettings tempSet = {GraphicTypes::Target2D,
                                     GraphicTypes::UVClampToEdge,
                                     GraphicTypes::UVClampToEdge,
                                     GraphicTypes::UVClampToEdge,
                                     sampler,
                                     sampler,
                                     bufferFormat,
                                     bufferComponents,
                                     GraphicTypes::TypeFloat,
                                     MsaaSampleCount::x0,
                                     0,
                                     false};

    ShadowSettingsPtr shadows     = GetEngineSettings().m_graphics->m_shadows;
    const int shadowAtlasSize     = shadows->GetShadowAtlasResolution();

    m_shadowBlurTempRT->ReconstructIfNeeded(shadowAtlasSize, shadowAtlasSize, &tempSet);

    const int kernelSize = shadows->GetVSMBlurKernelSizeVal().GetValue<int>();
    const int tapCount   = shadows->GetVSMBlurTapCountVal().GetValue<int>();
    const float amount   = 1.0f;

    if (tapCount <= 0)
    {
      Stats::EndGpuScope();
      return;
    }

    const int cascadeCount = shadows->GetCascadeCountVal();

    // Blur each light's shadow slots grouped by atlas layer.
    for (int layerIndex = 0; layerIndex < (int) m_atlasLayerSwitchIndices.size(); layerIndex++)
    {
      int begin = m_atlasLayerSwitchIndices[layerIndex];
      int end   = (int) m_lights.size();
      if (layerIndex + 1 < (int) m_atlasLayerSwitchIndices.size())
      {
        end = m_atlasLayerSwitchIndices[layerIndex + 1];
      }

      // Clear temp RT once per layer group to prevent
      // previous layer's data from bleeding into the vertical pass.
      m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowBlurTempRT, 0, -1);
      renderer->SetFramebuffer(m_shadowFramebuffer, GraphicBitFields::None);
      renderer->ClearBuffer(GraphicBitFields::ColorBits, m_shadowClearColor);

      for (int i = begin; i < end; i++)
      {
        Light* light   = m_lights[i];
        int slotCount  = 0;
        int resolution = (int) light->m_shadowResolution;

        switch (light->GetLightType())
        {
          case Light::LightType::Directional:
            slotCount = cascadeCount;
            break;
          case Light::LightType::Point:
            slotCount = 6;
            break;
          case Light::LightType::Spot:
            slotCount = 1;
            break;
        }

        for (int s = 0; s < slotCount; s++)
        {
          int layer = light->m_shadowAtlasLayers[s];
          if (layer < 0)
          {
            continue;
          }

          Vec2 coord = light->m_shadowAtlasCoords[s];

          renderer->ApplyGaussianBlurToArrayLayerSlot(m_shadowAtlas,
                                                      m_shadowBlurTempRT,
                                                      m_shadowFramebuffer,
                                                      layer,
                                                      kernelSize,
                                                      tapCount,
                                                      amount,
                                                      coord,
                                                      resolution);
        }
      }
    }

    Stats::EndGpuScope();
  }

  void ShadowPass::PlaceShadowMapsToShadowAtlas(const LightRawPtrArray& lights)
  {
    ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    const int cascadeCount    = shadows->GetCascadeCountVal();
    const int atlasSize       = shadows->GetShadowAtlasResolution();

    m_atlas.Reset();

    // Invalidate all lights' atlas slots first.
    for (Light* light : lights)
    {
      light->InvalidateShadowAtlasSlot();
    }

    // Step 1: Place directional lights into Layer 0 (Half slots). Always highest priority.
    for (Light* light : lights)
    {
      if (light->GetLightType() != Light::LightType::Directional)
      {
        continue;
      }

      for (int ii = 0; ii < cascadeCount; ii++)
      {
        ShadowAtlas::SlotInfo slot     = m_atlas.Allocate(ShadowAtlas::SlotSize::Half, atlasSize);
        light->m_shadowAtlasCoords[ii] = slot.coordinate;
        light->m_shadowAtlasLayers[ii] = slot.layer;
      }
      light->m_shadowResolution = (float) ShadowAtlas::GetSlotResolution(ShadowAtlas::SlotSize::Half, atlasSize);
    }

    // Step 2: Collect non-directional shadow casters, sort by priority score.
    Vec3 camPos = m_params.viewCamera->Position();

    LightRawPtrArray localLights;
    localLights.reserve(lights.size());

    for (Light* light : lights)
    {
      if (light->GetLightType() != Light::LightType::Directional)
      {
        localLights.push_back(light);
      }
    }

    std::sort(localLights.begin(),
              localLights.end(),
              [&camPos](Light* a, Light* b)
              {
                float distA =
                    glm::max(glm::distance(a->m_node->GetTranslation(TransformationSpace::TS_WORLD), camPos), 0.01f);
                float distB =
                    glm::max(glm::distance(b->m_node->GetTranslation(TransformationSpace::TS_WORLD), camPos), 0.01f);
                float scoreA = a->AffectDistance() / distA;
                float scoreB = b->AffectDistance() / distB;
                return scoreA > scoreB;
              });

    // Step 3: Place sorted lights. Try Quarter first, fallback to Eighth.
    for (Light* light : localLights)
    {
      int needed = (light->GetLightType() == Light::LightType::Point) ? 6 : 1;

      ShadowAtlas::SlotInfo slots[6];

      // Try Quarter (larger, higher quality).
      if (m_atlas.AllocateN(ShadowAtlas::SlotSize::Quarter, needed, atlasSize, slots))
      {
        for (int s = 0; s < needed; s++)
        {
          light->m_shadowAtlasCoords[s] = slots[s].coordinate;
          light->m_shadowAtlasLayers[s] = slots[s].layer;
        }
        light->m_shadowResolution = (float) ShadowAtlas::GetSlotResolution(ShadowAtlas::SlotSize::Quarter, atlasSize);
        continue;
      }

      // Fallback to Eighth (smaller).
      if (m_atlas.AllocateN(ShadowAtlas::SlotSize::Eighth, needed, atlasSize, slots))
      {
        for (int s = 0; s < needed; s++)
        {
          light->m_shadowAtlasCoords[s] = slots[s].coordinate;
          light->m_shadowAtlasLayers[s] = slots[s].layer;
        }
        light->m_shadowResolution = (float) ShadowAtlas::GetSlotResolution(ShadowAtlas::SlotSize::Eighth, atlasSize);
        continue;
      }

      // No slots available. Light gets no shadow.
    }

    // Invalidate cache for all lights to ensure GPU gets correct atlas data.
    for (Light* light : lights)
    {
      light->InvalidateCacheItem();
    }
  }

  void ShadowPass::InitShadowAtlas()
  {
    TK_PROFILE_FUNCTION();

    if (m_lights.empty())
    {
      return;
    }

    // Always place shadow maps each frame since light list can change.
    PlaceShadowMapsToShadowAtlas(m_lights);

    // Check if the shadow atlas texture needs to be reconstructed.
    bool needReconstruct      = m_shadowAtlas->m_gpuData == nullptr; // First time.
    ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    if (m_activeCascadeCount != shadows->GetCascadeCountVal())
    {
      m_activeCascadeCount = shadows->GetCascadeCountVal();
      needReconstruct      = true;
    }

    if (m_use2KLayer != shadows->GetUse2KShadowAtlasLayerVal())
    {
      m_use2KLayer    = shadows->GetUse2KShadowAtlasLayerVal();
      needReconstruct = true;
    }

    if (needReconstruct)
    {
      // Update shadow clear color to match warped depth space.
      const float vsmExponent   = 5.54f;
      float warpedMax           = std::exp(vsmExponent);
      m_shadowClearColor        = Vec4(warpedMax, warpedMax * warpedMax, 0.0f, 0.0f);

      const TextureSettings set = {GraphicTypes::Target2DArray,
                                   GraphicTypes::UVClampToEdge,
                                   GraphicTypes::UVClampToEdge,
                                   GraphicTypes::UVClampToEdge,
                                   GraphicTypes::SampleLinear,
                                   GraphicTypes::SampleLinear,
                                   GraphicTypes::FormatRG16F,
                                   GraphicTypes::FormatRG,
                                   GraphicTypes::TypeFloat,
                                   MsaaSampleCount::x0,
                                   ShadowAtlas::LayerCount,
                                   false};

      const int shadowAtlasSize = shadows->GetShadowAtlasResolution();

      m_shadowAtlas->ReconstructIfNeeded(shadowAtlasSize, shadowAtlasSize, &set);

      // Scratch FB used only by BlurShadowAtlas — keeps the old "one FB + SetColorAttachment"
      // pattern intact for the blur path (backend FB cache absorbs that churn).
      FramebufferSettings scratchSettings = {shadowAtlasSize, shadowAtlasSize, false, true, MsaaSampleCount::x0};
      m_shadowFramebuffer->ReconstructIfNeeded(scratchSettings);
      m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowAtlas, 0, 0);

      // Per-layer FBs for the main render path. Share one depth attachment across all layers —
      // the original design relied on a single shared depth (cleared once per layer group, then
      // partitioned by viewport across cascades). We extract it from m_shadowFramebuffer and
      // attach the same DepthTexturePtr to every per-layer FB.
      DepthTexturePtr sharedDepth          = m_shadowFramebuffer->GetDepthTexture();
      FramebufferSettings perLayerSettings = {shadowAtlasSize, shadowAtlasSize, false, false, MsaaSampleCount::x0};
      for (int layer = 0; layer < ShadowAtlas::LayerCount; ++layer)
      {
        FramebufferPtr& fb = m_shadowFramebuffers[layer];
        if (fb == nullptr)
        {
          fb = MakeNewPtr<Framebuffer>(perLayerSettings, "ShadowFB_L" + std::to_string(layer));
        }
        fb->ReconstructIfNeeded(perLayerSettings);
        fb->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowAtlas, 0, layer);
        if (sharedDepth != nullptr)
        {
          fb->AttachDepthTexture(sharedDepth);
        }
      }
    }
  }

} // namespace ToolKit