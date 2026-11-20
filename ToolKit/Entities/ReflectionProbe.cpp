/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, please visit [otyazilim.com].
 */

#include "ReflectionProbe.h"

#include "Texture.h"

#include <DebugNew.h>

namespace ToolKit
{

  namespace
  {
    /** A box big enough to stand in for a room before the author sizes it. */
    const Vec3 DefaultReflectionProbeSize = Vec3(10.0f, 10.0f, 10.0f);
  } // namespace

  TKDefineClass(ReflectionProbe, Entity);

  ReflectionProbe::ReflectionProbe() {}

  ReflectionProbe::~ReflectionProbe() {}

  EnvironmentComponentPtr ReflectionProbe::GetEnvironmentComponent()
  {
    return GetComponent<EnvironmentComponent>();
  }

  void ReflectionProbe::NativeConstruct()
  {
    Super::NativeConstruct();

    SetNameVal("ReflectionProbe");

    // Through the factory, so an editor build gets the subclass that owns the capture debounce.
    ComponentPtr comp = MakeNewPtrCasted<Component>(EnvironmentComponent::StaticClass()->Name);
    if (comp == nullptr)
    {
      comp = MakeNewPtr<EnvironmentComponent>();
    }

    EnvironmentComponentPtr env = SafeCast<EnvironmentComponent>(comp);
    if (env == nullptr)
    {
      return;
    }

    env->SetSizeVal(DefaultReflectionProbeSize);

    // A fileless Hdri is dynamic, and the component captures the scene into it on init: a probe
    // placed in a level shows the room it stands in without the author baking anything first.
    env->SetHdriVal(MakeNewPtr<Hdri>());

    // A reflection probe is meant to be looked into from inside the room it captures.
    env->SetParallaxCorrectionVal(true);

    AddComponent(env);

    HideEnvironmentParameters();
    SyncFromComponent();
  }

  void ReflectionProbe::HideEnvironmentParameters()
  {
    if (EnvironmentComponentPtr env = GetEnvironmentComponent())
    {
      env->m_localData.ExposeByCategory(false, EnvironmentComponentCategory);
    }
  }

  void ReflectionProbe::SyncFromComponent()
  {
    EnvironmentComponentPtr env = GetEnvironmentComponent();
    if (env == nullptr)
    {
      return;
    }

    // The entity setters would otherwise write straight back into the component and bounce between
    // the two on every value that changed.
    m_syncing = true;

    SetHdriVal(env->GetHdriVal());
    SetSizeVal(env->GetSizeVal());
    SetPositionOffsetVal(env->GetPositionOffsetVal());
    SetIlluminateVal(env->GetIlluminateVal());
    SetParallaxCorrectionVal(env->GetParallaxCorrectionVal());
    SetInteriorVal(env->GetInteriorVal());
    SetIntensityVal(env->GetIntensityVal());
    SetFadeVal(env->GetFadeVal());
    SetCaptureFarVal(env->GetCaptureFarVal());
    SetCaptureResolutionVal(env->GetCaptureResolutionVal());

    m_syncing = false;
  }

  void ReflectionProbe::SyncToComponent()
  {
    if (m_syncing)
    {
      return;
    }

    EnvironmentComponentPtr env = GetEnvironmentComponent();
    if (env == nullptr)
    {
      return;
    }

    env->SetHdriVal(GetHdriVal());
    env->SetSizeVal(GetSizeVal());
    env->SetPositionOffsetVal(GetPositionOffsetVal());
    env->SetIlluminateVal(GetIlluminateVal());
    env->SetParallaxCorrectionVal(GetParallaxCorrectionVal());
    env->SetInteriorVal(GetInteriorVal());
    env->SetIntensityVal(GetIntensityVal());
    env->SetFadeVal(GetFadeVal());
    env->SetCaptureFarVal(GetCaptureFarVal());
    env->SetCaptureResolutionVal(GetCaptureResolutionVal());
  }

