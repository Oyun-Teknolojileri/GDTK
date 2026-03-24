/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
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
    Mat4 views[6] = {glm::lookAt(ZERO, Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                     glm::lookAt(ZERO, Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)),
                     glm::lookAt(ZERO, Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)),
                     glm::lookAt(ZERO, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)),
                     glm::lookAt(ZERO, Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, -1.0f, 0.0f)),
                     glm::lookAt(ZERO, Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, -1.0f, 0.0f))};

    for (int i = 0; i < 6; i++)
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
      material->GetRenderState()->blendFunction = BlendFunction::NONE;
      material->Init();

      return material;
    };

    m_shadowMatOrtho = createShadowMaterialFn("orthogonalDepthVert.shader", "orthogonalDepthFrag.shader");
    m_shadowMatPersp = createShadowMaterialFn("perspectiveDepthVert.shader", "perspectiveDepthFrag.shader");
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

    // Clear shadow atlas before any draw call
    renderer->SetFramebuffer(m_shadowFramebuffer, GraphicBitFields::None);
    for (int i = 0; i < ShadowAtlas::LayerCount; i++)
    {
      m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowAtlas, 0, i);
      renderer->ClearBuffer(GraphicBitFields::ColorBits, m_shadowClearColor);
    }
    renderer->ClearBuffer(GraphicBitFields::DepthBits);

    // Update shadow maps.
    for (Light* light : m_lights)
    {
      light->UpdateShadowCamera();

      switch (light->GetLightType())
      {
        case Light::LightType::Directional:
        {
          Stats::BeginGpuScope("Directioal Shadow Map");
          DirectionalLight* dLight = static_cast<DirectionalLight*>(light);
          dLight->UpdateShadowFrustum(m_params.viewCamera, m_params.scene);
        }
        break;
        case Light::LightType::Point:
          Stats::BeginGpuScope("Point Shadow Map");
          break;
        case Light::LightType::Spot:
          Stats::BeginGpuScope("Spot Shadow Map");
          break;
        default:
          Stats::BeginGpuScope("Undefined Shadow Map");
          break;
      }

      RenderShadowMaps(light);
      Stats::EndGpuScope();
    }

    // Apply blur to the shadow atlas.
    BlurShadowAtlas();

    renderer->m_clearColor = lastClearColor;
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

    // Sort lights by their first shadow atlas layer to minimize layer switches
    // during both shadow map rendering and blur passes.
    std::sort(m_lights.begin(),
              m_lights.end(),
              [](Light* a, Light* b)
              {
                int layerA = a->HasValidShadowSlot() ? a->m_shadowAtlasLayers[0] : INT_MAX;
                int layerB = b->HasValidShadowSlot() ? b->m_shadowAtlasLayers[0] : INT_MAX;
                return layerA < layerB;
              });
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
    GetRenderer()->SetDirectionalLights(dlights);
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

    if (light->GetLightType() == Light::LightType::Directional)
    {
      int cascadeCount         = shadows->GetCascadeCountVal();
      DirectionalLight* dLight = static_cast<DirectionalLight*>(light);
      for (int i = 0; i < cascadeCount; i++)
      {
        int layer = dLight->m_shadowAtlasLayers[i];
        m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowAtlas, 0, layer);

        UVec2 coord = dLight->m_shadowAtlasCoords[i];
        renderer->SetViewportSize(coord.x, coord.y, resolution, resolution);

        RenderShadowCasters(light, dLight->m_cascadeShadowCameras[i], dLight->m_cascadeCullCameras[i]);

        // Depth is invalidated because, atlas has the shadow map.
        renderer->InvalidateFramebufferDepth(m_shadowFramebuffer);
      }
    }
    else if (light->GetLightType() == Light::LightType::Point)
    {
      for (int i = 0; i < 6; i++)
      {
        int layer = light->m_shadowAtlasLayers[i];
        m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowAtlas, 0, layer);

        light->m_shadowCamera->m_node->SetTranslation(light->m_node->GetTranslation());
        light->m_shadowCamera->m_node->SetOrientation(m_cubeMapRotations[i]);

        UVec2 coord = light->m_shadowAtlasCoords[i];
        renderer->SetViewportSize(coord.x, coord.y, resolution, resolution);

        RenderShadowCasters(light, light->m_shadowCamera, light->m_shadowCamera);

        // Depth is invalidated because, atlas has the shadow map.
        renderer->InvalidateFramebufferDepth(m_shadowFramebuffer);
      }
    }
    else
    {
      assert(light->GetLightType() == Light::LightType::Spot);

      int layer = light->m_shadowAtlasLayers[0];
      m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowAtlas, 0, layer);

      UVec2 coord = light->m_shadowAtlasCoords[0];

      renderer->SetViewportSize(coord.x, coord.y, resolution, resolution);
      RenderShadowCasters(light, light->m_shadowCamera, light->m_shadowCamera);

      // Depth is invalidated because, atlas has the shadow map.
      renderer->InvalidateFramebufferDepth(m_shadowFramebuffer);
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
    RenderJobProcessor::SeperateRenderData(renderData, true);

    renderer->OverrideBlendState(true, BlendFunction::NONE); // Blending must be disabled for shadow map generation.

    // Set material and program.
    bool orthogonalShadowMap   = lightType == Light::LightType::Directional;
    MaterialPtr shadowMaterial = orthogonalShadowMap ? m_shadowMatOrtho : m_shadowMatPersp;
    ShaderPtr frag             = shadowMaterial->GetFragmentShaderVal();
    frag->SetDefine("DrawAlphaMasked", "0");
    ShaderPtr vert = shadowMaterial->GetVertexShaderVal();
    vert->SetDefine("DrawAlphaMasked", "0");

    GpuProgramManager* gpuProgramManager = GetGpuProgramManager();
    m_program                            = gpuProgramManager->CreateProgram(vert, frag);
    renderer->BindProgram(m_program);

    if (orthogonalShadowMap)
    {
      if (renderer->EnableDepthClamp(true))
      {
        vert->SetDefine("Pancake", "0");
        frag->SetDefine("Pancake", "0");
      }
      else
      {
        // If depth clamp is not supported, fallback to pancake.
        frag->SetDefine("Pancake", "1");
        vert->SetDefine("Pancake", "1");
      }
    }

    // Draw opaque.
    RenderJobItr forwardBegin       = renderData.GetForwardOpaqueBegin();
    RenderJobItr forwardMaskedBegin = renderData.GetForwardAlphaMaskedBegin();
    for (RenderJobItr jobItr = forwardBegin; jobItr < forwardMaskedBegin; jobItr++)
    {
      renderer->Render(*jobItr);
    }

    // Draw alpha masked.
    frag->SetDefine("DrawAlphaMasked", "1");
    vert->SetDefine("DrawAlphaMasked", "1");
    m_program = gpuProgramManager->CreateProgram(vert, frag);
    renderer->BindProgram(m_program);

    RenderJobItr translucentBegin = renderData.GetForwardTranslucentBegin();
    for (RenderJobItr jobItr = forwardMaskedBegin; jobItr < translucentBegin; jobItr++)
    {
      renderer->Render(*jobItr);
    }

    if (orthogonalShadowMap)
    {
      renderer->EnableDepthClamp(false);
    }

    // Translucent shadow is not supported.

    renderer->OverrideBlendState(false, BlendFunction::NONE);
  }

  void ShadowPass::BlurShadowAtlas()
  {
    TK_PROFILE_FUNCTION();
    Stats::BeginGpuScope("Shadow Blur");

    Renderer* renderer = GetRenderer();

    // Create temp RT for blur ping-pong if needed.
    if (m_shadowBlurTempRT == nullptr)
    {
      m_shadowBlurTempRT = MakeNewPtr<RenderTarget>("ShadowBlurTempRT");
    }

    GraphicTypes bufferComponents = GraphicTypes::FormatRG;
    GraphicTypes bufferFormat     = m_use32BitShadowMap ? GraphicTypes::FormatRG32F : GraphicTypes::FormatRG16F;

    GraphicTypes sampler          = GraphicTypes::SampleLinear;
    if (!TK_GL_OES_texture_float_linear)
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
      return;
    }

    const int cascadeCount = shadows->GetCascadeCountVal();

    // Track last cleared layer across all lights to avoid redundant clears.
    // Lights are sorted by layer in PreRender, so layer transitions are minimal.
    int lastClearedLayer   = -1;

    // Blur each light's shadow slots individually, clamped to slot bounds.
    for (Light* light : m_lights)
    {
      if (!light->HasValidShadowSlot())
      {
        continue;
      }

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

        // Clear temp RT when switching to a different layer to prevent
        // previous layer's data from bleeding into the vertical pass.
        if (layer != lastClearedLayer)
        {
          m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowBlurTempRT, 0, -1);
          renderer->SetFramebuffer(m_shadowFramebuffer, GraphicBitFields::None);
          renderer->ClearBuffer(GraphicBitFields::ColorBits, m_shadowClearColor);
          lastClearedLayer = layer;
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
    bool needReconstruct      = m_shadowAtlas->m_textureId == 0; // First time.
    ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    if (m_activeCascadeCount != shadows->GetCascadeCountVal())
    {
      m_activeCascadeCount = shadows->GetCascadeCountVal();
      needReconstruct      = true;
    }

    if (m_use32BitShadowMap != shadows->GetUse32BitShadowMapVal())
    {
      m_use32BitShadowMap = shadows->GetUse32BitShadowMapVal();
      needReconstruct     = true;
    }

    if (m_use2KLayer != shadows->GetUse2KShadowAtlasLayerVal())
    {
      m_use2KLayer    = shadows->GetUse2KShadowAtlasLayerVal();
      needReconstruct = true;
    }

    if (needReconstruct)
    {
      // Update shadow clear color to match warped depth space.
      const float vsmExponent = m_use32BitShadowMap ? 8.0f : 5.54f;
      float warpedMax         = std::exp(vsmExponent);
      m_shadowClearColor      = Vec4(warpedMax, warpedMax * warpedMax, 0.0f, 0.0f);

      // Update materials.
      ShaderPtr frag          = m_shadowMatOrtho->GetFragmentShaderVal();
      frag->SetDefine("SMFormat16Bit", std::to_string(!m_use32BitShadowMap));

      frag = m_shadowMatPersp->GetFragmentShaderVal();
      frag->SetDefine("SMFormat16Bit", std::to_string(!m_use32BitShadowMap));

      GraphicTypes bufferComponents = GraphicTypes::FormatRG;
      GraphicTypes bufferFormat     = GraphicTypes::FormatRG32F;

      if (!m_use32BitShadowMap)
      {
        bufferFormat = GraphicTypes::FormatRG16F;
      }

      GraphicTypes sampler = GraphicTypes::SampleLinear;
      if (!TK_GL_OES_texture_float_linear)
      {
        // Fall back to nearest sampling. 32 bit filterable textures are not available.
        sampler = GraphicTypes::SampleNearest;
      }

      const TextureSettings set = {GraphicTypes::Target2DArray,
                                   GraphicTypes::UVClampToEdge,
                                   GraphicTypes::UVClampToEdge,
                                   GraphicTypes::UVClampToEdge,
                                   sampler,
                                   sampler,
                                   bufferFormat,
                                   bufferComponents,
                                   GraphicTypes::TypeFloat,
                                   MsaaSampleCount::x0,
                                   ShadowAtlas::LayerCount,
                                   false};

      const int shadowAtlasSize = shadows->GetShadowAtlasResolution();

      m_shadowAtlas->ReconstructIfNeeded(shadowAtlasSize, shadowAtlasSize, &set);

      FramebufferSettings fbSettings = {shadowAtlasSize, shadowAtlasSize, false, true, MsaaSampleCount::x0};

      m_shadowFramebuffer->ReconstructIfNeeded(fbSettings);
      m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowAtlas, 0, 0);
    }
  }

} // namespace ToolKit