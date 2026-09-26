/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
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

      /**
       * Sets the materials to show.
       * @param mat Materials to inspect, usually the list of an entity's material component.
       * @param owner Entity the materials belong to, when the view was opened from an entity. Its
       * material component and the index of each material in the list are what a key diamond
       * addresses, so without an owner (an asset browser window) there are no diamonds.
       */
      void SetMaterials(const MaterialPtrArray& mat, EntityPtr owner = nullptr);
      void ResetCamera();
      void SetSelectedMaterial(MaterialPtr mat);

     private:
      void UpdatePreviewScene();
      void ShowMaterial(MaterialPtr m_mat);

     private:
      PreviewViewportPtr m_viewport = nullptr;
      MaterialPtrArray m_materials;
      EntityWeakPtr m_owner;
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