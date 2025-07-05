/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Entity.h"
#include "RHI.h"

namespace ToolKit
{
  // LightCacheItem
  //////////////////////////////////////////

  struct TK_API LightCacheItem : CacheItem
  {
    struct CommonData
    {
      Vec3 color;              //!< Color of the light.
      float intensity;         //!< Intensity of the light.
      Vec3 position;           //!< World position for light.
      int castShadow;          //!< States if the light casts shadow or not.
      float shadowBias;        //!< Bias for shadow map generation.
      float bleedingReduction; //!< Reduces shadow bleeding artifacts.
      float pcfRadius;         //!< Radius for PCF shadow filtering.
      int pcfSamples;          //!< Number of samples for PCF shadow filtering.
      Vec2 shadowAtlasCoord;   //!< Start coordinates of the shadow map in the texture.
      float shadowResolution;  //!< Shadow resolution in pixels.
      int shadowAtlasLayer;    //!< Shows which index the shadow texture is in.
    };
  };

  // Light
  //////////////////////////////////////////

  class TK_API Light : public Entity, public ICacheable
  {
   public:
    TKDeclareClass(Light, Entity);

    Light();
    virtual ~Light();

    void NativeConstruct() override;

    virtual void UpdateShadowCamera();
    virtual float AffectDistance();

    /**
     * Returns  0 to 3 number that helps to sort lights by type. DirectionalLight: 0, PointLight: 1, SpotLight: 3.
     * Required for fast iterations. IsA still valid option to use but slower if it will be called 10k or more times.
     */
    enum LightType
    {
      Directional,
      Point,
      Spot
    };

    virtual LightType GetLightType() const = 0;

   protected:
    void InvalidateSpatialCaches() override;

    void UpdateShadowCameraTransform();
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

    /** Sets volume meshes boundary as local bounding box. */
    void UpdateLocalBoundingBox() override;

    /** Fills cache items common light data. */
    void UpdateCommonLightData(LightCacheItem::CommonData* data);

   public:
    TKDeclareParam(Vec3, Color);
    TKDeclareParam(float, Intensity);
    TKDeclareParam(bool, CastShadow);
    TKDeclareParam(MultiChoiceVariant, ShadowRes);
    TKDeclareParam(float, PCFRadius);
    TKDeclareParam(float, ShadowBias);
    TKDeclareParam(float, BleedingReduction);

    Mat4 m_shadowMapCameraProjectionViewMatrix;
    CameraPtr m_shadowCamera       = nullptr;
    bool m_shadowResolutionUpdated = false;
    MeshPtr m_volumeMesh           = nullptr;

    IntArray m_shadowAtlasLayers;  //!< Layer index in the shadow atlas for each cascade.
    Vec2Array m_shadowAtlasCoords; //!< Coordinates for each cascade in the corresponding layer.
  };

  // DirectionalLightCacheItem
  //////////////////////////////////////////

  struct TK_API DirectionalLightCacheItem : LightCacheItem
  {
    struct Data : LightCacheItem::CommonData
    {
      Vec3 direction; //<! Direction for directional and spot lights.
      float pad0;
    } data;

    void* GetData() override { return &data; }
  };

  // DirectionalLightBuffer
  //////////////////////////////////////////

  /**
   * This class holds all active directional light data and corresponding project view matrices for shadow calculation.
   * Require tobe updated once per frame. Its not a cache because it suppose to contain all active light data.
   */
  class TK_API DirectionalLightBuffer
  {
   public:
    DirectionalLightBuffer();
    virtual ~DirectionalLightBuffer();

    void Init();
    void Map(const LightRawPtrArray& lights);

   public:
    static constexpr int BindingSlotForLight = 7;
    static constexpr int BindingSlotForPVM   = 10;

    /** All data containing directional light properties. */
    UniformBuffer m_lightDataBuffer;

    /** Project view matrices for each cascade for all lights. */
    UniformBuffer m_pvms;

   private:
    byte* m_lightData;
    int m_lightDataSize;

    byte* m_pvmData;
    int m_pvmDataSize;
  };

  // DirectionalLight
  //////////////////////////////////////////

  class TK_API DirectionalLight : public Light
  {
   public:
    TKDeclareClass(DirectionalLight, Light);

    DirectionalLight();
    virtual ~DirectionalLight();

    void NativeConstruct() override;
    void UpdateShadowFrustum(CameraPtr cameraView, ScenePtr scene);

    void UpdateShadowCamera() override;

    LightType GetLightType() const override { return LightType::Directional; }

    const DirectionalLightCacheItem& GetCacheItem() override;

    void InvalidateCacheItem() override;

