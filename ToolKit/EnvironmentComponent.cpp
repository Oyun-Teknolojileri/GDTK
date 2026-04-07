/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "EnvironmentComponent.h"

#include "Entity.h"
#include "ForwardSceneRenderPath.h"
#include "MathUtil.h"
#include "RenderSystem.h"
#include "Renderer.h"
#include "Scene.h"
#include "Texture.h"
#include "ToolKit.h"

#include <DebugNew.h>

namespace ToolKit
{

  TKDefineClass(EnvironmentComponent, Component);

  EnvironmentComponent::EnvironmentComponent() {}

  EnvironmentComponent::~EnvironmentComponent() { UnInit(); }

  void EnvironmentComponent::Init(bool flushClientSideArray)
  {
    if (m_initialized)
    {
      return;
    }

    HdriPtr hdri = GetHdriVal();
    assert(hdri != nullptr && "Hdri on the environment component can't be null.");

    if (!hdri->IsDynamic())
    {
      hdri->Load();
    }

    String baseName = hdri->GenerateBakedEnvironmentFileBaseName();
    hdri->TrySettingCacheFiles(baseName);

    hdri->m_generateIrradianceCaches = true;
    hdri->Init(flushClientSideArray);

    UpdateBoundingBoxCache();
    m_initialized = true;
  }

  void EnvironmentComponent::UnInit() { m_initialized = false; }

  void EnvironmentComponent::ParameterConstructor()
  {
    Super::ParameterConstructor();

    Hdri_Define(nullptr, EnvironmentComponentCategory.Name, EnvironmentComponentCategory.Priority, true, true);

    PositionOffset_Define(Vec3(0.0f),
                          EnvironmentComponentCategory.Name,
                          EnvironmentComponentCategory.Priority,
                          true,
                          true,
                          {false, true, -FLT_MAX, FLT_MAX, 0.5f});

    Size_Define(Vec3(8.0f),
                EnvironmentComponentCategory.Name,
                EnvironmentComponentCategory.Priority,
                true,
                true,
                {false, true, 0.0f, 100000.0f, 0.5f});

    Illuminate_Define(true, EnvironmentComponentCategory.Name, EnvironmentComponentCategory.Priority, true, true);

    Intensity_Define(1.0f,
                     EnvironmentComponentCategory.Name,
                     EnvironmentComponentCategory.Priority,
                     true,
                     true,
                     {false, true, 0.0f, 100000.0f, 0.1f});

    Fade_Define(1.0f,
                EnvironmentComponentCategory.Name,
                EnvironmentComponentCategory.Priority,
                true,
                true,
                {false, true, 0.0f, 100000.0f, 0.1f});

    CaptureFar_Define(0.0f,
                      EnvironmentComponentCategory.Name,
                      EnvironmentComponentCategory.Priority,
                      true,
                      true,
                      {false, true, 0.0f, 100000.0f, 1.0f});

    CaptureResolution_Define(256,
                             EnvironmentComponentCategory.Name,
                             EnvironmentComponentCategory.Priority,
                             true,
                             true,
                             {false, true, 32, 2048, 1});

    Capture_Define(
        [this]() -> void
        {
          EntityPtr owner = OwnerEntity();
          if (owner == nullptr)
          {
            return;
          }

          uint res    = (uint) GetCaptureResolutionVal();

          // Compute world-space AABB from OBB for per-face far clipping.
          Vec3 offset = GetPositionOffsetVal();
          Vec3 half   = GetSizeVal() * 0.5f;

          BoundingBox localBB;
          localBB.min         = offset - half;
          localBB.max         = offset + half;

          Mat4 worldTransform = owner->m_node->GetTransform(TransformationSpace::TS_WORLD);

          // Place capture camera at the OBB center in world space.
          Vec3 position       = Vec3(worldTransform * Vec4(offset, 1.0f));

          Vec3Array corners;
          GetCorners(localBB, corners);

          float extraFar         = GetCaptureFarVal();

          // Compute per-face OBB clip distances.
          // Transform capture position into OBB local space, then compute
          // perpendicular distance to each of the 6 box walls.
          // Face order: +X(0), -X(1), -Y(2), +Y(3), +Z(4), -Z(5).
          Mat4 invWorldTransform = glm::inverse(worldTransform);
          Vec3 localPos          = Vec3(invWorldTransform * Vec4(position, 1.0f));

          float perFaceClipDist[6];
          perFaceClipDist[0] = localBB.max.x - localPos.x + extraFar; // +X
          perFaceClipDist[1] = localPos.x - localBB.min.x + extraFar; // -X
          perFaceClipDist[2] = localPos.y - localBB.min.y + extraFar; // -Y
          perFaceClipDist[3] = localBB.max.y - localPos.y + extraFar; // +Y
          perFaceClipDist[4] = localBB.max.z - localPos.z + extraFar; // +Z
          perFaceClipDist[5] = localPos.z - localBB.min.z + extraFar; // -Z

          // Clamp to a minimum positive value.
          float minDist      = 0.01f;
          for (int fi = 0; fi < 6; fi++)
          {
            perFaceClipDist[fi] = glm::max(perFaceClipDist[fi], minDist);
          }

          GetRenderSystem()->AddRenderTask(
              {[this, position, minDist, res, perFaceClipDist](Renderer* renderer) -> void
               {
                 // Disable self-illumination during capture to prevent feedback loop.
                 bool wasIlluminating = GetIlluminateVal();
                 SetIlluminateVal(false);

                 // Create a temporary render path for the capture.
                 ForwardSceneRenderPath capturePath;
                 capturePath.m_params.Scene = GetSceneManager()->GetCurrentScene();

                 CubeMapPtr cubemap =
                     renderer->RenderToCubeMap(&capturePath, position, minDist, 1000.0f, res, perFaceClipDist);

                 // Restore illuminate state.
                 SetIlluminateVal(wasIlluminating);

                 // Create a dynamic HDRI and assign the captured cubemap.
                 HdriPtr hdri    = MakeNewPtr<Hdri>();
                 hdri->m_cubemap = cubemap;
                 hdri->GenerateIrradianceCaches(renderer);
                 hdri->m_initiated = true;

                 SetHdriVal(hdri);
               }});
        },
        EnvironmentComponentCategory.Name,
        EnvironmentComponentCategory.Priority,
        true,
        true);

    auto createParameterVariant = [](const String& name, int val)
    {
      ParameterVariant param {val};
      param.m_name = name;
      return param;
    };
  }

