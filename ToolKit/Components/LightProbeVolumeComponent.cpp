/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "LightProbeVolumeComponent.h"

#include "Entity.h"
#include "MathUtil.h"
#include "Scene.h"
#include "Texture.h"
#include "ToolKit.h"

#include <DebugNew.h>

#include <filesystem>

namespace ToolKit
{

  TKDefineClass(LightProbeVolumeComponent, Component);

  LightProbeVolumeComponent::LightProbeVolumeComponent() {}

  LightProbeVolumeComponent::~LightProbeVolumeComponent()
  {
    UnInit();

    if (EntityPtr owner = OwnerEntity())
    {
      if (ScenePtr scene = owner->m_scene.lock())
      {
        scene->RemoveProbeVolume(this);
      }
    }
  }

  void LightProbeVolumeComponent::Init(bool flushClientSideArray)
  {
    if (m_initialized)
    {
      return;
    }

    UpdateBoundingBoxCache();
    m_initialized = true;

    if (EntityPtr owner = OwnerEntity())
    {
      if (ScenePtr scene = owner->m_scene.lock())
      {
        scene->AddProbeVolume(Self<LightProbeVolumeComponent>());
      }
    }

    if (IsBaked())
    {
      return;
    }

    // Nothing on disk is not an error: a scene that was never baked simply contributes nothing.
    if (!GetBakeFileVal().empty())
    {
      LoadBake();
    }
  }

  void LightProbeVolumeComponent::UnInit()
  {
    for (DataTexturePtr& texture : m_shTextures)
    {
      if (texture != nullptr)
      {
        texture->UnInit();
        texture = nullptr;
      }
    }

    m_texturesValid = false;
  }

  ComponentPtr LightProbeVolumeComponent::Copy(EntityPtr ntt)
  {
    LightProbeVolumeComponentPtr copy = MakeNewPtr<LightProbeVolumeComponent>();
    copy->m_localData                 = m_localData;
    copy->m_entity                    = ntt;
    copy->m_data                      = m_data;
    copy->m_spatialCachesInvalidated  = m_spatialCachesInvalidated;

    return copy;
  }

  void LightProbeVolumeComponent::ParameterConstructor()
  {
    Super::ParameterConstructor();

    Size_Define(Vec3(10.0f),
                LightProbeVolumeCategory.Name,
                LightProbeVolumeCategory.Priority,
                true,
                true,
                {false, true, 0.01f, 100000.0f, 0.5f});

    PositionOffset_Define(Vec3(0.0f),
                          LightProbeVolumeCategory.Name,
                          LightProbeVolumeCategory.Priority,
                          true,
                          true,
                          {false, true, -100000.0f, 100000.0f, 0.1f});

    BakeFile_Define(String(""),
                    LightProbeVolumeCategory.Name,
                    LightProbeVolumeCategory.Priority,
                    true,
                    true);

    Intensity_Define(1.0f,
                     LightProbeVolumeCategory.Name,
                     LightProbeVolumeCategory.Priority,
                     true,
                     true,
                     {false, true, 0.0f, 100000.0f, 0.1f});

    AmbientOcclusionStrength_Define(1.0f,
                                    LightProbeVolumeCategory.Name,
                                    LightProbeVolumeCategory.Priority,
                                    true,
                                    true,
                                    {false, true, 0.0f, 1.0f, 0.05f});
  }

  void LightProbeVolumeComponent::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    ParamSize().m_onValueChangedFn.push_back([this](Value&, Value&) -> void { InvalidateSpatialCaches(); });

    ParamPositionOffset().m_onValueChangedFn.push_back(
        [this](Value&, Value&) -> void { InvalidateSpatialCaches(); });

