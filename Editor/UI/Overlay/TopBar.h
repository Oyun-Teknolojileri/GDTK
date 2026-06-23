/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "OverlayUI.h"

namespace ToolKit
{
  namespace Editor
  {
    class TK_EDITOR_API OverlayTopBar : public OverlayUI
    {
     public:
      OverlayTopBar(EditorViewport* owner);
      void Show() override;
      static void ShowAddMenuPopup();

     protected:
      void ShowAddMenu(std::function<void()> showMenuFn, uint& nextItemIndex);
      void ShowTransformOrientation(uint& nextColumnItem);
      void SnapOptions(uint& nextItemIndex);
      void CameraAlignmentOptions(uint& nextItemIndex);
    };

  } // namespace Editor
} // namespace ToolKit