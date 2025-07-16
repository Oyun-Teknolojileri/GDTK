/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "PublishManager.h"

#include "App.h"

#include <FileManager.h>
#include <PluginManager.h>

namespace ToolKit
{
  namespace Editor
  {

    void PublishManager::Publish(PublishPlatform platform, PublishConfig publishConfig)
    {
      if (m_isBuilding)
      {
        TK_WRN("Toolkit is already building a project.");
        return;
      }

      String publishArguments = ConstructPublishArgs(platform, publishConfig, false);

      GetFileManager()->WriteAllText("PublishArguments.txt", publishArguments);
      GetApp()->SetStatusMsg(g_statusPublishing + g_statusNoTerminate);

      String packerPath = NormalizePath("Utils/Packer/Packer.exe");

      // Close zip file before running packer, because packer will use this file as well,
      // this will cause errors otherwise.
      GetFileManager()->CloseZipFile();

      m_isBuilding                       = true;
      packerPath                         = std::filesystem::absolute(ConcatPaths({"..", packerPath})).string();

      SysCommandDoneCallback afterPackFn = [&](int res) -> void
      {
        if (res != 0)
        {
          TK_ERR("Publish Failed.");
          GetApp()->SetStatusMsg(g_statusFailed);
        }
        else
        {
          TK_LOG("Publish Ended.");
          GetApp()->SetStatusMsg(g_statusSucceeded);
        }
        m_isBuilding = false;
      };

      if (platform == PublishPlatform::Web)
      {
        TK_LOG("Publishing to Web...");
      }
      else if (platform == PublishPlatform::Android)
      {
        TK_LOG("Publishing to Android...");
      }
      else if (platform == PublishPlatform::Windows)
      {
        TK_LOG("Publishing to Windows...");
      }
      else
      {
        TK_LOG("Building Plugin...");

        afterPackFn = [=](int res) -> void
        {
          if (res != 0)
          {
            TK_ERR("Plugin Building Failed.");
            GetApp()->SetStatusMsg(g_statusFailed);
            m_isBuilding = false;
            return;
          }
          else
          {
            TK_LOG("Plugin Building Ended.");
            GetApp()->SetStatusMsg(g_statusSucceeded);
          }

          auto afterCompile = [=]() -> void
          {
            String fullPath = GetApp()->m_workspace.GetBinPath();
            if (platform == PublishPlatform::EditorPlugin)
            {
              fullPath = ConcatPaths({m_appName, "Bin", m_pluginName});
            }

            String binFile = fullPath + GetPluginExtention();
            if (PluginManager* plugMan = GetPluginManager())
            {
              if (fullPath.find("Plugins") != String::npos) // Deal with plugins
              {
                if (PluginRegister* reg = plugMan->Load(fullPath))
                {
                  reg->m_plugin->m_currentState = PluginState::Running;
                }
              }
              else // or game.
              {
                GetApp()->LoadGamePlugin();
              }
            }
          };

          // Reload at the end of frame.
          TKAsyncTask(WorkerManager::MainThread, afterCompile);

          m_isBuilding = false;
        };
      }

      GetApp()->ExecSysCommand(packerPath, true, true, afterPackFn);
    }

    void PublishManager::Pack()
    {
      if (m_isBuilding)
      {
        TK_WRN("Toolkit is already building a project.");
        return;
      }

      // Platform and config is not important, this function will just pack the resources.
      String publishArguments = ConstructPublishArgs(PublishPlatform::Windows, PublishConfig::Debug, true);

      GetFileManager()->WriteAllText("PublishArguments.txt", publishArguments);
      GetApp()->SetStatusMsg(g_statusPacking + g_statusNoTerminate);

      String packerPath = NormalizePath("Utils/Packer/Packer.exe");

      // Close zip file before running packer, because packer will use this file as well,
      // this will cause errors otherwise.
      GetFileManager()->CloseZipFile();

      m_isBuilding           = true;
      packerPath             = std::filesystem::absolute(ConcatPaths({"..", packerPath})).string();

      const auto afterPackFn = [&](int res) -> void
      {
        if (res != 0)
        {
          TK_ERR("Packing Failed.");
          GetApp()->SetStatusMsg(g_statusFailed);
        }
        else
        {
          TK_LOG("Packing Ended.");
          GetApp()->SetStatusMsg(g_statusSucceeded);
        }
        m_isBuilding = false;
      };

      GetApp()->ExecSysCommand(packerPath, true, true, afterPackFn);
    }

    String PublishManager::ConstructPublishArgs(PublishPlatform platform, PublishConfig publishConfig, bool packOnly)
    {
      // Project name for publishing.
      String publishArguments  = GetApp()->m_workspace.GetActiveProject().name + '\n';

      // Workspace for publishing resources.
      publishArguments        += GetApp()->m_workspace.GetActiveWorkspace() + '\n';

      // App name for publishing.
      publishArguments += m_appName.empty() ? GetApp()->m_workspace.GetActiveProject().name + '\n' : m_appName + '\n';

      // Try deploying the app after publishing. Try running the app.
      publishArguments += std::to_string((int) m_deployAfterBuild) + '\n';

      // Min sdk for mobile publish.
      publishArguments += std::to_string(m_minSdk) + '\n';

      // Max sdk for mobile publish.
      publishArguments += std::to_string(m_maxSdk) + '\n';

      // Mobile app orientation.
      publishArguments += std::to_string((int) m_oriantation) + '\n';

      // Android ABI.
      publishArguments += std::to_string((int) m_selectedABI) + '\n';

      // Publish platform.
      publishArguments += std::to_string((int) platform) + '\n';

      // Icon for the app.
      publishArguments += m_icon == nullptr ? TexturePath(ConcatPaths({"Icons", "app.png"}), true) : m_icon->GetFile();
      publishArguments += '\n';

      // Debug / Release / Release With Debug Info
      publishArguments += std::to_string((int) publishConfig) + '\n';

      // Only pack the resources.
      publishArguments += std::to_string((int) packOnly) + '\n';

      return publishArguments;
    }

  } // namespace Editor
} // namespace ToolKit
