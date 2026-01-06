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
      Launcher(int windowWidth, int windowHeight, App* app);
      virtual ~Launcher();

      void ShowLauncherWindow();

      inline Workspace* GetWorkspace() const { return m_workspace; }

     private:
      Launcher() {} // hide default constructor

      void HandleWorkspace();
      void ShowWorkspacePopup();
      void ShowNewProjectPopup();

     private:
      App* m_app              = nullptr;

      int m_windowWidth       = 640;
      int m_windowHeight      = 480;

      Workspace* m_workspace  = nullptr;
      String m_workspacePathOnUI;
      bool m_showWorkspacePopup = false;
      bool m_showNewProjectPopup = false;

      String m_newProjectName;
      int m_selectedProjectIndex = -1;
      TexturePtr m_logoTexture = nullptr;
      TexturePtr m_defaultProjectThumbnail = nullptr;
      String m_logoPath = "/Icons/app.png";
      String m_thumbnailPath = "../../thumbnail.png";
    };
  } // namespace Editor
} // namespace ToolKit
