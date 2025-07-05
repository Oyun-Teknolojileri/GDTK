/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Camera.h"
#include "EditorTypes.h"
#include "ParameterBlock.h"

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API EditorCamera : public Camera
    {
     public:
      TKDeclareClass(EditorCamera, Camera);

      EditorCamera();
      explicit EditorCamera(const EditorCamera* cam);
      virtual ~EditorCamera();

      void NativeConstruct() override;
      ObjectPtr Copy() const override;
      void GenerateFrustum();

     protected:
      void PostDeSerializeImp(const SerializationFileInfo& info, XmlNode* parent) override;

     private:
      void CreateGizmo();
      void ParameterConstructor() override;

     public:
      TKDeclareParam(VariantCallback, Poses);

     private:
      bool m_posessed = false;
    };

  } // namespace Editor
} // namespace ToolKit
