/*
 /*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Entity.h"
#include "EnvironmentComponent.h"

namespace ToolKit
{

  static VariantCategory ReflectionProbeCategory {"Reflection Probe", 90};

  class TK_API ReflectionProbe : public Entity
  {
   public:
    TKDeclareClass(ReflectionProbe, Entity);

    ReflectionProbe();
    virtual ~ReflectionProbe();

    void NativeConstruct() override;

    void Init();
    void CaptureEnvironment();

    EnvironmentComponentPtr GetEnvironmentComponent() const;

    /** Returns a unit bounding box. */
    const BoundingBox& GetBoundingBox(bool inWorld = false) override;

   protected:
    void ComponentConstructor() override;
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

   public:
    TKDeclareParam(bool, ParallaxCorrection);
    TKDeclareParam(bool, Interior);
    TKDeclareParam(float, Fade);
    TKDeclareParam(float, CaptureFar);
    TKDeclareParam(MultiChoiceVariant, CaptureResolution);

   private:
    bool m_initialized = false;
  };

  typedef std::shared_ptr<ReflectionProbe> ReflectionProbePtr;
  typedef std::weak_ptr<ReflectionProbe> ReflectionProbeWeakPtr;

} // namespace ToolKit