    ParamBakeFile().m_onValueChangedFn.push_back(
        [this](Value&, Value&) -> void
        {
          UnloadBake();

          // The editor adds a component to an entity that is already in the scene, so nothing
          // re-runs Scene::UpdateEntityCaches for it. Init is what registers the component with
          // the scene cache and loads the bake, so it is run again here.
          m_initialized = false;
          Init(true);
        });
  }

  bool LightProbeVolumeComponent::LoadBake()
  {
    UnloadBake();

    const String file = GetBakeFileVal();
    if (file.empty())
    {
      return false;
    }

    // Relative to the project's Resources folder, which is how the editor names every resource.
    // An absolute path is taken as it is, so a bake outside the project can still be previewed.
    String path = file;
    if (!std::filesystem::path(file).is_absolute())
    {
      path = ConcatPaths({ResourcePath(), file});
    }

    if (!LoadProbeVolume(path, m_data))
    {
      TK_WRN("Light probe volume '%s' could not load its bake '%s'.", StaticClass()->Name.c_str(), path.c_str());
      m_data = ProbeVolumeData {};
      return false;
    }

    m_texturesValid = false;

    // The box belongs to the bake: the coefficients only mean something inside the volume they
    // were traced in. Adopting the file's box removes the chance of an author sizing the box by
    // hand and sampling the grid where it has no data.
    const Vec3 boxMin = m_data.boxMin;
    const Vec3 boxMax = m_data.boxMax;
    if (glm::all(glm::greaterThan(boxMax, boxMin)))
    {
      SetPositionOffsetVal((boxMin + boxMax) * 0.5f);
      SetSizeVal(boxMax - boxMin);
    }

    InvalidateSpatialCaches();

    // The render job cache holds the probe volume each job was assigned, so a bake that arrives
    // after the cache was built has to force a rebuild or the volume never reaches the shader.
    if (EntityPtr owner = OwnerEntity())
    {
      if (ScenePtr scene = owner->m_scene.lock())
      {
        scene->BumpCacheVersion();
      }
    }

    TK_LOG("Light probe volume: loaded '%s' (%ux%ux%u probes, %s).",
           path.c_str(),
           m_data.resolution.x,
           m_data.resolution.y,
           m_data.resolution.z,
           m_data.CoefficientCount() == 4 ? "L1" : "L2");

    return true;
  }

  void LightProbeVolumeComponent::UnloadBake()
  {
    UnInit();
    m_data = ProbeVolumeData {};
  }

  bool LightProbeVolumeComponent::EnsureShaderTextures()
  {
    if (m_texturesValid && m_shTextures[0] != nullptr)
    {
      return true;
    }

    if (!IsBaked())
    {
      return false;
    }

    const UVec3 resolution = m_data.resolution;
    const uint coefficientCount = m_data.CoefficientCount();

    // The shader samples the quadratic set truncated to L1: four coefficients, one colour
    // channel per texture.
    if (coefficientCount < 4 || resolution.x == 0 || resolution.y == 0 || resolution.z == 0)
    {
      return false;
    }

    const uint width  = resolution.x;
    const uint height = resolution.y * resolution.z;

    TextureSettings settings;
    settings.Target         = GraphicTypes::Target2D;
    settings.WarpS          = GraphicTypes::UVClampToEdge;
    settings.WarpT          = GraphicTypes::UVClampToEdge;
    settings.MinFilter      = GraphicTypes::SampleLinear;
    settings.MagFilter      = GraphicTypes::SampleLinear;
    settings.InternalFormat = GraphicTypes::FormatRGBA16F;
    settings.Format         = GraphicTypes::FormatRGBA;
    settings.Type           = GraphicTypes::TypeFloat;

    std::vector<float> pixels[3];
    for (int channel = 0; channel < 3; ++channel)
    {
      pixels[channel].assign((size_t) width * height * 4, 0.0f);
    }

    for (uint z = 0; z < resolution.z; ++z)
    {
      for (uint y = 0; y < resolution.y; ++y)
      {
        for (uint x = 0; x < resolution.x; ++x)
        {
          const size_t probe = ((size_t) z * resolution.y + y) * resolution.x + x;
          const size_t texel = ((size_t) z * resolution.y + y) * width + x;

          for (int channel = 0; channel < 3; ++channel)
          {
            for (uint coefficient = 0; coefficient < 4; ++coefficient)
            {
              const size_t source = (probe * coefficientCount + coefficient) * 3 + channel;
              pixels[channel][texel * 4 + coefficient] = m_data.coefficients[source];
            }
          }
        }
      }
    }

    for (int channel = 0; channel < 3; ++channel)
    {
      DataTexturePtr texture = MakeNewPtr<DataTexture>(width, height, settings, "ProbeSH");
      texture->Init(pixels[channel].data());
      m_shTextures[channel] = texture;
    }

    m_texturesValid = true;

    TK_LOG("Light probe volume: uploaded the L1 grid as %ux%u, %ux%ux%u probes.",
           width,
           height,
           resolution.x,
           resolution.y,
           resolution.z);

    return true;
  }

  DataTexturePtr LightProbeVolumeComponent::GetShaderTexture(int index) const
  {
    if (index < 0 || index >= 3)
    {
      return nullptr;
    }
    return m_shTextures[index];
  }

  const BoundingBox& LightProbeVolumeComponent::GetBoundingBox()
  {
    if (m_spatialCachesInvalidated)
    {
      UpdateBoundingBoxCache();
    }

    return m_boundingBoxCache;
  }

  void LightProbeVolumeComponent::InvalidateSpatialCaches() { m_spatialCachesInvalidated = true; }

  void LightProbeVolumeComponent::UpdateBoundingBoxCache()
  {
    const Vec3 offset = GetPositionOffsetVal();
    const Vec3 half   = GetSizeVal() * 0.5f;

    BoundingBox local;
    local.min = offset - half;
    local.max = offset + half;

    if (EntityPtr owner = OwnerEntity())
    {
      const Mat4 worldTransform = owner->m_node->GetTransform(TransformationSpace::TS_WORLD);

      Vec3Array corners;
      GetCorners(local, corners);

      m_boundingBoxCache.min = Vec3(FLT_MAX);
      m_boundingBoxCache.max = Vec3(-FLT_MAX);
      for (const Vec3& corner : corners)
      {
        const Vec3 worldCorner = Vec3(worldTransform * Vec4(corner, 1.0f));
        m_boundingBoxCache.min = glm::min(m_boundingBoxCache.min, worldCorner);
        m_boundingBoxCache.max = glm::max(m_boundingBoxCache.max, worldCorner);
      }
    }
    else
    {
      m_boundingBoxCache = local;
    }

    m_spatialCachesInvalidated = false;
  }

} // namespace ToolKit