   protected:
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;

   private:
    /** Adjust the light frustum such that, it covers entire view camera frustum. */
    void FitViewFrustumIntoLightFrustum(CameraPtr lightCamera,
                                        CameraPtr viewCamera,
                                        float near,
                                        float far,
                                        bool stableFit);

   public:
    /** Cascades are rendered with these cameras, due to stable fit, frustum can be larger than actual coverage. */
    CameraPtrArray m_cascadeShadowCameras;

    /** Scene is culled with these tightly fit cameras to create render jobs for shadow map generation. */
    CameraPtrArray m_cascadeCullCameras;

    /** Cascade camera projection matrices to fill the light buffer. */
    Mat4Array m_shadowMapCascadeCameraProjectionViewMatrices;

   private:
    /** Gpu representation of the light. */
    DirectionalLightCacheItem m_lightCacheItem;
  };

  typedef std::shared_ptr<DirectionalLight> DirectionalLightPtr;

  // PointLightCacheItem
  //////////////////////////////////////////

  struct TK_API PointLightCacheItem : LightCacheItem
  {
    struct Data : LightCacheItem::CommonData
    {
      float radius;
      float pad0, pad1, pad2;
    } data;

    void* GetData() override { return &data; }
  };

  // PointLightCache
  //////////////////////////////////////////

  class TK_API PointLightCache : public LRUCache<PointLightCacheItem, sizeof(PointLightCacheItem::Data)>
  {
   public:
    PointLightCache();
    virtual ~PointLightCache();

    void Init();
    bool Map();

   public:
    static constexpr int BindingSlot = 8;
    UniformBuffer m_gpuBuffer;
  };

  // PointLight
  //////////////////////////////////////////

  class TK_API PointLight : public Light
  {
   public:
    TKDeclareClass(PointLight, Light);

    PointLight();
    virtual ~PointLight();

    LightType GetLightType() const override { return LightType::Point; }

    void UpdateShadowCamera() override;
    float AffectDistance() override;

    const PointLightCacheItem& GetCacheItem() override;
    void InvalidateCacheItem() override;

   protected:
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;
    void UpdateLocalBoundingBox() override;

   public:
    TKDeclareParam(float, Radius);

    /** World space Bounding volume, updated after call to UpdateShadowCamera(). */
    BoundingSphere m_boundingSphereCache;

   private:
    /** Gpu representation of the light. */
    PointLightCacheItem m_lightCacheItem;
  };

  typedef std::shared_ptr<PointLight> PointLightLightPtr;

  // SpotLightCacheItem
  //////////////////////////////////////////

  struct TK_API SpotLightCacheItem : LightCacheItem
  {
    struct Data : LightCacheItem::CommonData
    {
      Vec3 direction;
      float radius;

      float outerAngle; //!< Outer angle in degrees.
      float innerAngle; //!< Inner angle in degrees.
      float pad0, pad1;

      Mat4 projectionViewMatrix;
    } data;

    void* GetData() override { return &data; }
  };

  // SpotLightCache
  //////////////////////////////////////////

  class TK_API SpotLightCache : public LRUCache<SpotLightCacheItem, sizeof(SpotLightCacheItem::Data)>
  {
   public:
    SpotLightCache();
    virtual ~SpotLightCache();

    void Init();
    bool Map();

   public:
    static constexpr int BindingSlot = 9;
    UniformBuffer m_gpuBuffer;
  };

  // SpotLight
  //////////////////////////////////////////

  class TK_API SpotLight : public Light
  {
   public:
    TKDeclareClass(SpotLight, Light);

    SpotLight();
    virtual ~SpotLight();

    void NativeConstruct() override;

    LightType GetLightType() const override { return LightType::Spot; }

    void UpdateShadowCamera() override;
    float AffectDistance() override;

    const SpotLightCacheItem& GetCacheItem() override;
    void InvalidateCacheItem() override;

   protected:
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;

   public:
    TKDeclareParam(float, Radius);
    TKDeclareParam(float, OuterAngle);
    TKDeclareParam(float, InnerAngle);

    /** Spot frustum, updated after call to UpdateShadowCamera(). */
    Frustum m_frustumCache;

    /**
     * Stores world space bounding box that encapsulates the spot frustum.
     * Used to cull against camera frustum.  Frustum vs Frustum would yield more prices results thus more
     * culled lights.
     */
    BoundingBox m_boundingBoxCache;

   private:
    /** Gpu representation of the light. */
    SpotLightCacheItem m_lightCacheItem;
  };

  typedef std::shared_ptr<SpotLight> SpotLightPtr;

} // namespace ToolKit
