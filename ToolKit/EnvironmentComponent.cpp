/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "EnvironmentComponent.h"

#include "Entity.h"
#include "MathUtil.h"
#include "RenderSystem.h"
#include "Texture.h"

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
    Vec3 pos;
    if (EntityPtr owner = OwnerEntity())
    {
      pos += owner->m_node->GetTranslation(TransformationSpace::TS_WORLD);
    }

    m_boundingBoxCache.min     = GetPositionOffsetVal() + pos - GetSizeVal() * 0.5f;
    m_boundingBoxCache.max     = GetPositionOffsetVal() + pos + GetSizeVal() * 0.5f;
    m_spatialCachesInvalidated = false;
  };

} // namespace ToolKit