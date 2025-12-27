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

    class TK_EDITOR_API MacOSBuildWindow : public Window
    {
     public:
      TKDeclareClass(MacOSBuildWindow, Window);

      MacOSBuildWindow();

      void Show() override;
      void OpenBuildWindow(PublishConfig publishType);

      String m_appName {};
      String m_bundleIdentifier = "com.otsoftware.game";
      String m_minMacOSVersion  = "11.0";
      TexturePtr m_icon         = nullptr;
      TexturePtr m_defaultIcon  = nullptr;
      bool m_deployAfterBuild   = false;
      PublishConfig m_publishType = PublishConfig::Develop;
    };

  } // namespace Editor
} // namespace ToolKit
