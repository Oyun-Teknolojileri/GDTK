/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "LightProbeVolumeComponent.h"

#include "Entity.h"
#include "IGraphicsBackend.h"
#include "MathUtil.h"
#include "RenderSystem.h"
#include "Scene.h"
#include "Texture.h"
#include "ToolKit.h"

#include <DebugNew.h>

#include <filesystem>

namespace ToolKit
{

  namespace
  {
    /**
     * The probe grid is derived from the spacing, so these are the only numbers that keep a typo
     * from asking for a billion probes: a 5 km box at 5 cm spacing is 8000^3 cells.
     */
    constexpr float DefaultProbeSpacing = 8.0f;
    constexpr float MinProbeSpacing     = 0.05f;
    constexpr float MaxProbeSpacing     = 1000.0f;
    constexpr int MaxProbesPerAxis      = 256;

    constexpr float DefaultProbeTraceRadius = 2.0f;
    constexpr float MaxProbeTraceRadius     = 16.0f;

    constexpr int DefaultBakeSamples = 512;
    constexpr int MinBakeSamples     = 1;
    constexpr int MaxBakeSamples     = 16384;
    constexpr int MinBakeBounces     = 1;
    constexpr int MaxBakeBounces     = 16;
    constexpr int DefaultBakeBounces = 4;
    constexpr int MaxBakeSeed        = 1000000;
  } // namespace

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

  void LightProbeVolumeComponent::Update() {}

