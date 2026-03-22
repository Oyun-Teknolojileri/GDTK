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
    renderer->SetFramebuffer(m_shadowFramebuffer, GraphicBitFields::AllBits);
    for (int i = 0; i < m_layerCount; i++)
    {
      m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowAtlas, 0, i);
      renderer->ClearBuffer(GraphicBitFields::ColorBits, m_shadowClearColor);
    }

    // Update shadow maps.
    for (Light* light : m_lights)
    {
      light->UpdateShadowCamera();

      if (light->GetLightType() == Light::LightType::Directional)
      {
        DirectionalLight* dLight = static_cast<DirectionalLight*>(light);
        dLight->UpdateShadowFrustum(m_params.viewCamera, m_params.scene);
      }

      RenderShadowMaps(light);
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

    Renderer* renderer        = GetRenderer();
    ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    uint resolution           = (uint) light->GetShadowResVal().GetValue<float>();

    if (light->GetLightType() == Light::LightType::Directional)
    {
      int cascadeCount         = shadows->GetCascadeCountVal();
      DirectionalLight* dLight = static_cast<DirectionalLight*>(light);
      for (int i = 0; i < cascadeCount; i++)
      {
        int layer = dLight->m_shadowAtlasLayers[i];
        m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowAtlas, 0, layer);

        renderer->ClearBuffer(GraphicBitFields::DepthBits, m_shadowClearColor);

        UVec2 coord = dLight->m_shadowAtlasCoords[i];
        renderer->SetViewportSize(coord.x, coord.y, resolution, resolution);

        RenderShadowMap(light, dLight->m_cascadeShadowCameras[i], dLight->m_cascadeCullCameras[i]);

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

        renderer->ClearBuffer(GraphicBitFields::DepthBits, m_shadowClearColor);

        UVec2 coord = light->m_shadowAtlasCoords[i];
        renderer->SetViewportSize(coord.x, coord.y, resolution, resolution);

        RenderShadowMap(light, light->m_shadowCamera, light->m_shadowCamera);

        // Depth is invalidated because, atlas has the shadow map.
        renderer->InvalidateFramebufferDepth(m_shadowFramebuffer);
      }
    }
    else
    {
      assert(light->GetLightType() == Light::LightType::Spot);

      m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0,
                                              m_shadowAtlas,
                                              0,
                                              light->m_shadowAtlasLayers[0]);

      renderer->ClearBuffer(GraphicBitFields::DepthBits, m_shadowClearColor);

      UVec2 coord = light->m_shadowAtlasCoords[0];

      renderer->SetViewportSize(coord.x, coord.y, resolution, resolution);
      RenderShadowMap(light, light->m_shadowCamera, light->m_shadowCamera);

      // Depth is invalidated because, atlas has the shadow map.
      renderer->InvalidateFramebufferDepth(m_shadowFramebuffer);
    }
  }

  void ShadowPass::RenderShadowMap(Light* light, CameraPtr shadowCamera, CameraPtr cullCamera)
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

    if (m_layerCount <= 0)
    {
      return;
    }

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

    for (int i = 0; i < m_layerCount; i++)
    {
      renderer->ApplyGaussianBlurToArrayLayer(m_shadowAtlas,
                                              m_shadowBlurTempRT,
                                              m_shadowFramebuffer,
                                              i,
                                              kernelSize,
                                              tapCount,
                                              amount);
    }
  }

  int ShadowPass::PlaceShadowMapsToShadowAtlas(const LightRawPtrArray& lights)
  {
    LightRawPtrArray lightArray = lights;

    // Sort all lights based on resolution.
    std::sort(lightArray.begin(),
              lightArray.end(),
              [](Light* l1, Light* l2) -> bool
              { return l1->GetShadowResVal().GetValue<float>() < l2->GetShadowResVal().GetValue<float>(); });

    ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    const int cascadeCount    = shadows->GetCascadeCountVal();

    IntArray resolutions;
    resolutions.reserve(lightArray.size() * 6);
    for (size_t i = 0; i < lightArray.size(); i++)
    {
      Light* light   = lightArray[i];
      int resolution = (int) light->GetShadowResVal().GetValue<float>();

      if (light->GetLightType() == Light::Directional)
      {
        for (int ii = 0; ii < cascadeCount; ii++)
        {
          resolutions.push_back(resolution);
        }
      }
      else if (light->GetLightType() == Light::Point)
      {
        for (int ii = 0; ii < 6; ii++)
        {
          resolutions.push_back(resolution);
        }
      }
      else
      {
        assert(light->GetLightType() == Light::LightType::Spot);
        resolutions.push_back(resolution);
      }
    }

    const int shadowAtlasSize        = shadows->GetShadowAtlasResolution();

    int layerCount                   = 0;
    BinPack2D::PackedRectArray rects = m_packer.Pack(resolutions, shadowAtlasSize, &layerCount);

    int rectIndex                    = 0;
    for (int i = 0; i < lightArray.size(); i++)
    {
      Light* light = lightArray[i];
      if (light->GetLightType() == Light::LightType::Directional)
      {
        for (int ii = 0; ii < shadows->GetCascadeCountVal(); ii++)
        {
          light->m_shadowAtlasCoords[ii] = rects[rectIndex].coordinate;
          light->m_shadowAtlasLayers[ii] = rects[rectIndex].layer;
          rectIndex++;
        }
      }
      else if (light->GetLightType() == Light::LightType::Point)
      {
        for (int ii = 0; ii < 6; ii++)
        {
          light->m_shadowAtlasCoords[ii] = rects[rectIndex].coordinate;
          light->m_shadowAtlasLayers[ii] = rects[rectIndex].layer;
          rectIndex++;
        }
      }
      else
      {
        assert(light->GetLightType() == Light::LightType::Spot);

        light->m_shadowAtlasCoords[0] = rects[rectIndex].coordinate;
        light->m_shadowAtlasLayers[0] = rects[rectIndex].layer;
        rectIndex++;
      }
    }

    return layerCount;
  }

  void ShadowPass::InitShadowAtlas()
  {
    TK_PROFILE_FUNCTION();

    // Check if the shadow atlas needs to be updated
    bool needChange           = false;
    ShadowSettingsPtr shadows = GetEngineSettings().m_graphics->m_shadows;
    if (m_activeCascadeCount != shadows->GetCascadeCountVal())
    {
      m_activeCascadeCount = shadows->GetCascadeCountVal();
      needChange           = true;
    }

    if (m_use32BitShadowMap != shadows->GetUse32BitShadowMapVal())
    {
      m_use32BitShadowMap = shadows->GetUse32BitShadowMapVal();
      needChange          = true;
    }

    if (m_use2KLayer != shadows->GetUse2KShadowAtlasLayerVal())
    {
      m_use2KLayer = shadows->GetUse2KShadowAtlasLayerVal();
      needChange   = true;
    }

    // After this loop m_previousShadowCasters is set with lights with shadows
    int nextId = 0;
    for (int i = 0; i < m_lights.size(); ++i)
    {
      Light* light = m_lights[i];
      if (light->m_shadowResolutionUpdated)
      {
        light->m_shadowResolutionUpdated = false;
        needChange                       = true;
      }

      if (nextId >= m_previousShadowCasters.size())
      {
        needChange = true;
        m_previousShadowCasters.push_back(light->GetIdVal());
        nextId++;
        continue;
      }

      if (m_previousShadowCasters[nextId] != light->GetIdVal())
      {
        needChange = true;
      }

      m_previousShadowCasters[nextId] = light->GetIdVal();
      nextId++;
    }

    if (needChange && !m_lights.empty())
    {
      // Update materials.
      ShaderPtr frag = m_shadowMatOrtho->GetFragmentShaderVal();
      frag->SetDefine("SMFormat16Bit", std::to_string(!m_use32BitShadowMap));

      frag = m_shadowMatPersp->GetFragmentShaderVal();
      frag->SetDefine("SMFormat16Bit", std::to_string(!m_use32BitShadowMap));

      // Update layers.
      m_previousShadowCasters.resize(nextId);

      // Place shadow textures to atlas
      m_layerCount        = PlaceShadowMapsToShadowAtlas(m_lights);

      const int maxLayers = GetRenderer()->GetMaxArrayTextureLayers();
      if (maxLayers < m_layerCount)
      {
        m_layerCount = maxLayers;
        GetLogger()->Log("ERROR: Max array texture layer size is reached: " + std::to_string(maxLayers) + " !");
      }

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
                                   m_layerCount,
                                   false};

      const int shadowAtlasSize = shadows->GetShadowAtlasResolution();

      // m_shadowFramebuffer->DetachColorAttachment(Framebuffer::Attachment::ColorAttachment0);
      m_shadowAtlas->ReconstructIfNeeded(shadowAtlasSize, shadowAtlasSize, &set);

      FramebufferSettings fbSettings = {shadowAtlasSize, shadowAtlasSize, false, true, MsaaSampleCount::x0};

      m_shadowFramebuffer->ReconstructIfNeeded(fbSettings);
      m_shadowFramebuffer->SetColorAttachment(Framebuffer::Attachment::ColorAttachment0, m_shadowAtlas, 0, 0);
    }
  }

} // namespace ToolKit