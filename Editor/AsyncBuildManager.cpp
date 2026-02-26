/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 * Author: erendgrmnc
 */

#include "AsyncBuildManager.h"

#include "App.h"
#include "PublishManager.h"

#include <FileManager.h>
#include <PluginManager.h>

#ifdef TK_MAC
  #include <sys/stat.h>
#endif

namespace ToolKit
{
  namespace Editor
  {
    AsyncBuildManager::AsyncBuildManager(App* app) : m_app(app), m_buildActive(false) {}

    AsyncBuildManager::~AsyncBuildManager() {}

    void AsyncBuildManager::StartBuild(PublishPlatform platform,
                                       PublishConfig config,
                                       const BuildConfig& buildConfig,
                                       bool isCapturingOutput)
    {
      if (m_buildActive)
      {
        TK_WRN("A build is already in progress.");
        return;
      }

      m_buildActive        = true;
      m_isCapturingOutput  = isCapturingOutput;
      m_currentBuildConfig = std::make_unique<BuildConfig>(buildConfig);

      if (platform == PublishPlatform::MacOS)
      {
        const MacOSBuildConfig* macConfig = static_cast<const MacOSBuildConfig*>(&buildConfig);
        m_bundleIdentifier                = macConfig->bundleIdentifier;
        m_minMacOSVersion                 = macConfig->minMacOSVersion;
      }

      if (platform == PublishPlatform::GamePlugin || platform == PublishPlatform::EditorPlugin)
      {
        BuildPlugin(platform, config, buildConfig);
      }
#ifdef TK_WIN
      else if (platform == PublishPlatform::Windows)
      {
        DirectWindowsBuild(config);
      }
#endif
#ifdef TK_MAC
      else if (platform == PublishPlatform::MacOS)
      {
        DirectMacOSBuild(config);
      }
#endif
      else
      {
        TK_ERR("Unsupported platform for async build");
        m_buildActive = false;
      }
    }

    void AsyncBuildManager::CancelBuild()
    {
      // TODO: Implement build cancellation
      m_buildActive = false;
    }

    int AsyncBuildManager::ExecCommand(StringView cmd,
                                       bool async,
                                       bool showConsole,
                                       std::function<void(int)> callback,
                                       bool captureOutput)
    {
      return m_app->ExecSysCommand(cmd, async, showConsole, callback, captureOutput);
    }

    void AsyncBuildManager::OnBuildComplete(PublishPlatform platform, int exitCode)
    {
      m_buildActive = false;

      if (exitCode != 0)
      {
        TK_ERR("Build failed with exit code: %d", exitCode);
        m_app->SetStatusMsg("Build Failed");
      }
      else
      {
        TK_LOG("Build completed successfully");
        m_app->SetStatusMsg("Build Succeeded");
      }
    }

