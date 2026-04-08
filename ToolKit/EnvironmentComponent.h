/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Component.h"
#include "GeometryTypes.h"

namespace ToolKit
{

  typedef std::shared_ptr<class EnvironmentComponent> EnvironmentComponentPtr;
  typedef std::vector<EnvironmentComponentPtr> EnvironmentComponentPtrArray;

  static VariantCategory EnvironmentComponentCategory {"Environment Component", 90};

  class TK_API EnvironmentComponent : public Component
  {
   public:
    TKDeclareClass(EnvironmentComponent, Component);

    EnvironmentComponent();
    virtual ~EnvironmentComponent();

    ComponentPtr Copy(EntityPtr ntt) override;
    const BoundingBox& GetBoundingBox();

    void Init(bool flushClientSideArray);
    void UnInit();
    virtual void CaptureEnvironment();

   protected:
    XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;
    XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;

   protected:
    void ParameterConstructor() override;
    void ParameterEventConstructor() override;

   private:
    void UpdateBoundingBoxCache();

   public:
    TKDeclareParam(HdriPtr, Hdri);
    TKDeclareParam(Vec3, Size);
    TKDeclareParam(Vec3, PositionOffset);
    TKDeclareParam(bool, Illuminate);
    TKDeclareParam(float, Intensity);
    TKDeclareParam(float, Fade);
    TKDeclareParam(float, CaptureFar);
    TKDeclareParam(int, CaptureResolution);

    bool m_spatialCachesInvalidated = true; //!< If true, bounding box caches are updated upon access.

   private:
    BoundingBox m_boundingBoxCache;
    bool m_initialized = false;
  };

} // namespace ToolKit