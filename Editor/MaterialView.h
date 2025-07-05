/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EditorTypes.h"
#include "View.h"

namespace ToolKit
{
  namespace Editor
  {

    // MaterialView
    //////////////////////////////////////////

    class TK_EDITOR_API MaterialView : public View
    {
     public:
      MaterialView();
      virtual ~MaterialView();

      void Show() override;
      void SetMaterials(const MaterialPtrArray& mat);
      void ResetCamera();
      void SetSelectedMaterial(MaterialPtr mat);

     private:
      void UpdatePreviewScene();
      void ShowMaterial(MaterialPtr m_mat);

     private:
      PreviewViewportPtr m_viewport = nullptr;
      MaterialPtrArray m_materials;
      uint m_activeObjectIndx    = 0;
      int m_currentMaterialIndex = 0;
      ScenePtr m_scenes[3];

     public:
      bool m_isTempView = false;
    };

    // MaterialWindow
    //////////////////////////////////////////

    class TK_EDITOR_API MaterialWindow : public Window
    {
     public:
      TKDeclareClass(MaterialWindow, Window);

      MaterialWindow();
      virtual ~MaterialWindow();

      void SetMaterial(MaterialPtr mat);
      void Show() override;

     private:
      MaterialViewPtr m_view;
    };

  } // namespace Editor
} // namespace ToolKit