  void ReflectionProbe::ParameterConstructor()
  {
    Super::ParameterConstructor();

    const String& category = ReflectionProbeCategory.Name;
    const int priority     = ReflectionProbeCategory.Priority;

    Hdri_Define(nullptr, category, priority, true, true);

    Size_Define(DefaultReflectionProbeSize, category, priority, true, true,
                {false, true, 0.01f, 100000.0f, 0.5f});

    PositionOffset_Define(Vec3(0.0f), category, priority, true, true,
                          {false, true, -FLT_MAX, FLT_MAX, 0.5f});

    Illuminate_Define(true, category, priority, true, true);

    ParallaxCorrection_Define(true, category, priority, true, true);

    Interior_Define(false, category, priority, true, true);

    Intensity_Define(1.0f, category, priority, true, true,
                     {false, true, 0.0f, 100000.0f, 0.1f});

    Fade_Define(1.0f, category, priority, true, true,
                {false, true, 0.0f, 100000.0f, 0.1f});

    CaptureFar_Define(0.0f, category, priority, true, true,
                      {false, true, 0.0f, 100000.0f, 1.0f});

    MultiChoiceVariant captureResMcv;
    captureResMcv.Choices.push_back(CreateMultiChoiceParameter("128", 128));
    captureResMcv.Choices.push_back(CreateMultiChoiceParameter("256", 256));
    captureResMcv.Choices.push_back(CreateMultiChoiceParameter("512", 512));
    captureResMcv.CurrentVal.Index = 1;
    CaptureResolution_Define(captureResMcv, category, priority, true, true);

    ReflectionProbeWeakPtr weakSelf = Self<ReflectionProbe>();

    Capture_Define(
        [weakSelf]() -> void
        {
          if (ReflectionProbePtr self = weakSelf.lock())
          {
            if (EnvironmentComponentPtr env = self->GetEnvironmentComponent())
            {
              env->CaptureEnvironment();
            }
          }
        },
        category,
        priority,
        true,
        true);

    CenterToArea_Define(
        [weakSelf]() -> void
        {
          if (ReflectionProbePtr self = weakSelf.lock())
          {
            EnvironmentComponentPtr env = self->GetEnvironmentComponent();
            if (env == nullptr)
            {
              return;
            }

            const Vec3 offset = env->GetPositionOffsetVal();
            if (offset == Vec3(0.0f))
            {
              return;
            }

            // Move the node onto the volume centre and drop the offset, so the entity and the box
            // share one origin from then on.
            const Mat4 world  = self->m_node->GetTransform(TransformationSpace::TS_WORLD);
            const Vec3 center = Vec3(world * Vec4(offset, 1.0f));

            self->m_node->SetTranslation(center, TransformationSpace::TS_WORLD);
            env->SetPositionOffsetVal(Vec3(0.0f));

            self->SyncFromComponent();
          }
        },
        category,
        priority,
        true,
        true);
  }

  void ReflectionProbe::ParameterEventConstructor()
  {
    Super::ParameterEventConstructor();

    // Every mirrored value has to reach the component: that is the copy the renderer and the
    // capture read, and the entity's own values are only the editable mirror the inspector shows.
    const auto push = [this](Value&, Value&) -> void { SyncToComponent(); };

    ParamHdri().m_onValueChangedFn.push_back(push);
    ParamSize().m_onValueChangedFn.push_back(push);
    ParamPositionOffset().m_onValueChangedFn.push_back(push);
    ParamIlluminate().m_onValueChangedFn.push_back(push);
    ParamParallaxCorrection().m_onValueChangedFn.push_back(push);
    ParamInterior().m_onValueChangedFn.push_back(push);
    ParamIntensity().m_onValueChangedFn.push_back(push);
    ParamFade().m_onValueChangedFn.push_back(push);
    ParamCaptureFar().m_onValueChangedFn.push_back(push);
    ParamCaptureResolution().m_onValueChangedFn.push_back(push);
  }

  XmlNode* ReflectionProbe::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
  {
    XmlNode* node = Super::DeSerializeImp(info, parent);

    // The serialized component comes back after NativeConstruct ran, so the group is hidden again
    // here and the entity's mirror is refreshed from what the scene actually restored.
    HideEnvironmentParameters();
    SyncFromComponent();

    return node;
  }

} // namespace ToolKit
