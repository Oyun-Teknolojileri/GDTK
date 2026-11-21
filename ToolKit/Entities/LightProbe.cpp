/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "LightProbe.h"

#include "LightProbeVolumeComponent.h"
#include "Scene.h"

#include <DebugNew.h>

namespace ToolKit
{

  namespace
  {
    /**
     * A new volume the user can see and grab before any scene specific size is known. The spacing
     * default comes from the component, so the entity only has to pick a box that fits in a level.
     */
    const Vec3 DefaultProbeVolumeSize = Vec3(40.0f, 20.0f, 40.0f);

    constexpr float DefaultProbeSpacing = 8.0f;
    constexpr int DefaultBakeSamples    = 512;
    constexpr int DefaultBakeBounces    = 4;
  } // namespace

  TKDefineClass(LightProbe, Entity);

  LightProbe::LightProbe() {}

  LightProbe::~LightProbe() {}

  LightProbeVolumeComponentPtr LightProbe::GetProbeVolume()
  {
    return GetComponent<LightProbeVolumeComponent>();
  }

  void LightProbe::NativeConstruct()
  {
    Super::NativeConstruct();

    SetNameVal("LightProbe");

    // Through the factory, so an editor build gets its own component subclass. Falling back to the
    // engine type keeps a headless host (the ProbeBake tool, a game) working with no registration.
    ComponentPtr comp = MakeNewPtrCasted<Component>(LightProbeVolumeComponent::StaticClass()->Name);
    if (comp == nullptr)
    {
      comp = MakeNewPtr<LightProbeVolumeComponent>();
    }

    LightProbeVolumeComponentPtr probe = SafeCast<LightProbeVolumeComponent>(comp);
    if (probe == nullptr)
    {
      return;
    }

    probe->SetSizeVal(DefaultProbeVolumeSize);

    // Sit the box on the node instead of centring it, which is where a level's playable space is.
    probe->SetPositionOffsetVal(Vec3(0.0f, DefaultProbeVolumeSize.y * 0.5f, 0.0f));

    AddComponent(probe);

    HideVolumeParameters();
    SyncFromComponent();
  }

  void LightProbe::HideVolumeParameters()
  {
    if (LightProbeVolumeComponentPtr probe = GetProbeVolume())
    {
      probe->m_localData.ExposeByCategory(false, LightProbeVolumeCategory);
    }
  }

  void LightProbe::SyncFromComponent()
  {
    LightProbeVolumeComponentPtr probe = GetProbeVolume();
    if (probe == nullptr)
    {
      return;
    }

    // The entity setters would otherwise write straight back into the component and bounce between
    // the two on every value that changed.
    m_syncing = true;

    SetSizeVal(probe->GetSizeVal());
    SetPositionOffsetVal(probe->GetPositionOffsetVal());
    SetBakeFileVal(probe->GetBakeFileVal());
    SetProbeSpacingVal(probe->GetProbeSpacingVal());
    SetProbeTraceRadiusVal(probe->GetProbeTraceRadiusVal());
    SetBakeSamplesVal(probe->GetBakeSamplesVal());
    SetBakeBouncesVal(probe->GetBakeBouncesVal());
    SetBakeSeedVal(probe->GetBakeSeedVal());
    SetBakeDenoiseVal(probe->GetBakeDenoiseVal());
    SetIntensityVal(probe->GetIntensityVal());
    SetAmbientOcclusionStrengthVal(probe->GetAmbientOcclusionStrengthVal());

    m_syncing = false;
  }

  void LightProbe::SyncToComponent()
  {
    if (m_syncing)
    {
      return;
    }

    LightProbeVolumeComponentPtr probe = GetProbeVolume();
    if (probe == nullptr)
    {
      return;
    }

    probe->SetSizeVal(GetSizeVal());
    probe->SetPositionOffsetVal(GetPositionOffsetVal());
    probe->SetBakeFileVal(GetBakeFileVal());
    probe->SetProbeSpacingVal(GetProbeSpacingVal());
    probe->SetProbeTraceRadiusVal(GetProbeTraceRadiusVal());
    probe->SetBakeSamplesVal(GetBakeSamplesVal());
    probe->SetBakeBouncesVal(GetBakeBouncesVal());
    probe->SetBakeSeedVal(GetBakeSeedVal());
    probe->SetBakeDenoiseVal(GetBakeDenoiseVal());
    probe->SetIntensityVal(GetIntensityVal());
    probe->SetAmbientOcclusionStrengthVal(GetAmbientOcclusionStrengthVal());
  }

