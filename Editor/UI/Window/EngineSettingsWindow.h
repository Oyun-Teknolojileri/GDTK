/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Window.h"

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API EngineSettingsWindow : public Window
    {
     public:
      TKDeclareClass(EngineSettingsWindow, Window);

      EngineSettingsWindow();
      virtual ~EngineSettingsWindow();
      void Show() override;

     protected:
      void ShowPostProcessingTab();
      void ShowGraphicsTab();
      void ShowShadowsTab();

     private:
      bool m_showLoadWindow = false; // Footer load dialog state
    };

  } // namespace Editor
} // namespace ToolKit