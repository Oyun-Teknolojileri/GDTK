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

#ifdef __APPLE__
  #include <sys/stat.h>
#endif

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

#if !defined(_WIN32)
      // On non-Windows platforms, use direct cmake execution for plugin builds
      if (platform == PublishPlatform::GamePlugin || platform == PublishPlatform::EditorPlugin)
      {
        DirectPluginBuild(platform, publishConfig);
        return;
      }
#endif

#if defined(__APPLE__)
      // On macOS, use direct cmake execution for native macOS builds
      if (platform == PublishPlatform::MacOS)
      {
        DirectMacOSBuild(publishConfig);
        return;
      }
#endif

      String publishArguments = ConstructPublishArgs(platform, publishConfig, false);

      GetFileManager()->WriteAllText("PublishArguments.txt", publishArguments);
      GetApp()->SetStatusMsg(g_statusPublishing + g_statusNoTerminate);

      String packerPath = NormalizePath("Utils/Packer/Packer.exe");

      // Close zip file before running packer, because packer will use this file as well,
      // this will cause errors otherwise.
      GetFileManager()->CloseZipFile();

      m_isBuilding                       = true;
      packerPath                         = ToAbsolutePath(ConcatPaths({"..", packerPath}));

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

    void PublishManager::DirectPluginBuild(PublishPlatform platform, PublishConfig publishConfig)
    {
      TK_LOG("Building Plugin...");
      m_isBuilding = true;
      GetApp()->SetStatusMsg(g_statusPublishing + g_statusNoTerminate);

      // Determine build configuration
      String buildConfig = GetBuildConfigString(publishConfig);

      // Get the project directory
      String projectDir = ConcatPaths({GetApp()->m_workspace.GetActiveWorkspace(),
                                       GetApp()->m_workspace.GetActiveProject().name});
      if (platform == PublishPlatform::EditorPlugin)
      {
        projectDir = m_appName;
      }

      // Build cmake commands
      // Note: Do NOT pass -DTK_PLATFORM for plugin builds - the CMakeLists.txt detects plugin build by absence of TK_PLATFORM
      String configCmd = "cd \"" + projectDir + "\" && cmake -S . -B ./Intermediate/Plugin -DCMAKE_BUILD_TYPE=" + buildConfig;
      String buildCmd  = "cd \"" + projectDir + "\" && cmake --build ./Intermediate/Plugin --config " + buildConfig;

      // Execute cmake configure
      SysCommandDoneCallback afterConfigFn = [=](int res) -> void
      {
        if (res != 0)
        {
          TK_ERR("CMake configure failed.");
          GetApp()->SetStatusMsg(g_statusFailed);
          m_isBuilding = false;
          return;
        }

        TK_LOG("CMake configure succeeded. Starting build...");

        // Execute cmake build
        SysCommandDoneCallback afterBuildFn = [=](int res) -> void
        {
          if (res != 0)
          {
            TK_ERR("Plugin Building Failed.");
            GetApp()->SetStatusMsg(g_statusFailed);
            m_isBuilding = false;
            return;
          }

          TK_LOG("Plugin Building Ended.");
          GetApp()->SetStatusMsg(g_statusSucceeded);

          // Reload the plugin
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

        GetApp()->ExecSysCommand(buildCmd, true, true, afterBuildFn);
      };

      GetApp()->ExecSysCommand(configCmd, true, true, afterConfigFn);
    }

    void PublishManager::DirectMacOSBuild(PublishConfig publishConfig)
    {
      TK_LOG("Building macOS App...");
      m_isBuilding = true;
      GetApp()->SetStatusMsg(g_statusPublishing + g_statusNoTerminate);

      // Pack resources if needed (Deploy config or if MinResources.pak doesn't exist)
      String projectDir = ConcatPaths({GetApp()->m_workspace.GetActiveWorkspace(),
                                       GetApp()->m_workspace.GetActiveProject().name});
      String pakPath = ConcatPaths({projectDir, "MinResources.pak"});

      bool needPacking = (publishConfig == PublishConfig::Deploy);
      needPacking |= !std::filesystem::exists(pakPath);

      if (needPacking)
      {
        TK_LOG("Packing resources...");
        int packResult = GetFileManager()->PackResources();
        if (packResult != 0)
        {
          TK_ERR("Resource packing failed.");
          GetApp()->SetStatusMsg(g_statusFailed);
          m_isBuilding = false;
          return;
        }
        TK_LOG("Resources packed successfully.");
      }

      // Determine build configuration
      String buildConfig = GetBuildConfigString(publishConfig);

      // Build cmake commands for native macOS build
      String configCmd = "cd \"" + projectDir + "\" && cmake -S . -B ./Intermediate/MacOS -DTK_PLATFORM=TKMac -DCMAKE_BUILD_TYPE=" + buildConfig;
      String buildCmd  = "cd \"" + projectDir + "\" && cmake --build ./Intermediate/MacOS --config " + buildConfig;

      // Execute cmake configure with callback
      auto afterConfigFn = std::bind(&PublishManager::OnMacOSConfigureComplete, this, std::placeholders::_1, publishConfig, buildCmd);
      GetApp()->ExecSysCommand(configCmd, true, true, afterConfigFn);
    }

    void PublishManager::OnMacOSConfigureComplete(int exitCode, PublishConfig publishConfig, const String& buildCmd)
    {
      if (exitCode != 0)
      {
        TK_ERR("CMake configure failed.");
        GetApp()->SetStatusMsg(g_statusFailed);
        m_isBuilding = false;
        return;
      }

      TK_LOG("CMake configure succeeded. Starting build...");

      // Execute cmake build with callback
      auto afterBuildFn = std::bind(&PublishManager::OnMacOSBuildComplete, this, std::placeholders::_1, publishConfig);
      GetApp()->ExecSysCommand(buildCmd, true, true, afterBuildFn);
    }

    void PublishManager::OnMacOSBuildComplete(int exitCode, PublishConfig publishConfig)
    {
      if (exitCode != 0)
      {
        TK_ERR("macOS App Building Failed.");
        GetApp()->SetStatusMsg(g_statusFailed);
        m_isBuilding = false;
        return;
      }

      TK_LOG("macOS App Building Ended.");

      // Create macOS build configuration
      MacOSBuildConfig config = CreateMacOSBuildConfig(publishConfig);

      // Create publish directories
      CreateMacOSPublishDirectories(config);

      // Check if CMake created the bundle
      if (!std::filesystem::exists(config.cmakeBundlePath))
      {
        TK_ERR(("CMake did not create .app bundle at: " + config.cmakeBundlePath).c_str());
        GetApp()->SetStatusMsg(g_statusFailed);
        m_isBuilding = false;
        return;
      }

      // Remove old target bundle if it exists
      if (std::filesystem::exists(config.targetBundlePath))
      {
        std::filesystem::remove_all(config.targetBundlePath);
      }

      // Copy the CMake-generated bundle to the target location
      std::filesystem::copy(config.cmakeBundlePath, config.targetBundlePath,
                           std::filesystem::copy_options::recursive);

      // Create Frameworks and Resources directories
      std::filesystem::create_directories(config.resourcesPath);
      std::filesystem::create_directories(config.frameworksPath);

      // Verify executable exists and is executable
      if (std::filesystem::exists(config.executablePath))
      {
        chmod(config.executablePath.c_str(), 0755);
        TK_LOG("macOS executable is ready");
      }
      else
      {
        TK_ERR(("Executable not found in bundle: " + config.executablePath).c_str());
      }

      // Bundle SDL2 library
      BundleSDL2Library(config);

      // Generate Info.plist
      GenerateInfoPlist(config);

      // Copy app icon (if present) and config files
      CopyMacOSResources(config);
      CopyMacOSConfig(config);

      // Copy MinResources.pak to publish directory
      std::error_code ec;
      String pakSrc = ConcatPaths({config.projectDir, "MinResources.pak"});
      String pakDst = ConcatPaths({config.publishDirectory, "MinResources.pak"});

      if (std::filesystem::exists(pakSrc, ec))
      {
        bool copySuccess = std::filesystem::copy_file(pakSrc, pakDst,
                                                      std::filesystem::copy_options::overwrite_existing, ec);
        if (!copySuccess || ec)
        {
          TK_ERR(("CRITICAL: Failed to copy MinResources.pak: " + ec.message()).c_str());
          TK_ERR("Build may not run correctly without resource package");
        }
        else
        {
          TK_LOG("MinResources.pak copied to publish directory");
        }
      }
      else
      {
        TK_WRN("MinResources.pak not found - build may not run correctly");
      }

      GetApp()->SetStatusMsg(g_statusSucceeded);
      TK_LOG(("macOS app published to: " + config.publishDirectory).c_str());

      // Deploy if requested
      if (m_deployAfterBuild)
      {
        String openCmd = "open \"" + config.targetBundlePath + "\"";
        system(openCmd.c_str());
      }

      m_isBuilding = false;
    }

    String PublishManager::GetBuildConfigString(PublishConfig publishConfig)
    {
      switch (publishConfig)
      {
        case PublishConfig::Debug:
          return "Debug";
        case PublishConfig::Develop:
          return "RelWithDebInfo";
        case PublishConfig::Deploy:
          return "Release";
        default:
          return "Release";
      }
    }

    String PublishManager::GetToolkitPath()
    {
      // Get the GDTK toolkit root directory from m_defaultResourceRoot
      // m_defaultResourceRoot is: /path/to/GDTK/Resources/Engine
      // So go up 2 levels to get to /path/to/GDTK
      String defaultResourceRoot = Main::GetInstance()->m_defaultResourceRoot;
      String toolkitPath = ConcatPaths({defaultResourceRoot, "..", ".."});

      // Normalize the path
      char resolvedPath[1024];
      if (realpath(toolkitPath.c_str(), resolvedPath) != nullptr)
      {
        toolkitPath = resolvedPath;
      }

      return toolkitPath;
    }

    MacOSBuildConfig PublishManager::CreateMacOSBuildConfig(PublishConfig publishConfig)
    {
      MacOSBuildConfig config;

      // Basic configuration
      config.appName = m_appName.empty() ? GetApp()->m_workspace.GetActiveProject().name : m_appName;
      config.projectDir = ConcatPaths({GetApp()->m_workspace.GetActiveWorkspace(),
                                      GetApp()->m_workspace.GetActiveProject().name});
      config.buildConfig = GetBuildConfigString(publishConfig);
      config.toolkitPath = GetToolkitPath();

      // CMake bundle path
      config.cmakeBundlePath = ConcatPaths({config.projectDir, "Codes", "Bin", config.appName + ".app"});

      // Publish directory structure
      config.publishDirectory = ConcatPaths({config.projectDir, "Publish", "MacOS"});
      config.publishBinDir = ConcatPaths({config.publishDirectory, "Bin"});
      config.publishConfigDir = ConcatPaths({config.publishDirectory, "Config"});
      config.targetBundlePath = ConcatPaths({config.publishBinDir, config.appName + ".app"});

      // Bundle internal paths
      config.contentsPath = ConcatPaths({config.targetBundlePath, "Contents"});
      config.macOSPath = ConcatPaths({config.contentsPath, "MacOS"});
      config.resourcesPath = ConcatPaths({config.contentsPath, "Resources"});
      config.frameworksPath = ConcatPaths({config.contentsPath, "Frameworks"});
      config.executablePath = ConcatPaths({config.macOSPath, config.appName});

      return config;
    }

    void PublishManager::CreateMacOSPublishDirectories(const MacOSBuildConfig& config)
    {
      std::filesystem::create_directories(config.publishBinDir);
      std::filesystem::create_directories(config.publishConfigDir);
    }

    void PublishManager::BundleSDL2Library(const MacOSBuildConfig& config)
    {
      String sdl2LibSrc = ConcatPaths({config.toolkitPath, "Dependency", "Intermediate", "TKMac", "Release", "libSDL2-2.0.0.dylib"});
      String sdl2LibDst = ConcatPaths({config.frameworksPath, "libSDL2-2.0.0.dylib"});

      std::error_code ec;
      if (std::filesystem::exists(sdl2LibSrc, ec))
      {
        // Copy SDL2 library to Frameworks directory
        bool copySuccess = std::filesystem::copy_file(sdl2LibSrc, sdl2LibDst,
                                                      std::filesystem::copy_options::overwrite_existing, ec);
        if (!copySuccess || ec)
        {
          TK_ERR(("CRITICAL: Failed to copy SDL2 library: " + ec.message()).c_str());
          TK_ERR("Build will fail - SDL2 is required for macOS apps");
          return;
        }

        // Make library executable
        chmod(sdl2LibDst.c_str(), 0755);

        // Fix the executable's rpath to look in @executable_path/../Frameworks
        String installNameCmd = "install_name_tool -change @rpath/libSDL2-2.0.0.dylib @executable_path/../Frameworks/libSDL2-2.0.0.dylib \"" + config.executablePath + "\"";
        system(installNameCmd.c_str());

        TK_LOG("SDL2 library bundled and rpath fixed");
      }
      else
      {
        TK_ERR(("CRITICAL: SDL2 library not found at: " + sdl2LibSrc).c_str());
        TK_ERR("Build will fail - SDL2 is required for macOS apps");
      }
    }

    void PublishManager::GenerateInfoPlist(const MacOSBuildConfig& config)
    {
      String infoPlist = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
      infoPlist += "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
      infoPlist += "<plist version=\"1.0\">\n<dict>\n";
      infoPlist += "\t<key>CFBundleExecutable</key>\n\t<string>" + config.appName + "</string>\n";
      infoPlist += "\t<key>CFBundleIdentifier</key>\n\t<string>" + m_bundleIdentifier + "</string>\n";
      infoPlist += "\t<key>CFBundleName</key>\n\t<string>" + config.appName + "</string>\n";
      infoPlist += "\t<key>CFBundlePackageType</key>\n\t<string>APPL</string>\n";
      infoPlist += "\t<key>CFBundleShortVersionString</key>\n\t<string>1.0</string>\n";
      infoPlist += "\t<key>CFBundleVersion</key>\n\t<string>1</string>\n";
      infoPlist += "\t<key>LSMinimumSystemVersion</key>\n\t<string>" + m_minMacOSVersion + "</string>\n";
      infoPlist += "\t<key>NSHighResolutionCapable</key>\n\t<true/>\n";

      // Add icon if present
      if (m_icon != nullptr)
      {
        infoPlist += "\t<key>CFBundleIconFile</key>\n\t<string>AppIcon</string>\n";
      }

      infoPlist += "</dict>\n</plist>";

      String infoPlistPath = ConcatPaths({config.contentsPath, "Info.plist"});
      GetFileManager()->WriteAllText(infoPlistPath, infoPlist);
    }

    void PublishManager::CopyMacOSResources(const MacOSBuildConfig& config)
    {
      std::error_code ec;

      // Copy icon if present (non-critical)
      if (m_icon != nullptr)
      {
        String iconSrc = m_icon->GetFile();
        String iconDst = ConcatPaths({config.resourcesPath, "AppIcon.icns"});

        if (std::filesystem::exists(iconSrc, ec))
        {
          std::filesystem::copy_file(iconSrc, iconDst,
                                    std::filesystem::copy_options::overwrite_existing, ec);
          if (ec)
          {
            TK_WRN(("Failed to copy app icon (non-critical): " + ec.message()).c_str());
          }
          else
          {
            TK_LOG("App icon copied to bundle");
          }
        }
      }
    }

    void PublishManager::CopyMacOSConfig(const MacOSBuildConfig& config)
    {
      std::error_code ec;
      String configSrc = ConcatPaths({config.projectDir, "Config", "Windows"}); // Use Windows config as macOS doesn't have specific one

      if (!std::filesystem::exists(configSrc, ec))
      {
        configSrc = ConcatPaths({config.projectDir, "Config"}); // Fallback to root Config
      }

      if (std::filesystem::exists(configSrc, ec))
      {
        // If using platform-specific config, copy just the Engine.settings file
        if (configSrc.find("Windows") != String::npos)
        {
          String engineSettingsSrc = ConcatPaths({configSrc, "Engine.settings"});
          String engineSettingsDst = ConcatPaths({config.publishConfigDir, "Engine.settings"});

          if (std::filesystem::exists(engineSettingsSrc, ec))
          {
            std::filesystem::copy_file(engineSettingsSrc, engineSettingsDst,
                                      std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
            {
              TK_ERR(("CRITICAL: Failed to copy Engine.settings: " + ec.message()).c_str());
              TK_ERR("Build may not run correctly without engine settings");
              return;
            }
          }
        }
        else
        {
          // Copy entire Config directory contents
          for (const auto& entry : std::filesystem::directory_iterator(configSrc, ec))
          {
            if (ec)
            {
              TK_ERR(("Failed to iterate Config directory: " + ec.message()).c_str());
              break;
            }

            String filename = entry.path().filename().string();
            // Skip platform-specific subdirectories
            if (filename == "Android" || filename == "Windows" || filename == "Web")
            {
              continue;
            }

            String destPath = ConcatPaths({config.publishConfigDir, filename});
            if (std::filesystem::is_regular_file(entry.path(), ec))
            {
              std::filesystem::copy_file(entry.path(), destPath,
                                        std::filesystem::copy_options::overwrite_existing, ec);
              if (ec)
              {
                TK_WRN(("Failed to copy config file '" + filename + "': " + ec.message()).c_str());
                ec.clear(); // Continue with other files
              }
            }
          }
        }

        if (!ec)
        {
          TK_LOG("Config files copied to publish directory");
        }
      }
      else
      {
        TK_WRN("No config directory found - using default settings");
      }
    }

  } // namespace Editor
} // namespace ToolKit
