/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Workspace.h"

namespace ToolKit
{
  namespace Editor
  {
    class App;

    class Launcher
    {
     public:
      Launcher(Workspace* workspace, App* app);
      virtual ~Launcher();

      void ShowLauncherWindow();

     private:
      Launcher() {} // hide default constructor

      void HandleWorkspace();
      void ShowWorkspacePopup();
      void ShowNewProjectPopup();

     public:
      CreateProjectShortcutOnDesktopFn m_createProjectShortcutOnDesktopFn;

     private:
      App* m_app              = nullptr;

      float m_windowWidth     = 1000.0f;
      float m_windowHeight    = 700.0f;

      Workspace* m_workspace  = nullptr;
      String m_workspacePathOnUI;
      bool m_showWorkspacePopup = false;
      bool m_showNewProjectPopup = false;

      String m_newProjectName;
      String m_newProjectPathOrUrl; // Can be local path or git URL
      bool m_isCloning = false;
      String m_cloneProgress;
      bool m_newProjectTabLocal = true; // true = Local, false = Remote
      int m_selectedProjectIndex = -1;
      TexturePtr m_logoTexture = nullptr;
      TexturePtr m_defaultProjectThumbnail = nullptr;
      String m_logoPath = "/splash.png";
      String m_thumbnailPath = "../../thumbnail.png";
      String m_launchIconPath = "/Icons/play.png";
      String m_folderIconPath = "/Icons/folder.png";
      String m_shortcutIconPath = "/Icons/file.png";
      TexturePtr m_launchIconTexture = nullptr;
      TexturePtr m_folderIconTexture = nullptr;
      TexturePtr m_shortcutIconTexture = nullptr;
    };
  } // namespace Editor
} // namespace ToolKit
