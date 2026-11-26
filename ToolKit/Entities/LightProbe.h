/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

/**
 * @file LightProbe.h Header for LightProbe
 */

#include "Entity.h"
#include "LightProbeVolumeComponent.h"

namespace ToolKit
{

  class LightProbe;
  typedef std::shared_ptr<LightProbe> LightProbePtr;
  typedef std::weak_ptr<LightProbe> LightProbeWeakPtr;

  static VariantCategory LightProbeCategory {"Light Probe", 92};

  /**
   * An empty entity that always owns a LightProbeVolumeComponent.
   *
   * The component is created through the object factory rather than as a C++ type, the same way a
   * deserialized scene creates one: an editor build registers a probe component subclass that
   * carries the bake runner, and creating the engine type here would hand the user a volume with
   * no way to bake it until the scene had been saved and loaded again.
   *
   * The parameters are mirrored here, the way a Sky mirrors its environment component: the values
   * live on the component - that is what the renderer and the bake read - and the component's own
   * category is hidden, so the inspector shows one Light Probe group on the entity instead of a
   * second block below it.
   */
  class TK_API LightProbe : public Entity
  {
   public:
    TKDeclareClass(LightProbe, Entity);

    LightProbe();
    ~LightProbe() override;

    void NativeConstruct() override;

    /** Returns the volume this entity owns, or null before NativeConstruct ran. */
    LightProbeVolumeComponentPtr GetProbeVolume();

    /** Copies the component's values into this entity's parameters. */
    void SyncFromComponent();

    /** Writes this entity's parameters into the component. */
    void SyncToComponent();

   public:
    TKDeclareParam(Vec3, Size);
    TKDeclareParam(Vec3, PositionOffset);
    TKDeclareParam(String, BakeFile);
    TKDeclareParam(float, ProbeSpacing);
    TKDeclareParam(float, ProbeTraceRadius);

    /** Texture side the target device allows; a grid over it is refused. See the component. */
    TKDeclareParam(MultiChoiceVariant, ProbeCapacity);
    TKDeclareParam(int, BakeSamples);
    TKDeclareParam(int, BakeBounces);
    TKDeclareParam(int, BakeSeed);
    TKDeclareParam(int, BakeDenoise);
    TKDeclareParam(float, Intensity);
    TKDeclareParam(float, AmbientOcclusionStrength);

    /** Shifts the probe lookup along the normal, in cells. See the component. */
    TKDeclareParam(float, NormalOffset);

    /** Fits the volume to the scene bounds. */
    TKDeclareParam(VariantCallback, FitToScene);

    /** Runs the bake. Does nothing in a host that does not link the baker. */
    TKDeclareParam(VariantCallback, Bake);

   protected:
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;

    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

   private:
    /** Hides the volume component's own parameter group, so only this entity's group shows. */
    void HideVolumeParameters();

    /** Set while the mirror is being refreshed, so the setters do not write back into it. */
    bool m_syncing = false;
  };

} // namespace ToolKit