    void AsyncBuildManager::BuildPlugin(PublishPlatform platform, PublishConfig config, const BuildConfig& buildConfig)
    {
      TK_LOG("Building Plugin...");
      m_app->SetStatusMsg("Building Plugin...");

      String buildConfigStr = GetBuildConfigString(config);

      String projectDir =
          ConcatPaths({m_app->m_workspace.GetActiveWorkspace(), m_app->m_workspace.GetActiveProject().name});
      if (platform == PublishPlatform::EditorPlugin)
      {
        projectDir = buildConfig.appName;
      }

      // Note: DO NOT pass -DTK_PLATFORM for plugin builds - the CMakeLists.txt detects plugin build by absence of
      // TK_PLATFORM
      String configCmd =
          "cd \"" + projectDir + "\" && cmake -S . -B ./Intermediate/Plugin -DCMAKE_BUILD_TYPE=" + buildConfigStr;
      String buildCmd = "cd \"" + projectDir + "\" && cmake --build ./Intermediate/Plugin --config " + buildConfigStr;

      TK_LOG("Executing CMake configure: %s", configCmd.c_str());

      auto self          = shared_from_this();

      auto afterConfigFn = [this, self, platform, buildCmd](int res) -> void
      {
        if (res != 0)
        {
          TK_ERR("CMake configure failed.");
          OnBuildComplete(platform, res);
          return;
        }

        TK_LOG("CMake configure succeeded. Starting build...");
        TK_LOG("Executing CMake build: %s", buildCmd.c_str());

        auto afterBuildFn = [this, self, platform](int res) -> void
        {
          if (res != 0)
          {
            TK_ERR("Plugin Building Failed.");
            OnBuildComplete(platform, res);
            return;
          }

          TK_LOG("Plugin Building Ended.");

          auto afterCompile = [this, platform]() -> void
          {
            String fullPath = m_app->m_workspace.GetBinPath();
            if (platform == PublishPlatform::EditorPlugin)
            {
              fullPath = ConcatPaths({m_currentBuildConfig->appName, "Bin", m_currentBuildConfig->pluginName});
            }

            String binFile = fullPath + GetPluginExtention();
            if (PluginManager* plugMan = GetPluginManager())
            {
              if (fullPath.find("Plugins") != String::npos)
              {
                if (PluginRegister* reg = plugMan->Load(fullPath))
                {
                  reg->m_plugin->m_currentState = PluginState::Running;
                }
              }
              else
              {
                m_app->LoadGamePlugin();
              }
            }
          };

          TKAsyncTask(WorkerManager::MainThread, afterCompile);
          OnBuildComplete(platform, 0);
        };

        ExecCommand(buildCmd, true, true, afterBuildFn);
      };

      ExecCommand(configCmd, true, true, afterConfigFn);
    }

#ifdef TK_MAC
    void AsyncBuildManager::DirectMacOSBuild(PublishConfig publishConfig)
    {
      TK_LOG("Building macOS App...");
      m_buildActive = true;
      GetApp()->SetStatusMsg(g_statusPublishing + g_statusNoTerminate);

      String projectDir =
          ConcatPaths({GetApp()->m_workspace.GetActiveWorkspace(), GetApp()->m_workspace.GetActiveProject().name});
      String pakPath    = ConcatPaths({projectDir, "MinResources.pak"});

      bool needPacking  = (publishConfig == PublishConfig::Deploy);
      needPacking      |= !std::filesystem::exists(pakPath);

      if (needPacking)
      {
        TK_LOG("Packing resources...");
        int packResult = GetFileManager()->PackResources();
        if (packResult != 0)
        {
          TK_ERR("Resource packing failed.");
          GetApp()->SetStatusMsg(g_statusFailed);
          m_buildActive = false;
          return;
        }
        TK_LOG("Resources packed successfully.");
      }

      String buildConfig = GetBuildConfigString(publishConfig);

      String configCmd =
          "cd \"" + projectDir +
          "\" && cmake -S . -B ./Intermediate/MacOS -DTK_PLATFORM=MacOS -DCMAKE_BUILD_TYPE=" + buildConfig;
      String buildCmd = "cd \"" + projectDir + "\" && cmake --build ./Intermediate/MacOS --config " + buildConfig;

      auto afterConfigFn =
          std::bind(&AsyncBuildManager::OnMacOSConfigureComplete, this, std::placeholders::_1, publishConfig, buildCmd);
      GetApp()->ExecSysCommand(configCmd, true, true, afterConfigFn);
    }

    void AsyncBuildManager::OnMacOSConfigureComplete(int exitCode, PublishConfig publishConfig, const String& buildCmd)
    {
      if (exitCode != 0)
      {
        TK_ERR("CMake configure failed.");
        GetApp()->SetStatusMsg(g_statusFailed);
        m_buildActive = false;
        return;
      }

      TK_LOG("CMake configure succeeded. Starting build...");

      auto afterBuildFn =
          std::bind(&AsyncBuildManager::OnMacOSBuildComplete, this, std::placeholders::_1, publishConfig);
      GetApp()->ExecSysCommand(buildCmd, true, true, afterBuildFn);
    }

