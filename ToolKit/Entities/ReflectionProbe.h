/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, please visit [otyazilim.com].
 */

#pragma once

/**
 * @file ReflectionProbe.h Header for ReflectionProbe
 */

#include "Entity.h"
#include "EnvironmentComponent.h"

namespace ToolKit
{

  class ReflectionProbe;
  typedef std::shared_ptr<ReflectionProbe> ReflectionProbePtr;
  typedef std::weak_ptr<ReflectionProbe> ReflectionProbeWeakPtr;

  static VariantCategory ReflectionProbeCategory {"Reflection Probe", 93};

  /**
   * An entity that always owns an EnvironmentComponent and places it as a reflection source.
   *
   * Since the diffuse term of a local volume moved to the baked probe volume, a volume is what a
   * level uses to say "reflect this room here": the component supplies the prefiltered cubemap and
   * the parallax corrected box, and nothing in the diffuse path reads it. The component is created
   * through the object factory, the way a Sky owns its environment component, so an editor build
   * gets the capture aware subclass and a game gets the engine type.
   *
   * The parameters are mirrored on the entity and the component's own category is hidden, so the
   * inspector shows one Reflection Probe block; the values live on the component, which is what the
   * renderer and the capture read. A fresh probe owns a dynamic (fileless) Hdri, which is what makes
   * the component capture the scene on init.
   */
  class TK_API ReflectionProbe : public Entity
  {
   public:
    TKDeclareClass(ReflectionProbe, Entity);

    ReflectionProbe();
    ~ReflectionProbe() override;

    void NativeConstruct() override;

    /** Returns the environment component this entity owns, or null before NativeConstruct ran. */
    EnvironmentComponentPtr GetEnvironmentComponent();

    /** Copies the component's values into this entity's parameters. */
    void SyncFromComponent();

    /** Writes this entity's parameters into the component. */
    void SyncToComponent();

   public:
    TKDeclareParam(HdriPtr, Hdri);
    TKDeclareParam(Vec3, Size);
    TKDeclareParam(Vec3, PositionOffset);
    TKDeclareParam(bool, Illuminate);
    TKDeclareParam(bool, ParallaxCorrection);
    TKDeclareParam(bool, Interior);
    TKDeclareParam(float, Intensity);
    TKDeclareParam(float, Fade);
    TKDeclareParam(float, CaptureFar);
    TKDeclareParam(MultiChoiceVariant, CaptureResolution);

    /** Captures the surroundings into the probe's cubemap. */
    TKDeclareParam(VariantCallback, Capture);

   protected:
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;

    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

   private:
    /** Hides the component's own parameter group, so only this entity's group shows. */
    void HideEnvironmentParameters();

    /** Set while the mirror is being refreshed, so the setters do not write back into it. */
    bool m_syncing = false;
  };

} // namespace ToolKit