  void LightProbe::ParameterConstructor()
  {
    Super::ParameterConstructor();

    const String& category = LightProbeCategory.Name;
    const int priority     = LightProbeCategory.Priority;

    Size_Define(DefaultProbeVolumeSize, category, priority, true, true,
                {false, true, 0.01f, 100000.0f, 0.5f});

    PositionOffset_Define(Vec3(0.0f, DefaultProbeVolumeSize.y * 0.5f, 0.0f), category, priority, true, true,
                          {false, true, -100000.0f, 100000.0f, 0.1f});

    BakeFile_Define(String(""), category, priority, true, true);

    ProbeSpacing_Define(DefaultProbeSpacing, category, priority, true, true,
                        {false, true, 0.05f, 1000.0f, 0.5f});

    ProbeTraceRadius_Define(2.0f, category, priority, true, true,
                             {false, true, 0.0f, 16.0f, 0.5f});

    BakeSamples_Define(DefaultBakeSamples, category, priority, true, true,
                       {false, true, 1.0f, 16384.0f, 1.0f});

    BakeBounces_Define(DefaultBakeBounces, category, priority, true, true,
                       {false, true, 1.0f, 16.0f, 1.0f});

    BakeSeed_Define(0, category, priority, true, true, {false, true, 0.0f, 1000000.0f, 1.0f});

    BakeDenoise_Define(2, category, priority, true, true, {false, true, 0.0f, 8.0f, 1.0f});

    Intensity_Define(1.0f, category, priority, true, true,
                     {false, true, 0.0f, 100000.0f, 0.1f});

    AmbientOcclusionStrength_Define(1.0f, category, priority, true, true,
                                    {false, true, 0.0f, 1.0f, 0.05f});

    LightProbeWeakPtr weakSelf = Self<LightProbe>();

    FitToScene_Define(
        [weakSelf]() -> void
        {
          if (LightProbePtr self = weakSelf.lock())
          {
            if (LightProbeVolumeComponentPtr probe = self->GetProbeVolume())
            {
              probe->FitToScene();
            }
            self->SyncFromComponent();
          }
        },
        category,
        priority,
        true,
        true);

    Bake_Define(
        [weakSelf]() -> void
        {
          if (LightProbePtr self = weakSelf.lock())
          {
            if (LightProbeVolumeComponentPtr probe = self->GetProbeVolume())
            {
              probe->RequestBake();
            }
          }
        },
        category,
        priority,
        true,
        true);
  }

  void LightProbe::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    // Every mirrored value has to reach the component: that is the copy the renderer and the bake
    // read, and the entity's own values are only the editable mirror the inspector shows.
    const auto push = [this](Value&, Value&) -> void { SyncToComponent(); };

    ParamSize().m_onValueChangedFn.push_back(push);
    ParamPositionOffset().m_onValueChangedFn.push_back(push);
    ParamBakeFile().m_onValueChangedFn.push_back(push);
    ParamProbeSpacing().m_onValueChangedFn.push_back(push);
    ParamProbeTraceRadius().m_onValueChangedFn.push_back(push);
    ParamBakeSamples().m_onValueChangedFn.push_back(push);
    ParamBakeBounces().m_onValueChangedFn.push_back(push);
    ParamBakeSeed().m_onValueChangedFn.push_back(push);
    ParamBakeDenoise().m_onValueChangedFn.push_back(push);
    ParamIntensity().m_onValueChangedFn.push_back(push);
    ParamAmbientOcclusionStrength().m_onValueChangedFn.push_back(push);
  }

  XmlNode* LightProbe::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    XmlNode* node = Super::DeSerializeImp(info, parent);

    // The serialized component comes back after NativeConstruct ran, so the group is hidden again
    // here and the entity's mirror is refreshed from what the scene actually restored.
    HideVolumeParameters();
    SyncFromComponent();

    return node;
  }

} // namespace ToolKit
