/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

/** Launcher only works with Opengl backend */
#undef TK_VULKAN

#include "../Editor/EditorTypes.h"
#include "../Editor/IconsFontAwesome.h"
#include "Workspace.h"

namespace ToolKit
{
  namespace Launcher
  {
    typedef std::function<void(const String&, const String&)> CreateProjectShortcutOnDesktopFn;

    class LauncherApp
    {
     public:
      LauncherApp();
      virtual ~LauncherApp();

      void ShowLauncherWindow();

     private:
      void HandleWorkspace();
      void UpdateThumbnailCache();
      void ShowWorkspacePopup();
      void ShowNewProjectPopup();
      void OpenProject(const Project& project);

     public:
      Editor::SysCommandExecutionFn m_sysComExecFn;
      CreateProjectShortcutOnDesktopFn m_createProjectShortcutOnDesktopFn;

     private:
      // Launcher states.
      float m_windowWidth  = 1024.0f;
      float m_windowHeight = 768.0f;

      WorkspacePtr m_workspace;
      bool m_showWorkspacePopup  = false;
      bool m_showNewProjectPopup = false;

      String m_newProjectName;
      String m_newProjectPathOrUrl;
      bool m_isCloning = false;
      String m_cloneProgress;
      bool m_newProjectTabLocal  = true;
      int m_selectedProjectIndex = -1;

      StringBoolMap m_thumbnailCache;
      String m_searchFilter;

      // Icon Paths.
      String m_logoPath;
      String m_defaultThumbnailPath;
      String m_thumbnailPath;
      String m_launchIconPath;
      String m_folderIconPath;
      String m_shortcutIconPath;
      String m_workspacePathOnUI;

      // Icon Textures.
      TexturePtr m_launchIconTexture       = nullptr;
      TexturePtr m_folderIconTexture       = nullptr;
      TexturePtr m_shortcutIconTexture     = nullptr;

      // Textures.
      TexturePtr m_logoTexture             = nullptr;
      TexturePtr m_defaultProjectThumbnail = nullptr;
    };

    void DeserializeThemeSettings();
  } // namespace Launcher
} // namespace ToolKit
