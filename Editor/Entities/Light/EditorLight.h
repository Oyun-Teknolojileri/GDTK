/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Gizmo.h"

#include <AABBOverrideComponent.h>
#include <Light.h>
#include <Primative.h>
#include <ToolKit.h>
#include <Types.h>

namespace ToolKit
{
  namespace Editor
  {

    // Editor Light Utils.
    TK_EDITOR_API void EnableLightGizmo(Light* light, bool enable);

    // ThreePointLightSystem
    //////////////////////////////////////////

    class TK_EDITOR_API ThreePointLightSystem
    {
     public:
      ThreePointLightSystem();
      ~ThreePointLightSystem();

     public:
      LightPtrArray m_lights;
      Node* m_parentNode = nullptr;
    };

    // LightGizmoController
    //////////////////////////////////////////

    class TK_EDITOR_API LightGizmoController
    {
     public:
      explicit LightGizmoController(Light* light);
      virtual ~LightGizmoController();

      bool GizmoActive() const;
      void EnableGizmo(bool enable) const;
      virtual void InitController();

     protected:
      ValueUpdateFn m_gizmoUpdateFn;

     public:
      class LightMeshGenerator* m_gizmoGenerator = nullptr;

     protected:
      Light* m_light             = nullptr;
      bool m_initialized         = false;
      mutable bool m_gizmoActive = false;
    };

    // EditorDirectionalLight
    //////////////////////////////////////////

    class TK_EDITOR_API EditorDirectionalLight : public DirectionalLight, public LightGizmoController
    {
     public:
      TKDeclareClass(EditorDirectionalLight, DirectionalLight);

      EditorDirectionalLight();
      virtual ~EditorDirectionalLight();
      ObjectPtr Copy() const override;
      LineBatchPtr GetDebugShadowFrustum();

     protected:
      XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
      XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;
    };

    // EditorPointLight
    //////////////////////////////////////////

    class TK_EDITOR_API EditorPointLight : public PointLight, public LightGizmoController
    {
     public:
      TKDeclareClass(EditorPointLight, PointLight);

      EditorPointLight();
      virtual ~EditorPointLight();
      ObjectPtr Copy() const override;

     protected:
      XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
      XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;
      void ParameterEventConstructor() override;
    };

    // EditorSpotLight
    //////////////////////////////////////////

    class TK_EDITOR_API EditorSpotLight : public SpotLight, public LightGizmoController
    {
     public:
      TKDeclareClass(EditorSpotLight, SpotLight);

      EditorSpotLight();
      virtual ~EditorSpotLight();
      ObjectPtr Copy() const override;

     protected:
      XmlNode* SerializeImp(XmlDocument* doc, XmlNode* parent) const override;
      XmlNode* DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;
      void ParameterEventConstructor() override;
    };

  } // namespace Editor
} // namespace ToolKit
