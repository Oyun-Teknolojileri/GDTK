/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "PublishManager.h"
#include "Window.h"

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API AndroidBuildWindow : public Window
    {
     public:
      TKDeclareClass(AndroidBuildWindow, Window);

      AndroidBuildWindow();

      void Show() override;
      void OpenBuildWindow(PublishConfig publishType);

      String m_appName {};
      int m_minSdk                = 27;
      int m_maxSdk                = 34;
      int m_selectedOriantation   = 0; // 0 undefined 1 landscape 2 Portrait
      TexturePtr m_icon           = nullptr;
      TexturePtr m_defaultIcon    = nullptr;
      bool m_deployAfterBuild     = true;
      PublishConfig m_publishType = PublishConfig::Develop;
      AndroidABI m_selectedABI    = AndroidABI::Arm64V8a;
    };

  } // namespace Editor
} // namespace ToolKit