  void LightProbeVolumeComponent::UnInit()
  {
    if (m_shTexture != nullptr)
    {
      m_shTexture->UnInit();
      m_shTexture = nullptr;
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

    ProbeSpacing_Define(DefaultProbeSpacing,
                        LightProbeVolumeCategory.Name,
                        LightProbeVolumeCategory.Priority,
                        true,
                        true,
                        {false, true, MinProbeSpacing, MaxProbeSpacing, 0.5f});

    ProbeTraceRadius_Define(DefaultProbeTraceRadius,
                             LightProbeVolumeCategory.Name,
                             LightProbeVolumeCategory.Priority,
                             true,
                             true,
                             {false, true, 0.0f, MaxProbeTraceRadius, 0.5f});

    BakeSamples_Define(DefaultBakeSamples,
                       LightProbeVolumeCategory.Name,
                       LightProbeVolumeCategory.Priority,
                       true,
                       true,
                       {false, true, (float) MinBakeSamples, (float) MaxBakeSamples, 1.0f});

    BakeBounces_Define(DefaultBakeBounces,
                       LightProbeVolumeCategory.Name,
                       LightProbeVolumeCategory.Priority,
                       true,
                       true,
                       {false, true, (float) MinBakeBounces, (float) MaxBakeBounces, 1.0f});

    BakeSeed_Define(0,
                    LightProbeVolumeCategory.Name,
                    LightProbeVolumeCategory.Priority,
                    true,
                    true,
                    {false, true, 0.0f, (float) MaxBakeSeed, 1.0f});

    BakeDenoise_Define(2,
                       LightProbeVolumeCategory.Name,
                       LightProbeVolumeCategory.Priority,
                       true,
                       true,
                       {false, true, 0.0f, 8.0f, 1.0f});

    LightProbeVolumeComponentWeakPtr weakSelf = Self<LightProbeVolumeComponent>();
    FitToScene_Define(
        [weakSelf]() -> void
        {
          if (LightProbeVolumeComponentPtr self = weakSelf.lock())
          {
            self->FitToScene();
          }
        },
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
    //
    // The file stores the box in world space, the one the tracer covered, while Size and
    // PositionOffset are the node's local space. Writing the world box into them moved the volume
    // by the node's own transform - the box jumps the moment a bake lands and the probes then
    // sample where the geometry is not.
    BoundingBox fileBox;
    fileBox.min = m_data.boxMin;
    fileBox.max = m_data.boxMax;
    if (glm::all(glm::greaterThan(fileBox.max, fileBox.min)))
    {
      const BoundingBox local = WorldBoxToLocal(fileBox);

      // A box the author already sized wins: a bake is always traced from it, so the two agree,
      // and re-adopting on every load is what made a rotated node grow its box a little per bake
      // (the traced box is axis aligned, its local image is not). The file's box is only taken
      // when there is none yet, which is the "point a fresh volume at an existing bake" case.
      const Vec3 currentSize = GetSizeVal();
      if (!glm::all(glm::greaterThan(currentSize, Vec3(MinProbeSpacing))))
      {
        SetPositionOffsetVal((local.min + local.max) * 0.5f);
        SetSizeVal(local.max - local.min);
      }
      else
      {
        const Vec3 fileSize = local.max - local.min;
        const Vec3 drift    = glm::abs(fileSize - currentSize) / glm::max(currentSize, Vec3(MinProbeSpacing));
        if (glm::any(glm::greaterThan(drift, Vec3(0.05f))))
        {
          TK_WRN("Light probe volume: the bake '%s' was traced for a %.2f x %.2f x %.2f volume but "
                 "this one is %.2f x %.2f x %.2f. Re-bake it, or the probes will be sampled where "
                 "the grid has no data.",
                 path.c_str(),
                 fileSize.x,
                 fileSize.y,
                 fileSize.z,
                 currentSize.x,
                 currentSize.y,
                 currentSize.z);
        }
      }
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
    if (m_texturesValid && m_shTexture != nullptr)
    {
      return true;
    }

    if (!IsBaked())
    {
      return false;
    }

    const UVec3 resolution      = m_data.resolution;
    const uint coefficientCount = m_data.CoefficientCount();

    // The shader samples the quadratic set truncated to L1: four coefficients, one colour channel
    // per texture. A quadratic bake still loads - the extra coefficients are simply not read - and
    // it is not worth the space either way: a 2.25x larger file for a difference that does not show
    // on a surface, because the cosine convolution has already flattened the band a probe carries.
    if (coefficientCount < 4 || resolution.x == 0 || resolution.y == 0 || resolution.z == 0)
    {
      return false;
    }

    const uint width  = resolution.x;
    const uint height = resolution.y * resolution.z;
    const size_t layerTexels = (size_t) width * height;
    const size_t layerStride = layerTexels * 4;

    // The z slices are stacked vertically, so the texture is res.y * res.z rows tall. A grid the
    // device cannot hold would come back as an incomplete texture and sample black, so name the
    // limit and refuse instead of uploading it.
    int maxTextureSize = 0;
    if (RenderSystem* renderSystem = GetRenderSystem())
    {
      if (IGraphicsBackend* backend = renderSystem->GetBackend())
      {
        maxTextureSize = backend->GetMaxTextureSize();
      }
    }

    if (maxTextureSize > 0 && ((int) width > maxTextureSize || (int) height > maxTextureSize))
    {
      GetLogger()->Log(LogType::Error,
                       "Light probe volume: the L1 grid needs a %ux%u texture, but this device's "
                       "largest is %d. Rebake with a larger ProbeSpacing.",
                       width,
                       height,
                       maxTextureSize);
      return false;
    }

    TextureSettings settings;
    settings.Target         = GraphicTypes::Target2DArray;
    settings.Layers         = 3;
    settings.WarpS          = GraphicTypes::UVClampToEdge;
    settings.WarpT          = GraphicTypes::UVClampToEdge;
    settings.MinFilter      = GraphicTypes::SampleLinear;
    settings.MagFilter      = GraphicTypes::SampleLinear;
    settings.InternalFormat = GraphicTypes::FormatRGBA16F;
    settings.Format         = GraphicTypes::FormatRGBA;
    settings.Type           = GraphicTypes::TypeFloat;

    // Layer major, one layer per colour channel: the shader addresses a channel with the array
    // layer index, so one sampler carries all three.
    std::vector<float> pixels(layerStride * 3, 0.0f);

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
              pixels[(size_t) channel * layerStride + texel * 4 + coefficient] = m_data.coefficients[source];
            }
          }
        }
      }
    }

    DataTexturePtr texture = MakeNewPtr<DataTexture>(width, height, settings, "ProbeSH");
    texture->Init(pixels.data());
    m_shTexture = texture;

    m_texturesValid = true;

    // Log() rather than TK_LOG: this line is how a device build proves the array upload happened,
    // so it has to reach Log.txt and not only the editor console.
    GetLogger()->Log(LogType::Memo,
                     "Light probe volume: uploaded the L1 grid as %ux%u x 3 layers, %ux%ux%u probes.",
                     width,
                     height,
                     resolution.x,
                     resolution.y,
                     resolution.z);

    return true;
  }

  DataTexturePtr LightProbeVolumeComponent::GetShaderTexture() const { return m_shTexture; }

  const BoundingBox& LightProbeVolumeComponent::GetBoundingBox()
  {
    if (m_spatialCachesInvalidated)
    {
      UpdateBoundingBoxCache();
    }

    return m_boundingBoxCache;
  }

  void LightProbeVolumeComponent::InvalidateSpatialCaches() { m_spatialCachesInvalidated = true; }

  BoundingBox LightProbeVolumeComponent::WorldBoxToLocal(const BoundingBox& worldBox)
  {
    BoundingBox local = worldBox;

    EntityPtr owner = OwnerEntity();
    if (owner == nullptr)
    {
      return local;
    }

    const Mat4 toLocal = glm::inverse(owner->m_node->GetTransform(TransformationSpace::TS_WORLD));

    Vec3Array corners;
    GetCorners(worldBox, corners);

    local.min = Vec3(FLT_MAX);
    local.max = Vec3(-FLT_MAX);
    for (const Vec3& corner : corners)
    {
      const Vec3 localCorner = Vec3(toLocal * Vec4(corner, 1.0f));
      local.min              = glm::min(local.min, localCorner);
      local.max              = glm::max(local.max, localCorner);
    }

    return local;
  }

  void LightProbeVolumeComponent::FitToScene()
  {
    EntityPtr owner = OwnerEntity();
    if (owner == nullptr)
    {
      return;
    }

    ScenePtr scene = owner->m_scene.lock();
    if (scene == nullptr)
    {
      return;
    }

    BoundingBox bounds = scene->GetSceneBoundary();
    const Vec3 extent  = bounds.max - bounds.min;
    if (glm::any(glm::lessThanEqual(extent, Vec3(0.0f))))
    {
      return;
    }

    // Same padding the ProbeBake tool fits when it is given no box.
    const float padding =
        glm::max(glm::max(extent.x, glm::max(extent.y, extent.z)) * 0.02f, 0.1f);
    bounds.min -= Vec3(padding);
    bounds.max += Vec3(padding);

    // The scene bound is world space; Size and PositionOffset are the node's local space.
    const BoundingBox local = WorldBoxToLocal(bounds);
    SetSizeVal(local.max - local.min);
    SetPositionOffsetVal((local.min + local.max) * 0.5f);
  }

  UVec3 LightProbeVolumeComponent::ComputeBakeResolution() const
  {
    const Vec3 size    = glm::max(GetSizeVal(), Vec3(MinProbeSpacing));
    const float spacing = glm::clamp(GetProbeSpacingVal(), MinProbeSpacing, MaxProbeSpacing);

    // A box that is an exact multiple of the spacing lands a probe on both faces, so the cell
    // count is rounded rather than ceiled; one probe per axis is the floor.
    const Vec3 cells = glm::round(size / spacing);
    const Vec3 clamped = glm::clamp(cells, Vec3(1.0f), Vec3((float) MaxProbesPerAxis));
    return UVec3(clamped);
  }

  String LightProbeVolumeComponent::ResolveBakeFile()
  {
    const String file = GetBakeFileVal();
    if (!file.empty())
    {
      return file;
    }

    // Nothing named yet: default next to the scene that owns the volume, so a bake started from the
    // editor lands where the pack step and a hand written path both look for it.
    String sceneName = "scene";
    if (EntityPtr owner = OwnerEntity())
    {
      if (ScenePtr scene = owner->m_scene.lock())
      {
        String name, ext;
        DecomposePath(scene->GetFile(), nullptr, &name, &ext);
        if (!name.empty())
        {
          sceneName = name;
        }
      }
    }

    return ConcatPaths({"LightProbes", sceneName + ".pgi"});
  }

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
