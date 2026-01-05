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
    class Launcher
    {
     public:
      Launcher(int windowWidth, int windowHeight);
      virtual ~Launcher();

      void ShowLauncherWindow();

      inline Workspace* GetWorkspace() const { return m_workspace; }

     private:
      void HandleWorkspace();

     private:
      int m_windowWidth = 640;
      int m_windowHeight = 480;

      Workspace* m_workspace = nullptr;
      String m_workspacePathOnUI;
    };
  } // namespace Editor
} // namespace ToolKit
