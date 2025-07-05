/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Light.h"

#include "AABBOverrideComponent.h"
#include "Camera.h"
#include "Component.h"
#include "DirectionComponent.h"
#include "EngineSettings.h"
#include "Material.h"
#include "MathUtil.h"
#include "Mesh.h"
#include "Pass.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "Scene.h"
#include "Shader.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  // Light
  //////////////////////////////////////////

  TKDefineAbstractClass(Light, Entity);

  Light::Light()
  {
    m_shadowCamera      = MakeNewPtr<Camera>();

    float minShadowClip = GetEngineSettings().m_graphics->m_shadows->GetShadowMinDistanceVal();
    m_shadowCamera->SetNearClipVal(minShadowClip);
    m_shadowCamera->SetOrthographicScaleVal(1.0f);
  }

  Light::~Light() {}

  void Light::NativeConstruct() { Super::NativeConstruct(); }

  void Light::ParameterConstructor()
  {
    Super::ParameterConstructor();

    MultiChoiceVariant mcv = {
        {CreateMultiChoiceParameter("512", 512.0f),
         CreateMultiChoiceParameter("1024", 1024.0f),
         CreateMultiChoiceParameter("2048", 2048.0f)},
        1
    };

    ShadowRes_Define(mcv, "Light", 90, true, true);

    Color_Define(Vec3(1.0f), "Light", 0, true, true, {true});
    Intensity_Define(1.0f, "Light", 90, true, true, {false, true, 0.0f, 100000.0f, 0.1f});
    CastShadow_Define(false, "Light", 90, true, true);
    PCFRadius_Define(1.0f, "Light", 90, true, true, {false, true, 0.0f, 10.0f, 0.1f});
    ShadowBias_Define(0.1f, "Light", 90, true, true, {false, true, 0.0f, 20000.0f, 0.01f});
    BleedingReduction_Define(0.1f, "Light", 90, true, true, {false, true, 0.0f, 1.0f, 0.001f});
  }

  void Light::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    ParamShadowRes().GetVar<MultiChoiceVariant>().CurrentVal.Callback = [&](Value& oldVal, Value& newVal)
    {
      InvalidateCacheItem();

      if (GetCastShadowVal())
      {
        // Invalidates shadow atlas.
        m_shadowResolutionUpdated = true;
      }
    };

    ParamColor().m_onValueChangedFn.push_back([this](Value& oldVal, Value& newVal) -> void { InvalidateCacheItem(); });

    ParamIntensity().m_onValueChangedFn.push_back([this](Value& oldVal, Value& newVal) -> void
                                                  { InvalidateCacheItem(); });

    ParamCastShadow().m_onValueChangedFn.push_back([this](Value& oldVal, Value& newVal) -> void
                                                   { InvalidateCacheItem(); });

    ParamPCFRadius().m_onValueChangedFn.push_back([this](Value& oldVal, Value& newVal) -> void
                                                  { InvalidateCacheItem(); });

    ParamShadowBias().m_onValueChangedFn.push_back([this](Value& oldVal, Value& newVal) -> void
                                                   { InvalidateCacheItem(); });

    ParamBleedingReduction().m_onValueChangedFn.push_back([this](Value& oldVal, Value& newVal) -> void
                                                          { InvalidateCacheItem(); });
  }

  void Light::UpdateShadowCamera() { m_shadowMapCameraProjectionViewMatrix = m_shadowCamera->GetProjectViewMatrix(); }

  float Light::AffectDistance() { return 1000.0f; }

  void Light::InvalidateSpatialCaches()
  {
    Super::InvalidateSpatialCaches();
    InvalidateCacheItem();
  }

  void Light::UpdateShadowCameraTransform()
  {
    Mat4 lightTs = m_node->GetTransform();
    m_shadowCamera->m_node->SetTransform(lightTs);
  }

  XmlNode* Light::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);

    return node;
  }

  XmlNode* Light::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    ClearComponents(); // Read from file.
    XmlNode* nttNode = Super::DeSerializeImp(info, parent);
    return nttNode->first_node(StaticClass()->Name.c_str());
  }

  void Light::UpdateLocalBoundingBox()
  {
    if (m_volumeMesh != nullptr)
    {
      m_localBoundingBoxCache = m_volumeMesh->m_boundingBox;
    }
    else
    {
      m_localBoundingBoxCache = infinitesimalBox;
    }
  }

  void Light::UpdateCommonLightData(LightCacheItem::CommonData* data)
  {
    data->color             = GetColorVal();
    data->intensity         = GetIntensityVal();
    data->position          = m_node->GetTranslation(TransformationSpace::TS_WORLD);
    data->castShadow        = GetCastShadowVal();
    data->shadowBias        = GetShadowBiasVal() * RHIConstants::ShadowBiasMultiplier;
    data->bleedingReduction = GetBleedingReductionVal();
    data->pcfRadius         = GetPCFRadiusVal();
    data->shadowResolution  = GetShadowResVal().GetValue<float>();
    data->shadowAtlasLayer  = m_shadowAtlasLayers[0];
    data->shadowAtlasCoord  = m_shadowAtlasCoords[0];
  }

  // DirectionalLightBuffer
  //////////////////////////////////////////

  DirectionalLightBuffer::DirectionalLightBuffer()
  {
    m_lightDataSize = RHIConstants::DirectionalLightCacheItemCount * sizeof(DirectionalLightCacheItem::Data);
    m_lightData     = new byte[m_lightDataSize];
    memset(m_lightData, 0, m_lightDataSize);

    m_pvmDataSize = RHIConstants::DirectionalLightCacheItemCount * RHIConstants::MaxCascadeCount * sizeof(Mat4);
    m_pvmData     = new byte[m_pvmDataSize];
    memset(m_pvmData, 0, m_pvmDataSize);
  }

  DirectionalLightBuffer::~DirectionalLightBuffer()
  {
    SafeDelArray(m_lightData);
    SafeDelArray(m_pvmData);
  }

  void DirectionalLightBuffer::Init()
  {
    m_lightDataBuffer.Init(m_lightDataSize);
    m_lightDataBuffer.m_slot = BindingSlotForLight;

    m_pvms.Init(m_pvmDataSize);
    m_pvms.m_slot = BindingSlotForPVM;
  }

  void DirectionalLightBuffer::Map(const LightRawPtrArray& lights)
  {
    int lightCount = (int) lights.size();
    for (int i = 0; i < lightCount; i++)
    {
      DirectionalLight* dl                  = static_cast<DirectionalLight*>(lights[i]);
      const DirectionalLightCacheItem& item = dl->GetCacheItem();
      memcpy(m_lightData + (i * sizeof(DirectionalLightCacheItem::Data)),
             &item.data,
             sizeof(DirectionalLightCacheItem::Data));

      for (int j = 0; j < RHIConstants::MaxCascadeCount; j++)
      {
        memcpy(m_pvmData + (i * RHIConstants::MaxCascadeCount + j) * sizeof(Mat4),
               &dl->m_shadowMapCascadeCameraProjectionViewMatrices[j],
               sizeof(Mat4));
      }
    }

    m_lightDataBuffer.Map(m_lightData, m_lightDataSize);
    m_pvms.Map(m_pvmData, m_pvmDataSize);

    if (TKStats* stats = GetTKStats())
    {
      stats->m_directionalLightUpdatePerFrame += 2; // Includes pvm updates.
    }
  }

  // DirectionalLight
  //////////////////////////////////////////

  TKDefineClass(DirectionalLight, Light);

  DirectionalLight::DirectionalLight()
  {
    m_shadowCamera->SetOrthographicVal(true);
    for (int i = 0; i < RHIConstants::MaxCascadeCount; i++)
    {
      CameraPtr cam = MakeNewPtr<Camera>();
      cam->SetOrthographicVal(true);
      cam->SetOrthographicScaleVal(1.0f);
      cam->InvalidateSpatialCaches();
      m_cascadeShadowCameras.push_back(cam);

      m_cascadeCullCameras.push_back(Cast<Camera>(cam->Copy()));

      m_shadowAtlasLayers.push_back(-1);
      m_shadowAtlasCoords.push_back(Vec2(-1.0f));
    }

    m_shadowMapCascadeCameraProjectionViewMatrices.resize(RHIConstants::MaxCascadeCount);
  }

  DirectionalLight::~DirectionalLight()
  {
    m_cascadeShadowCameras.clear();
    m_shadowMapCascadeCameraProjectionViewMatrices.clear();
  }

  void DirectionalLight::NativeConstruct()
  {
    Super::NativeConstruct();
    AddComponent<DirectionComponent>();
  }

  void DirectionalLight::UpdateShadowFrustum(CameraPtr cameraView, ScenePtr scene)
  {
    ShadowSettingsPtr shadows  = GetEngineSettings().m_graphics->m_shadows;
    int cascades               = shadows->GetCascadeCountVal();
    Vec4 cascadeDists          = shadows->GetCascadeDistancesVal();

    const float lastCameraNear = cameraView->Near();
    const float lastCameraFar  = cameraView->Far();

    float nearClip             = shadows->GetShadowMinDistanceVal();
    float farClip              = cascadeDists[0];

    for (int i = 0; i < cascades; i++)
    {
      // Setting near far to cascade z boundaries for calculating tight cascade frustum.
      cameraView->SetNearClipVal(nearClip);
      cameraView->SetFarClipVal(farClip);

      FitViewFrustumIntoLightFrustum(m_cascadeShadowCameras[i],
                                     cameraView,
                                     nearClip,
                                     farClip,
                                     shadows->GetStableShadowMapVal());
      FitViewFrustumIntoLightFrustum(m_cascadeCullCameras[i], cameraView, nearClip, farClip, false);

      if (i + 1 < cascades)
      {
        nearClip = cascadeDists[i];
        farClip  = cascadeDists[i + 1];
      }
    }

    // Setting back the original view distances.
    cameraView->SetNearClipVal(lastCameraNear);
    cameraView->SetFarClipVal(lastCameraFar);

    UpdateShadowCamera();
    InvalidateCacheItem();
  }

  void DirectionalLight::UpdateShadowCamera()
  {
    for (int i = 0; i < RHIConstants::MaxCascadeCount; i++)
    {
      m_shadowMapCascadeCameraProjectionViewMatrices[i] = m_cascadeShadowCameras[i]->GetProjectViewMatrix();
    }
  }

  const DirectionalLightCacheItem& DirectionalLight::GetCacheItem()
  {
    if (m_lightCacheItem.isValid)
    {
      return m_lightCacheItem;
    }

    m_lightCacheItem.id = GetIdVal();
    UpdateCommonLightData(&m_lightCacheItem.data);

    m_lightCacheItem.data.direction = GetComponentFast<DirectionComponent>()->GetDirection();

    m_lightCacheItem.Validate();

    return m_lightCacheItem;
  }

  void DirectionalLight::InvalidateCacheItem() { m_lightCacheItem.Invalidate(); }

  XmlNode* DirectionalLight::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);

    return node;
  }

  void DirectionalLight::FitViewFrustumIntoLightFrustum(CameraPtr lightCamera,
                                                        CameraPtr viewCamera,
                                                        float near,
                                                        float far,
                                                        bool stableFit)
  {
    // View camera has near far distances coming from i'th cascade boundaries.
    // Shadow camera is aligned with light direction, and positioned to the view camera frustum's center.
    // Now we can calculate a bounding box that tightly fits to the i'th cascade.
    Vec3Array frustum = viewCamera->ExtractFrustumCorner(); // World space frustum.

    Vec3 center       = ZERO;
    for (int i = 0; i < 8; i++)
    {
      center += frustum[i];
    }
    center /= 8.0f;

    lightCamera->m_node->SetOrientation(m_node->GetOrientation()); // shadow camera direction aligned with light.
    lightCamera->m_node->SetTranslation(center);                   // shadow camera is at the frustum center.

    EngineSettings& settings = GetEngineSettings();

    // Calculate tight shadow volume, in light's view.
    BoundingBox tightShadowVolume;
    if (stableFit)
    {
      // Fit a sphere around the view frustum to prevent swimming when rotating the view camera.
      // Sphere fit will prevent size / center changes of the frustum, which will yield the same shadow map
      // after the camera is rotated.
      // Additional shadow map resolution will be wasted due to bounding box / bounding sphere difference.
      float radius = 0.0f;
      for (int i = 0; i < 8; i++)
      {
        radius = glm::max(radius, glm::distance(center, frustum[i]));
      }

      radius                = glm::ceil(radius * 16.0f) / 16.0f;
      tightShadowVolume.min = Vec3(-radius);
      tightShadowVolume.max = Vec3(radius);
    }
    else
    {
      // Tight fit a bounding box to the view frustum in light space.
      Mat4 lightView = lightCamera->GetViewMatrix();
      for (int i = 0; i < 8; i++)
      {
        Vec4 vertex = lightView * Vec4(frustum[i], 1.0f); // Move the view camera frustum to light's view.
        tightShadowVolume.UpdateBoundary(vertex);         // Calculate its boundary.
      }
    }

    // Now frustum is sitting at the origin in light's view. Since the light was placed at the frustum center,
    // half of the volume is behind the camera.

    // Push the tighShadowVolume just in front of the camera by pulling the camera backwards from the center
    // exactly max z units. If we not perform this, frustum center will be placed to origin, from 0 to max.z will stay
    // behind the camera.
    lightCamera->m_node->SetTranslation(center - lightCamera->Direction() * tightShadowVolume.max.z);

    // Set the lens such that it only captures everything inside the frustum.
    float tightFar = tightShadowVolume.max.z - tightShadowVolume.min.z;
    lightCamera->SetLens(tightShadowVolume.min.x,
                         tightShadowVolume.max.x,
                         tightShadowVolume.min.y,
                         tightShadowVolume.max.y,
                         0.0f,
                         tightFar);

    // Allow camera to only make texel size movements.
    // To do this, find the camera origin in projection space and calculate the offset that
    // puts the camera origin on to a texel, prevent sub pixel movements and shimmering in shadow map.
    float shadowMapRes                     = GetShadowResVal().GetValue<float>();
    Mat4 shadowMatrix                      = lightCamera->GetProjectViewMatrix();
    Vec4 shadowOrigin                      = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    shadowOrigin                           = shadowMatrix * shadowOrigin;
    shadowOrigin                           = shadowOrigin * shadowMapRes / 2.0f;

    Vec4 roundedOrigin                     = glm::round(shadowOrigin);
    Vec4 roundOffset                       = roundedOrigin - shadowOrigin;
    roundOffset                            = roundOffset * 2.0f / shadowMapRes;
    roundOffset.z                          = 0.0f;
    roundOffset.w                          = 0.0f;

    lightCamera->GetProjectionMatrix()[3] += roundOffset;
  }

  // PointLightCache
  //////////////////////////////////////////

  PointLightCache::PointLightCache()
      : LRUCache(RHIConstants::PointLightCacheItemCount * sizeof(PointLightCacheItem::Data))
  {
  }

  PointLightCache::~PointLightCache() {}

  void PointLightCache::Init()
  {
    m_gpuBuffer.Init(m_cacheSize);
    m_gpuBuffer.m_slot = BindingSlot;
  }

  bool PointLightCache::Map()
  {
    return LRUCache::Map([this](const void* data, uint64 size) { m_gpuBuffer.Map(data, size); });
  }

  // PointLight
  //////////////////////////////////////////

  TKDefineClass(PointLight, Light);

  PointLight::PointLight()
  {
    for (int i = 0; i < 6; i++)
    {
      m_shadowAtlasLayers.push_back(-1);
      m_shadowAtlasCoords.push_back(Vec2(-1.0f));
    }
  }

  PointLight::~PointLight() {}

  void PointLight::UpdateShadowCamera()
  {
    m_shadowCamera->SetLens(glm::half_pi<float>(), 1.0f, 0.01f, AffectDistance());

    Light::UpdateShadowCamera();

    UpdateShadowCameraTransform();

    m_boundingSphereCache = {m_node->GetTranslation(), GetRadiusVal()};
  }

  const PointLightCacheItem& PointLight::GetCacheItem()
  {
    if (m_lightCacheItem.isValid)
    {
      return m_lightCacheItem;
    }

    UpdateCommonLightData(&m_lightCacheItem.data);

    m_lightCacheItem.id          = GetIdVal();
    m_lightCacheItem.data.radius = GetRadiusVal();

    m_lightCacheItem.Validate();

    return m_lightCacheItem;
  }

  void PointLight::InvalidateCacheItem() { m_lightCacheItem.Invalidate(); };

  float PointLight::AffectDistance() { return GetRadiusVal(); }

  XmlNode* PointLight::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);

    return node;
  }

  void PointLight::ParameterConstructor()
  {
    Super::ParameterConstructor();

    ParamPCFRadius().m_hint.increment = 0.02f;
    Radius_Define(3.0f, "Light", 90, true, true, {false, true, 0.1f, 100000.0f, 0.3f});
  }

  void PointLight::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    ParamRadius().m_onValueChangedFn.push_back([this](Value& oldVal, Value& newVal) -> void
                                               { InvalidateSpatialCaches(); });
  }

  void PointLight::UpdateLocalBoundingBox()
  {
    float radius            = GetRadiusVal();
    m_localBoundingBoxCache = BoundingBox(Vec3(-radius), Vec3(radius));
  }

  // SpotLightCache
  //////////////////////////////////////////

  SpotLightCache::SpotLightCache() : LRUCache(RHIConstants::SpotLightCacheItemCount * sizeof(SpotLightCacheItem::Data))
  {
  }

  SpotLightCache::~SpotLightCache() {}

  void SpotLightCache::Init()
  {
    m_gpuBuffer.Init(m_cacheSize);
    m_gpuBuffer.m_slot = BindingSlot;
  }

  bool SpotLightCache::Map()
  {
    return LRUCache::Map([this](const void* data, uint64 size) { m_gpuBuffer.Map(data, size); });
  }

  // SpotLight
  //////////////////////////////////////////

  TKDefineClass(SpotLight, Light);

  SpotLight::SpotLight()
  {
    m_shadowAtlasLayers.push_back(-1);
    m_shadowAtlasCoords.push_back(Vec2(-1.0f));
  }

  SpotLight::~SpotLight() {}

  void SpotLight::UpdateShadowCamera()
  {
    m_shadowCamera->SetLens(glm::radians(GetOuterAngleVal()), 1.0f, 0.01f, AffectDistance());

    UpdateShadowCameraTransform();

    Light::UpdateShadowCamera();

    // Calculate frustum.
    m_frustumCache           = ExtractFrustum(m_shadowMapCameraProjectionViewMatrix, false);

    // Calculate bounding box for the frustum.
    Vec3Array frustumCorners = m_shadowCamera->ExtractFrustumCorner();
    m_boundingBoxCache       = BoundingBox();
    for (int i = 0; i < 8; i++)
    {
      m_boundingBoxCache.UpdateBoundary(frustumCorners[i]);
    }
  }

  float SpotLight::AffectDistance() { return GetRadiusVal(); }

  const SpotLightCacheItem& SpotLight::GetCacheItem()
  {
    if (m_lightCacheItem.isValid)
    {
      return m_lightCacheItem;
    }

    UpdateCommonLightData(&m_lightCacheItem.data);

    m_lightCacheItem.id                        = GetIdVal();
    m_lightCacheItem.data.radius               = GetRadiusVal();
    m_lightCacheItem.data.outerAngle           = glm::cos(glm::radians(GetOuterAngleVal() * 0.5f));
    m_lightCacheItem.data.innerAngle           = glm::cos(glm::radians(GetInnerAngleVal() * 0.5f));
    m_lightCacheItem.data.direction            = GetComponentFast<DirectionComponent>()->GetDirection();
    m_lightCacheItem.data.projectionViewMatrix = m_shadowMapCameraProjectionViewMatrix;

    m_lightCacheItem.Validate();

    return m_lightCacheItem;
  }

  void SpotLight::InvalidateCacheItem() { m_lightCacheItem.Invalidate(); }

  XmlNode* SpotLight::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);
    return node;
  }

  XmlNode* SpotLight::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    XmlNode* node = Super::DeSerializeImp(info, parent);
    MeshGenerator::GenerateConeMesh(m_volumeMesh, GetRadiusVal(), 32, GetOuterAngleVal());
    return node;
  }

  void SpotLight::NativeConstruct()
  {
    Super::NativeConstruct();

    AddComponent<DirectionComponent>();
    m_volumeMesh = MakeNewPtr<Mesh>();

    MeshGenerator::GenerateConeMesh(m_volumeMesh, GetRadiusVal(), 32, GetOuterAngleVal());
  }

  void SpotLight::ParameterConstructor()
  {
    Super::ParameterConstructor();

    Radius_Define(10.0f, "Light", 90, true, true, {false, true, 0.1f, 100000.0f, 0.5f});
    OuterAngle_Define(35.0f, "Light", 90, true, true, {false, true, 1.0f, 179.8f, 1.0f});
    InnerAngle_Define(30.0f, "Light", 90, true, true, {false, true, 0.5f, 179.8f, 1.0f});
  }

  void SpotLight::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    ParamOuterAngle().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          float outer = std::get<float>(newVal);
          float inner = GetInnerAngleVal();

          if (outer < 1.0f)
          {
            outer             = 1.0f;
            ParamOuterAngle() = outer;
          }

          // provide a minimal falloff by pushing inner.
          float falloff = outer * 0.95f;
          if (inner > falloff)
          {
            ParamInnerAngle() = falloff;
          }

          MeshGenerator::GenerateConeMesh(m_volumeMesh, GetRadiusVal(), 32, outer);
          InvalidateSpatialCaches();
        });

    ParamInnerAngle().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          float outer   = GetOuterAngleVal();
          float inner   = std::get<float>(newVal);

          // provide a minimal falloff by pushing outer.
          float falloff = outer * 0.95f;
          if (inner > falloff)
          {
            ParamOuterAngle() = inner * 1.05f;
          }

          InvalidateSpatialCaches();
        });

    ParamRadius().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          const float radius = std::get<float>(newVal);
          MeshGenerator::GenerateConeMesh(m_volumeMesh, radius, 32, GetOuterAngleVal());
          InvalidateSpatialCaches();
        });
  }

} // namespace ToolKit
