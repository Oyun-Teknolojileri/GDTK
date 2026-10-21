/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
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
#include "Stats.h"
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

    Color_Define(Vec3(1.0f), "Light", 0, true, true, Animatable({true}));
    Intensity_Define(1.0f, "Light", 90, true, true, Animatable({false, true, 0.0f, 100000.0f, 0.1f}));
    CastShadow_Define(false, "Light", 90, true, true, Animatable({true}));
    ShadowBias_Define(0.2f, "Light", 90, true, true, {false, true, 0.0f, 10.0f, 0.01f});
    BleedingReduction_Define(0.1f, "Light", 90, true, true, {false, true, 0.0f, 1.0f, 0.001f});
  }

  void Light::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    // A light edit invalidates the objects around it, not just its own cache entry. Their render job
    // keeps the light list it was assigned, and the light's place in the GPU light buffer is only
    // refreshed when a draw asks for it, so a light that changed while its objects were out of view
    // would otherwise keep the old value the next time they are drawn.
    const auto onLightEdited = [this](Value&, Value&) -> void
    {
      InvalidateCacheItem();
      m_forceNeighbourInvalidation = true;
      InvalidateSpatialCaches();
    };

    ParamColor().m_onValueChangedFn.push_back(onLightEdited);
    ParamIntensity().m_onValueChangedFn.push_back(onLightEdited);
    ParamCastShadow().m_onValueChangedFn.push_back(onLightEdited);
    ParamShadowBias().m_onValueChangedFn.push_back(onLightEdited);
    ParamBleedingReduction().m_onValueChangedFn.push_back(onLightEdited);
  }

  void Light::UpdateShadowCamera() { m_shadowMapCameraProjectionViewMatrix = m_shadowCamera->GetProjectViewMatrix(); }

  float Light::AffectDistance() { return 1000.0f; }

  bool Light::HasValidShadowSlot() const
  {
    for (int shadowAtlasLayer : m_shadowAtlasLayers)
    {
      if (shadowAtlasLayer >= 0)
      {
        return true;
      }
    }
    return false;
  }

  void Light::InvalidateShadowAtlasSlot()
  {
    for (size_t i = 0; i < m_shadowAtlasLayers.size(); i++)
    {
      m_shadowAtlasLayers[i] = -1;
      m_shadowAtlasCoords[i] = Vec2(-1.0f);
    }
    m_shadowResolution = 0.0f;
  }

  void Light::InvalidateSpatialCaches()
  {
    Super::InvalidateSpatialCaches();
    InvalidateCacheItem();

    // A light that moves changes which objects it reaches. An object's light assignment is cached
    // with its render job, so the objects around the light have to be invalidated with it, at the
    // position it is now and at the one it left, or an object it walked away from keeps the light
    // and an object it walked up to never receives it.
    //
    // A directional light reaches everything from any position, so there is nothing positional to
    // invalidate and the walk is skipped. So is a re-entry through another light, which would
    // otherwise ping pong between two lights that sit close together.
    const LightType type = GetLightType();
    if (type == LightType::Directional || m_invalidatingNeighbours || m_node == nullptr)
    {
      return;
    }

    ScenePtr scene = m_scene.lock();
    if (scene == nullptr)
    {
      return;
    }

    float radius = 0.0f;
    if (type == LightType::Point)
    {
      radius = static_cast<PointLight*>(this)->GetRadiusVal();
    }
    else
    {
      radius = static_cast<SpotLight*>(this)->GetRadiusVal();
    }

    if (radius <= 0.0f)
    {
      return;
    }

    m_invalidatingNeighbours = true;

    const Vec3 now = m_node->GetTranslation(TransformationSpace::TS_WORLD);

    // A light that inches forward does not change which objects it reaches, and the walk below is
    // the expensive part of a moving light. The step is relative to the radius so it scales with
    // the light, and a radius change invalidates on its own even when the light stayed put.
    constexpr float kInvalidationStep = 0.05f;
    const bool radiusChanged          = m_lastInvalidationRadius != radius;
    const bool forced                 = m_forceNeighbourInvalidation;
    m_forceNeighbourInvalidation      = false;
    if (!forced && m_lastInvalidationPosition != Vec3(TK_FLT_MAX) && !radiusChanged)
    {
      const float moved = glm::distance(now, m_lastInvalidationPosition);
      if (moved < radius * kInvalidationStep)
      {
        m_invalidatingNeighbours = false;
        return;
      }
    }

    // The tree query is a box, which covers about 1.7 times the volume of the sphere the light
    // actually reaches, and the objects near the corners were invalidated for nothing.
    BoundingBox query(now - Vec3(radius), now + Vec3(radius));
    BoundingSphere nowSphere;
    nowSphere.pos    = now;
    nowSphere.radius = radius;
    const bool hasPrevious = m_lastInvalidationPosition != Vec3(TK_FLT_MAX);

    // The previous sphere is built with the previous radius, not the current one. Shrinking a light is
    // the case that needs it: the objects it just lost sit outside the new sphere, so with the new
    // radius they are never visited, they keep the light in their cached assignment, and nothing else
    // would ever correct them because they do not move.
    const float previousRadius = m_lastInvalidationRadius > 0.0f ? m_lastInvalidationRadius : radius;
    BoundingSphere previousSphere;
    previousSphere.pos    = m_lastInvalidationPosition;
    previousSphere.radius = previousRadius;
    if (hasPrevious)
    {
      query = BoundingBox::Union(query,
                                 BoundingBox(m_lastInvalidationPosition - Vec3(previousRadius),
                                             m_lastInvalidationPosition + Vec3(previousRadius)));
    }

    EntityRawPtrArray touched = scene->m_aabbTree.VolumeQuery(query, false);
    for (Entity* ntt : touched)
    {
      if (ntt == this || ntt->As<Light>() != nullptr)
      {
        continue;
      }

      // An object has to be refreshed when it is in reach now, and also when it was in reach before
      // this move: the light walked away from it and its light list still has to lose it.
      const BoundingBox& box = ntt->GetBoundingBox(true);
      if (!SphereBoxIntersection(nowSphere, box) &&
          !(hasPrevious && SphereBoxIntersection(previousSphere, box)))
      {
        continue;
      }

      ntt->InvalidateLightAssignment();
    }

    m_lastInvalidationPosition = now;
    m_lastInvalidationRadius   = radius;
    m_invalidatingNeighbours   = false;
  }

  void Light::InvalidateAssignment()
  {
    // The walk below skips itself when the light did not move far enough, which is exactly the case a
    // turn is: the light stands still while its cone covers different objects.
    m_forceNeighbourInvalidation = true;
    InvalidateSpatialCaches();
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
    data->color               = GetColorVal();
    data->intensity           = GetIntensityVal();
    data->position            = m_node->GetTranslation(TransformationSpace::TS_WORLD);
    data->castShadow          = GetCastShadowVal() && HasValidShadowSlot();
    data->shadowBias          = GetShadowBiasVal() * RHIConstants::ShadowBiasMultiplier;
    data->bleedingReduction   = GetBleedingReductionVal();
    data->pad0                = 0.0f;
    data->pad1                = 0.0f;
    float atlasSize           = (float) GetEngineSettings().m_graphics->m_shadows->GetShadowAtlasResolution();
    data->shadowAtlasResRatio = m_shadowResolution / atlasSize;
    data->shadowAtlasLayer    = m_shadowAtlasLayers[0];
    data->shadowAtlasCoord    = m_shadowAtlasCoords[0];
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
    m_lightDataBuffer.m_slot = ReservedUniformBufferSlots::DirectionalLightBuffer;

    m_pvms.Init(m_pvmDataSize);
    m_pvms.m_slot = ReservedUniformBufferSlots::DirectionalLightPVMBuffer;
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

    Stats::AddStat(FrameStatType::DirectionalLightUpdate, 2); // Includes pvm updates.
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

    // Prevents light leaking when view near is far away. Don't use shadowMinDistance here. It does
    // not provide float precision since the projection is orthogonal.
    nearClip                   = 0.001f;

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
  bool DirectionalLight::IsCacheItemDirty() const { return !m_lightCacheItem.isValid; }

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

      // Quantize radius to absorb floating point noise.
      radius                = glm::ceil(radius * 2.0f) / 2.0f;
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
    float shadowMapRes                     = m_shadowResolution;
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
    m_gpuBuffer.m_slot = ReservedUniformBufferSlots::PointLightCache;
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
  bool PointLight::IsCacheItemDirty() const { return !m_lightCacheItem.isValid; }

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
    m_gpuBuffer.m_slot = ReservedUniformBufferSlots::SpotLightCache;
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

    // A turn moves the cone over different objects while the light itself stands still, so the objects
    // it now covers only get the light if their assignment is refreshed. Measured once a frame here and
    // not in the invalidation: that runs from inside the node's transform update, and asking the node
    // for its orientation from there forces the update to run again.
    const Quaternion orientation = m_node->GetOrientation(TransformationSpace::TS_WORLD);
    if (glm::abs(glm::dot(orientation, m_lastAssignmentOrientation)) < 0.9999f)
    {
      m_lastAssignmentOrientation = orientation;
      InvalidateAssignment();
    }

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
  bool SpotLight::IsCacheItemDirty() const { return !m_lightCacheItem.isValid; }

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
          m_forceNeighbourInvalidation = true;
          InvalidateSpatialCaches();
        });
  }

} // namespace ToolKit
