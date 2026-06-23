/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#pragma once

#include "Window.h"

#include <Plugin.h>

namespace ToolKit
{
  namespace Editor
  {

    // PluginSettingsWindow
    //////////////////////////////////////////

    class TK_EDITOR_API PluginSettingsWindow : public Window
    {
     public:
      TKDeclareClass(PluginSettingsWindow, Window);

      PluginSettingsWindow();
      void Show() override;
      void SetPluginSettings(PluginSettings* settings);

     private:
      PluginSettings* m_settings;
      PluginSettings m_bckup;
    };

    // PluginWindow
    //////////////////////////////////////////

    /**
     * Window that shows all the plugins in the project and their status.
     * Also allows load, unload and reload the plugins.
     */
    class TK_EDITOR_API PluginWindow : public Window
    {
     public:
      TKDeclareClass(PluginWindow, Window);

      PluginWindow();

      void Show() override;
      void LoadPluginSettings();

      /** Loads enabled plugins. Enable status is stored in engine settings. */
      void LoadEnabledPlugins();

      /** Unloads all plugins for the current project. */
      void UnloadProjectPlugins();

     private:
      std::vector<PluginSettings> m_pluginSettings;
    };

  } // namespace Editor
} // namespace ToolKit