  void EnvironmentComponent::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    ParamPositionOffset().m_onValueChangedFn.push_back([this](Value& oldVal, Value& newVal) -> void
                                                       { m_spatialCachesInvalidated = true; });

    ParamSize().m_onValueChangedFn.push_back([this](Value& oldVal, Value& newVal) -> void
                                             { m_spatialCachesInvalidated = true; });

    ParamHdri().m_onValueChangedFn.push_back(
        [this](Value& oldVal, Value& newVal) -> void
        {
          HdriPtr hdri = std::get<HdriPtr>(newVal);
          if (hdri != nullptr)
          {
            if (hdri->IsDynamic())
            {
              // This is a procedurally generated hdri.
              // Image and irradiance cache generation must be performed by the owner entity.
              return;
            }

            if (hdri->m_waitingForInit && hdri->m_generateIrradianceCaches)
            {
              // A generate is already in progress.
              return;
            }

            if (hdri->m_initiated && hdri->m_specularEnvMap && hdri->m_diffuseEnvMap)
            {
              // Already initialized.
              return;
            }
            else
            {
              String baseName = hdri->GenerateBakedEnvironmentFileBaseName();
              hdri->TrySettingCacheFiles(baseName);

              // Loaded as image and missing irradiance caches.
              if (hdri->m_loaded && hdri->m_initiated)
              {
                hdri->m_waitingForInit  = true;

                RenderSystem* renderSys = GetRenderSystem();
                if (!hdri->_diffuseBakeFile.empty() && !hdri->_specularBakeFile.empty())
                {
                  renderSys->AddRenderTask(
                      {[hdri](Renderer* renderer) -> void { hdri->LoadIrradianceCaches(renderer); }});
                }
                else
                {
                  renderSys->AddRenderTask(
                      {[hdri](Renderer* renderer) -> void { hdri->GenerateIrradianceCaches(renderer); }});
                }
              }
              else
              {
                hdri->m_generateIrradianceCaches = true;
                hdri->Load();
                hdri->Init();
              }
            }
          }
        });
  }

  ComponentPtr EnvironmentComponent::Copy(EntityPtr ntt)
  {
    EnvironmentComponentPtr ec = MakeNewPtr<EnvironmentComponent>();
    ec->m_localData            = m_localData;
    ec->m_entity               = ntt;

    return ec;
  }

  XmlNode* EnvironmentComponent::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    XmlNode* compNode = Super::DeSerializeImp(info, parent);
    return compNode->first_node(StaticClass()->Name.c_str());
  }

  XmlNode* EnvironmentComponent::SerializeImp(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* root = Super::SerializeImp(doc, parent);
    if (!m_serializableComponent)
    {
      return root;
    }

    XmlNode* node = CreateXmlNode(doc, StaticClass()->Name, root);

    return node;
  }

  const BoundingBox& EnvironmentComponent::GetBoundingBox()
  {
    if (m_spatialCachesInvalidated)
    {
      UpdateBoundingBoxCache();
    }

    return m_boundingBoxCache;
  }

  void EnvironmentComponent::UpdateBoundingBoxCache()
  {
    Vec3 offset = GetPositionOffsetVal();
    Vec3 half   = GetSizeVal() * 0.5f;

    // Local-space BB corners.
    BoundingBox localBB;
    localBB.min = offset - half;
    localBB.max = offset + half;

    if (EntityPtr owner = OwnerEntity())
    {
      // Transform local BB corners by entity world transform to get world-space enclosing AABB.
      Mat4 worldTransform = owner->m_node->GetTransform(TransformationSpace::TS_WORLD);

      Vec3Array corners;
      GetCorners(localBB, corners);

      m_boundingBoxCache.min = Vec3(FLT_MAX);
      m_boundingBoxCache.max = Vec3(-FLT_MAX);
      for (const Vec3& corner : corners)
      {
        Vec3 worldCorner       = Vec3(worldTransform * Vec4(corner, 1.0f));
        m_boundingBoxCache.min = glm::min(m_boundingBoxCache.min, worldCorner);
        m_boundingBoxCache.max = glm::max(m_boundingBoxCache.max, worldCorner);
      }
    }
    else
    {
      m_boundingBoxCache = localBB;
    }

    m_spatialCachesInvalidated = false;
  };

} // namespace ToolKit