    void AsyncBuildManager::OnMacOSBuildComplete(int exitCode, PublishConfig publishConfig)
    {
      if (exitCode != 0)
      {
        TK_ERR("macOS App Building Failed.");
        GetApp()->SetStatusMsg(g_statusFailed);
        m_buildActive = false;
        return;
      }

      TK_LOG("macOS App Building Ended.");

      MacOSBuildConfig config = CreateMacOSBuildConfig(publishConfig);

      CreateMacOSPublishDirectories(config);
      if (!std::filesystem::exists(config.cmakeBundlePath))
      {
        TK_ERR(("CMake did not create .app bundle at: " + config.cmakeBundlePath).c_str());
        GetApp()->SetStatusMsg(g_statusFailed);
        m_buildActive = false;
        return;
      }

      if (std::filesystem::exists(config.targetBundlePath))
      {
        std::filesystem::remove_all(config.targetBundlePath);
      }

      std::filesystem::copy(config.cmakeBundlePath, config.targetBundlePath, std::filesystem::copy_options::recursive);

      std::filesystem::create_directories(config.resourcesPath);
      std::filesystem::create_directories(config.frameworksPath);

      if (std::filesystem::exists(config.executablePath))
      {
        chmod(config.executablePath.c_str(), 0755);
        TK_LOG("macOS executable is ready");
      }
      else
      {
        TK_ERR(("Executable not found in bundle: " + config.executablePath).c_str());
      }

      BundleSDL2Library(config);

      GenerateInfoPlist(config);

      CopyMacOSResources(config);

      CopyMacOSConfig(config);

      std::error_code ec;
      String pakSrc = ConcatPaths({config.projectDir, "MinResources.pak"});
      String pakDst = ConcatPaths({config.publishDirectory, "MinResources.pak"});

      if (std::filesystem::exists(pakSrc, ec))
      {
        bool copySuccess =
            std::filesystem::copy_file(pakSrc, pakDst, std::filesystem::copy_options::overwrite_existing, ec);
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

      if (m_currentBuildConfig->deployAfterBuild)
      {
        String openCmd = "open \"" + config.targetBundlePath + "\"";
        system(openCmd.c_str());
      }

      m_buildActive = false;
    }

    MacOSBuildConfig AsyncBuildManager::CreateMacOSBuildConfig(PublishConfig publishConfig)
    {
      MacOSBuildConfig config;

      config.appName          = m_currentBuildConfig->appName.empty() ? GetApp()->m_workspace.GetActiveProject().name
                                                                      : m_currentBuildConfig->appName;
      config.pluginName       = m_currentBuildConfig->pluginName;
      config.icon             = m_currentBuildConfig->icon;
      config.deployAfterBuild = m_currentBuildConfig->deployAfterBuild;

      config.bundleIdentifier = m_bundleIdentifier;
      config.minMacOSVersion  = m_minMacOSVersion;

      config.projectDir =
          ConcatPaths({GetApp()->m_workspace.GetActiveWorkspace(), GetApp()->m_workspace.GetActiveProject().name});
      config.buildConfig      = GetBuildConfigString(publishConfig);
      config.toolkitPath      = GetToolkitPath();

      config.cmakeBundlePath  = ConcatPaths({config.projectDir, "Codes", "Bin", config.appName + ".app"});

      config.publishDirectory = ConcatPaths({config.projectDir, "Publish", "MacOS"});
      config.publishBinDir    = ConcatPaths({config.publishDirectory, "Bin"});
      config.publishConfigDir = ConcatPaths({config.publishDirectory, "Config"});
      config.targetBundlePath = ConcatPaths({config.publishBinDir, config.appName + ".app"});

      config.contentsPath     = ConcatPaths({config.targetBundlePath, "Contents"});
      config.macOSPath        = ConcatPaths({config.contentsPath, "MacOS"});
      config.resourcesPath    = ConcatPaths({config.contentsPath, "Resources"});
      config.frameworksPath   = ConcatPaths({config.contentsPath, "Frameworks"});
      config.executablePath   = ConcatPaths({config.macOSPath, config.appName});

      return config;
    }

    void AsyncBuildManager::CreateMacOSPublishDirectories(const MacOSBuildConfig& config)
    {
      std::filesystem::create_directories(config.publishBinDir);
      std::filesystem::create_directories(config.publishConfigDir);
    }

    void AsyncBuildManager::BundleSDL2Library(const MacOSBuildConfig& config)
    {
      String sdl2LibSrc =
          ConcatPaths({config.toolkitPath, "Dependency", "Intermediate", "MacOS", "Release", "libSDL2-2.0.0.dylib"});
      String sdl2LibDst = ConcatPaths({config.frameworksPath, "libSDL2-2.0.0.dylib"});

      std::error_code ec;
      if (std::filesystem::exists(sdl2LibSrc, ec))
      {
        bool copySuccess =
            std::filesystem::copy_file(sdl2LibSrc, sdl2LibDst, std::filesystem::copy_options::overwrite_existing, ec);
        if (!copySuccess || ec)
        {
          TK_ERR(("CRITICAL: Failed to copy SDL2 library: " + ec.message()).c_str());
          TK_ERR("Build will fail - SDL2 is required for macOS apps");
          return;
        }

        chmod(sdl2LibDst.c_str(), 0755);

        String installNameCmd = "install_name_tool -change @rpath/libSDL2-2.0.0.dylib "
                                "@executable_path/../Frameworks/libSDL2-2.0.0.dylib \"" +
                                config.executablePath + "\"";
        system(installNameCmd.c_str());

        TK_LOG("SDL2 library bundled and rpath fixed");
      }
      else
      {
        TK_ERR(("CRITICAL: SDL2 library not found at: " + sdl2LibSrc).c_str());
        TK_ERR("Build will fail - SDL2 is required for macOS apps");
      }
    }

    void AsyncBuildManager::GenerateInfoPlist(const MacOSBuildConfig& config)
    {
      String infoPlist  = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
      infoPlist        += "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                          "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
      infoPlist        += "<plist version=\"1.0\">\n<dict>\n";
      infoPlist        += "\t<key>CFBundleExecutable</key>\n\t<string>" + config.appName + "</string>\n";
      infoPlist        += "\t<key>CFBundleIdentifier</key>\n\t<string>" + config.bundleIdentifier + "</string>\n";
      infoPlist        += "\t<key>CFBundleName</key>\n\t<string>" + config.appName + "</string>\n";
      infoPlist        += "\t<key>CFBundlePackageType</key>\n\t<string>APPL</string>\n";
      infoPlist        += "\t<key>CFBundleShortVersionString</key>\n\t<string>1.0</string>\n";
      infoPlist        += "\t<key>CFBundleVersion</key>\n\t<string>1</string>\n";
      infoPlist        += "\t<key>LSMinimumSystemVersion</key>\n\t<string>" + config.minMacOSVersion + "</string>\n";
      infoPlist        += "\t<key>NSHighResolutionCapable</key>\n\t<true/>\n";

      if (config.icon != nullptr)
      {
        infoPlist += "\t<key>CFBundleIconFile</key>\n\t<string>AppIcon</string>\n";
      }

      infoPlist            += "</dict>\n</plist>";

      String infoPlistPath  = ConcatPaths({config.contentsPath, "Info.plist"});
      GetFileManager()->WriteAllText(infoPlistPath, infoPlist);
    }

    void AsyncBuildManager::CopyMacOSResources(const MacOSBuildConfig& config)
    {
      std::error_code ec;

      if (config.icon != nullptr)
      {
        String iconSrc = config.icon->GetFile();
        String iconDst = ConcatPaths({config.resourcesPath, "AppIcon.icns"});

        if (std::filesystem::exists(iconSrc, ec))
        {
          std::filesystem::copy_file(iconSrc, iconDst, std::filesystem::copy_options::overwrite_existing, ec);
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

    void AsyncBuildManager::CopyMacOSConfig(const MacOSBuildConfig& config)
    {
      std::error_code ec;
      String configSrc = ConcatPaths({config.projectDir, "Config", "Windows"});

      if (!std::filesystem::exists(configSrc, ec))
      {
        configSrc = ConcatPaths({config.projectDir, "Config"});
      }

      if (std::filesystem::exists(configSrc, ec))
      {
        if (configSrc.find("Windows") != String::npos)
        {
          String engineSettingsSrc = ConcatPaths({configSrc, "Engine.settings"});
          String engineSettingsDst = ConcatPaths({config.publishConfigDir, "Engine.settings"});

          if (std::filesystem::exists(engineSettingsSrc, ec))
          {
            std::filesystem::copy_file(engineSettingsSrc,
                                       engineSettingsDst,
                                       std::filesystem::copy_options::overwrite_existing,
                                       ec);
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
          for (const auto& entry : std::filesystem::directory_iterator(configSrc, ec))
          {
            if (ec)
            {
              TK_ERR(("Failed to iterate Config directory: " + ec.message()).c_str());
              break;
            }

            String filename = entry.path().filename().string();
            if (filename == "Android" || filename == "Windows" || filename == "Web")
            {
              continue;
            }

            String destPath = ConcatPaths({config.publishConfigDir, filename});
            if (std::filesystem::is_regular_file(entry.path(), ec))
            {
              std::filesystem::copy_file(entry.path(), destPath, std::filesystem::copy_options::overwrite_existing, ec);
              if (ec)
              {
                TK_WRN(("Failed to copy config file '" + filename + "': " + ec.message()).c_str());
                ec.clear();
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
#endif // TK_MAC

#ifdef TK_WIN
    void AsyncBuildManager::DirectWindowsBuild(PublishConfig publishConfig)
    {
      TK_LOG("Building Windows App...");
      m_buildActive = true;
      GetApp()->SetStatusMsg(g_statusPublishing + g_statusNoTerminate);

      String projectDir =
          ConcatPaths({GetApp()->m_workspace.GetActiveWorkspace(), GetApp()->m_workspace.GetActiveProject().name});
      String pakPath    = ConcatPaths({projectDir, "MinResources.pak"});

      bool needPacking  = (publishConfig == PublishConfig::Deploy);
      needPacking      |= !std::filesystem::exists(pakPath);

      if (needPacking)
      {
        TK_LOG("Packing resources...");
        int packResult = GetFileManager()->PackResources();
        if (packResult != 0)
        {
          TK_ERR("Resource packing failed.");
          GetApp()->SetStatusMsg(g_statusFailed);
          m_buildActive = false;
          return;
        }
        TK_LOG("Resources packed successfully.");
      }

      String buildConfig = GetBuildConfigString(publishConfig);

      String configCmd =
          "cd \"" + projectDir +
          "\" && cmake -S . -B ./Intermediate/Windows -DTK_PLATFORM=Windows -DCMAKE_BUILD_TYPE=" + buildConfig;
      String buildCmd = "cd \"" + projectDir + "\" && cmake --build ./Intermediate/Windows --config " + buildConfig;

      TK_LOG("Executing CMake configure: %s", configCmd.c_str());

      auto afterConfigFn = std::bind(&AsyncBuildManager::OnWindowsConfigureComplete,
                                     this,
                                     std::placeholders::_1,
                                     publishConfig,
                                     buildCmd);
      GetApp()->ExecSysCommand(configCmd, true, false, afterConfigFn, m_buildActive);
    }

    void AsyncBuildManager::OnWindowsConfigureComplete(int exitCode,
                                                       PublishConfig publishConfig,
                                                       const String& buildCmd)
    {
      if (exitCode != 0)
      {
        TK_ERR("CMake configure failed.");
        GetApp()->SetStatusMsg(g_statusFailed);
        m_buildActive = false;
        return;
      }

      TK_LOG("CMake configure succeeded. Starting build...");
      TK_LOG("Executing CMake build: %s", buildCmd.c_str());

      auto afterBuildFn =
          std::bind(&AsyncBuildManager::OnWindowsBuildComplete, this, std::placeholders::_1, publishConfig);
      GetApp()->ExecSysCommand(buildCmd, true, false, afterBuildFn, m_isCapturingOutput);
    }

    void AsyncBuildManager::OnWindowsBuildComplete(int exitCode, PublishConfig publishConfig)
    {
      if (exitCode != 0)
      {
        TK_ERR("Windows App Building Failed.");
        GetApp()->SetStatusMsg(g_statusFailed);
        m_buildActive = false;
        return;
      }

      TK_LOG("Windows build succeeded. Creating publish directory...");

      String projectDir =
          ConcatPaths({GetApp()->m_workspace.GetActiveWorkspace(), GetApp()->m_workspace.GetActiveProject().name});
      String projectName      = GetApp()->m_workspace.GetActiveProject().name;
      String buildConfig      = GetBuildConfigString(publishConfig);

      String intermediatePath = ConcatPaths({projectDir, "Intermediate", "Windows", buildConfig});

      String codesBinPath     = ConcatPaths({projectDir, "Codes", "Bin"});
      String publishDir       = ConcatPaths({projectDir, "Publish", "Windows"});
      String publishBinDir    = ConcatPaths({publishDir, "Bin"});
      String publishConfigDir = ConcatPaths({publishDir, "Config"});

      std::error_code ec;
      std::filesystem::create_directories(publishBinDir, ec);
      if (ec)
      {
        TK_ERR(("Failed to create publish bin directory: " + ec.message()).c_str());
        GetApp()->SetStatusMsg(g_statusFailed);
        m_buildActive = false;
        return;
      }

      std::filesystem::create_directories(publishConfigDir, ec);
      if (ec)
      {
        TK_ERR(("Failed to create publish config directory: " + ec.message()).c_str());
        GetApp()->SetStatusMsg(g_statusFailed);
        m_buildActive = false;
        return;
      }

      String exeSrc = ConcatPaths({codesBinPath, projectName + ".exe"});
      String exeDst = ConcatPaths({publishBinDir, projectName + ".exe"});

      if (std::filesystem::exists(exeSrc, ec))
      {
        std::filesystem::copy_file(exeSrc, exeDst, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
          TK_ERR(("Failed to copy executable: " + ec.message()).c_str());
        }
        else
        {
          TK_LOG(("Copied executable: " + projectName + ".exe").c_str());
        }
      }
      else
      {
        TK_ERR(("Executable not found at: " + exeSrc).c_str());
      }

      String toolkitPath = GetToolkitPath();
      String tkBinPath   = ConcatPaths({toolkitPath, "Bin"});
      String sdlName     = (buildConfig == "Debug") ? "SDL2d.dll" : "SDL2.dll";

      String sdlDllSrc   = ConcatPaths({tkBinPath, sdlName});
      String sdlDllDst   = ConcatPaths({publishBinDir, sdlName});
      if (std::filesystem::exists(sdlDllSrc, ec))
      {
        std::filesystem::copy_file(sdlDllSrc, sdlDllDst, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec)
        {
          TK_LOG(("Copied " + sdlName).c_str());
        }
      }
      else
      {
        TK_WRN(("SDL2 DLL not found at: " + sdlDllSrc).c_str());
      }

      String pakSrc = ConcatPaths({projectDir, "MinResources.pak"});
      String pakDst = ConcatPaths({publishDir, "MinResources.pak"});

      if (std::filesystem::exists(pakSrc, ec))
      {
        std::filesystem::copy_file(pakSrc, pakDst, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec)
        {
          TK_LOG("Copied MinResources.pak");
        }
      }
      else
      {
        TK_WRN("MinResources.pak not found - game may not run correctly");
      }

      String engineSettingsSrc = ConcatPaths({projectDir, "Config", "Windows", "Engine.settings"});
      String engineSettingsDst = ConcatPaths({publishConfigDir, "Engine.settings"});

      if (std::filesystem::exists(engineSettingsSrc, ec))
      {
        std::filesystem::copy_file(engineSettingsSrc,
                                   engineSettingsDst,
                                   std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (!ec)
        {
          TK_LOG("Copied Engine.settings");
        }
      }
      else
      {
        String warningMsg = "Engine.settings not found at: " + engineSettingsSrc;
        TK_WRN(warningMsg.c_str());
      }

      TK_LOG(("Windows App published to: " + publishDir).c_str());
      TK_LOG("Windows App Building Ended.");
      GetApp()->SetStatusMsg(g_statusSucceeded);
      m_buildActive = false;
    }
#endif // TK_WIN

    String AsyncBuildManager::GetBuildConfigString(PublishConfig publishConfig)
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

    String AsyncBuildManager::GetToolkitPath()
    {
      String defaultResourceRoot = Main::GetInstance()->m_defaultResourceRoot;
      String toolkitPath         = ConcatPaths({defaultResourceRoot, "..", ".."});

      std::error_code ec;
      std::filesystem::path canonical = std::filesystem::canonical(toolkitPath, ec);
      if (!ec)
      {
        toolkitPath = canonical.string();
      }

      return toolkitPath;
    }

  } // namespace Editor
} // namespace ToolKit
