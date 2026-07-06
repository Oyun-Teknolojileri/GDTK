/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include <Animation.h>
#include <Common/PlatformHelper.h>
#include <FileManager.h>
#include <Image.h>
#include <Material.h>
#include <RenderSystem.h>
#include <SDL.h>
#include <Texture.h>
#include <ToolKit.h>
#include <Types.h>
#include <Util.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <chrono>
#include <mutex>
#include <thread>

namespace ToolKit
{

  // Same enum that is inside Editor::PublisManager
  enum class PublishConfig
  {
    Debug   = 0, // Debug build
    Develop = 1, // Release build with debug info, profile.
    Deploy  = 2  // Release build with calling packer
  };

  static String activeProjectName;
  static String workspacePath;

  StringArray messages;

  enum class PublishPlatform
  {
    Web,
    Windows,
    Linux,
    Android,
    GamePlugin,
    EditorPlugin
  };

  enum Oriantation
  {
    Undefined,
    Landscape,
    Portrait
  };

  enum class AndroidABI
  {
    All        = 0,
    ArmeabiV7a = 1,
    Arm64V8a   = 2,
    X86        = 3,
    X86_64     = 4
  };

  class Packer
  {
   public:
    int Publish();
    int WindowsPublish();
    int LinuxPublish();
    int WebPublish();
    int AndroidPublish();
    int PluginPublish();

    void SetAndroidOptions();
    void AndroidPrepareIcon();
    void AndroidRunOnPhone(bool isAPKUnsigned);
    int PackResources();

    /**
     * Checks the error code and returns true if there is an error.
     * Also reports the message to console in case of error.
     */
    bool CheckErrorReturn(String message);

    void RestoreWorkingDirectory();

   public:
    /** Just packs the resources. Does not perform publishing. */
    bool m_onlyPack = false;
    String m_icon;
    String m_appName;
    int m_minSdk            = 27;
    int m_maxSdk            = 32;
    bool m_deployAfterBuild = false;
    PublishConfig m_publishConfig;
    PublishPlatform m_platform = PublishPlatform::Android;
    String m_toolkitPath;
    String m_templateGameFolderPath;
    Oriantation m_oriantation;
    AndroidABI m_androidABI = AndroidABI::All; // Default to All

    std::error_code m_errorCode;
    Path m_workingDirectory;
  };

  int Packer::PackResources()
  {
    String projectName = activeProjectName;
    if (projectName.empty())
    {
      TK_ERR("No project is loaded.");
      return -1;
    }

    int packResult = GetFileManager()->PackResources();
    if (packResult != 0)
    {
      return packResult;
    }

    return 0;
  }

  bool Packer::CheckErrorReturn(String message)
  {
    if (m_errorCode)
    {
      TK_ERR("%s %s", m_errorCode.message().c_str(), message.c_str());
      RestoreWorkingDirectory();
      return true;
    }

    return false;
  }

  void Packer::RestoreWorkingDirectory()
  {
    if (!m_workingDirectory.empty())
    {
      std::filesystem::current_path(m_workingDirectory, m_errorCode);
      if (m_errorCode)
      {
        TK_ERR("%s\n", m_errorCode.message().c_str(), "Rolling back the working directory failed.");
        TK_ERR("%s\n", "******** PLEASE RESTART THE EDITOR ********");
      }
    }
  }

  int Packer::Publish()
  {
    String packPath   = ConcatPaths({ResourcePath(), "..", "MinResources.pak"});

    bool needPacking  = m_publishConfig == PublishConfig::Deploy;
    needPacking      |= !CheckSystemFile(packPath);
    needPacking      |= m_onlyPack;

    // Prevent packing for plugins.
    needPacking      &= (m_platform != PublishPlatform::EditorPlugin && m_platform != PublishPlatform::GamePlugin);

    if (needPacking)
    {
      int packResult = PackResources();
      if (packResult != 0 || m_onlyPack)
      {
        return packResult;
      }
    }

    switch (m_platform)
    {
      case PublishPlatform::Web:
        return WebPublish();
        break;
      case PublishPlatform::Windows:
        return WindowsPublish();
        break;
      case PublishPlatform::Linux:
        return LinuxPublish();
        break;
      case PublishPlatform::Android:
        return AndroidPublish();
      case PublishPlatform::EditorPlugin:
      case PublishPlatform::GamePlugin:
        return PluginPublish();
      default:
        TK_ERR("Unknown publish platform: %i\n", (int) m_platform);
        return -1;
    }
  }

  int Packer::WindowsPublish()
  {
    TK_LOG("Building for Windows\n");
    m_workingDirectory            = std::filesystem::current_path();

    Path projectDir               = ConcatPaths({ResourcePath(), ".."});
    projectDir                    = projectDir.lexically_normal();
    String projectDirStr          = projectDir.string();
    projectDirStr                 = projectDirStr.substr(0, projectDirStr.size() - 1);

    // Move files to publish directory
    const String projectName      = activeProjectName;
    const String publishDirectory = ConcatPaths({projectDirStr, "Publish", "Windows"});
    const String publishBinDir    = ConcatPaths({publishDirectory, "Bin"});
    const String publishConfigDir = ConcatPaths({publishDirectory, "Config"});

    // Create directories
    std::filesystem::create_directories(publishDirectory, m_errorCode);
    if (CheckErrorReturn("Creating directory " + publishDirectory))
    {
      return -1;
    }

    std::filesystem::create_directories(publishBinDir, m_errorCode);
    if (CheckErrorReturn("Creating directory " + publishBinDir))
    {
      return -1;
    }

    std::filesystem::create_directories(publishConfigDir, m_errorCode);
    if (CheckErrorReturn("Creating directory " + publishConfigDir))
    {
      return -1;
    }

    // Run cmake for Windows build
    String buildConfig;
    if (m_publishConfig == PublishConfig::Debug)
    {
      buildConfig = "Debug";
    }
    else if (m_publishConfig == PublishConfig::Develop)
    {
      buildConfig = "RelWithDebInfo";
    }
    else
    {
      buildConfig = "Release";
    }

    Path newWorkDir = Path(ConcatPaths({ResourcePath(), ".."})).lexically_normal();
    std::filesystem::current_path(newWorkDir, m_errorCode);
    if (CheckErrorReturn("Setting current directory to " + newWorkDir.string()))
    {
      return -1;
    }

    int winBuildCompileResult = -1;
    String cmd                = "cmake -S . -B ./Intermediate/Windows -A x64 -DTK_PLATFORM=Windows";
    winBuildCompileResult     = std::system(cmd.c_str());
    if (winBuildCompileResult != 0)
    {
      TK_ERR("Cmake configure failed: %s\n", cmd.c_str());
      return -1;
    }

    cmd                   = "cmake --build ./Intermediate/Windows --config " + buildConfig;
    winBuildCompileResult = std::system(cmd.c_str());
    if (winBuildCompileResult != 0)
    {
      TK_ERR("Cmake build failed: %s\n", cmd.c_str());
      return -1;
    }

    std::filesystem::current_path(m_workingDirectory, m_errorCode);
    if (CheckErrorReturn("Setting current directory to " + m_workingDirectory.string()))
    {
      return -1;
    }

    // Create bin directory if not exist already.
    String binDir = ConcatPaths({projectDirStr, "Codes", "Bin"});
    std::filesystem::create_directories(binDir);
    if (CheckErrorReturn("Creating directory " + binDir))
    {
      return -1;
    }

    String sdlName                      = buildConfig == "Debug" ? "SDL2d.dll" : "SDL2.dll";
    const String exeFile                = ConcatPaths({binDir, projectName + ".exe"});
    const String pakFile                = ConcatPaths({projectDirStr, "MinResources.pak"});
    const String sdlDllPath             = ConcatPaths({m_toolkitPath, "Bin", sdlName});
    const String engineSettingsPath     = ConcatPaths({projectDirStr, "Config", "Windows", "Engine.settings"});
    const String destEngineSettingsPath = ConcatPaths({publishConfigDir, "Engine.settings"});

    TK_LOG("Windows build done, moving files\n");

    // Copy exe file
    std::filesystem::copy(exeFile, publishBinDir, std::filesystem::copy_options::overwrite_existing, m_errorCode);
    if (CheckErrorReturn("Copy exe to " + publishBinDir))
    {
      return -1;
    }

    // Copy SDL2.dll from ToolKit bin folder to publish bin folder
    std::filesystem::copy(sdlDllPath, publishBinDir, std::filesystem::copy_options::overwrite_existing, m_errorCode);
    if (CheckErrorReturn("Copy sdl.dll to " + publishBinDir))
    {
      return -1;
    }

    // Copy pak
    std::filesystem::copy(pakFile, publishDirectory, std::filesystem::copy_options::overwrite_existing, m_errorCode);
    if (CheckErrorReturn("Copy MinResource.pak to " + publishDirectory))
    {
      return -1;
    }

    // Copy engine settings to config folder
    std::filesystem::copy(engineSettingsPath,
                          destEngineSettingsPath,
                          std::filesystem::copy_options::overwrite_existing,
                          m_errorCode);

    if (CheckErrorReturn("Copy Engine.settings to " + engineSettingsPath))
    {
      return -1;
    }

    // Tell user about where the location of output files is
    TK_SUCCESS("Building for WINDOWS has been completed successfully.\n");
    TK_LOG("Output files location: %s\n", std::filesystem::absolute(publishDirectory).string().c_str());

    PlatformHelpers::OpenExplorer(publishDirectory);
    return 0;
  }

  int Packer::LinuxPublish()
  {
    TK_LOG("Building for Linux\n");
    m_workingDirectory            = std::filesystem::current_path();

    // Run cmake for Linux build
    String buildConfig;
    if (m_publishConfig == PublishConfig::Debug)
    {
      buildConfig = "Debug";
    }
    else if (m_publishConfig == PublishConfig::Develop)
    {
      buildConfig = "RelWithDebInfo";
    }
    else
    {
      buildConfig = "Release";
    }

    Path newWorkDir = Path(ConcatPaths({ResourcePath(), ".."})).lexically_normal();
    std::filesystem::current_path(newWorkDir, m_errorCode);
    if (CheckErrorReturn("Setting current directory to " + newWorkDir.string()))
    {
      return -1;
    }

    String cmd = "cmake -S . -B ./Intermediate/Linux -DTK_PLATFORM=Linux";
    if (!m_toolkitPath.empty())
    {
      cmd += " -DTOOLKIT_DIR=\"" + m_toolkitPath + "\"";
    }
    int compileRes = std::system(cmd.c_str());
    if (compileRes != 0)
    {
      TK_ERR("Cmake configure failed: %s\n", cmd.c_str());
      return -1;
    }

    cmd        = "cmake --build ./Intermediate/Linux --config " + buildConfig;
    compileRes = std::system(cmd.c_str());
    if (compileRes != 0)
    {
      TK_ERR("Cmake build failed: %s\n", cmd.c_str());
      return -1;
    }

    std::filesystem::current_path(m_workingDirectory, m_errorCode);
    if (CheckErrorReturn("Setting current directory to " + m_workingDirectory.string()))
    {
      return -1;
    }

    Path projectDir                = Path(ConcatPaths({ResourcePath(), ".."})).lexically_normal();
    String projectDirStr           = projectDir.string();
    projectDirStr                  = projectDirStr.substr(0, projectDirStr.size() - 1);

    const String projectName       = activeProjectName;
    const String publishDirectory  = ConcatPaths({projectDirStr, "Publish", "Linux"});
    const String publishBinDir     = ConcatPaths({publishDirectory, "Bin"});
    const String publishConfigDir  = ConcatPaths({publishDirectory, "Config"});

    std::filesystem::create_directories(publishDirectory, m_errorCode);
    if (CheckErrorReturn("Creating directory " + publishDirectory))
    {
      return -1;
    }
    std::filesystem::create_directories(publishBinDir, m_errorCode);
    if (CheckErrorReturn("Creating directory " + publishBinDir))
    {
      return -1;
    }
    std::filesystem::create_directories(publishConfigDir, m_errorCode);
    if (CheckErrorReturn("Creating directory " + publishConfigDir))
    {
      return -1;
    }

    // Copy build artifacts from the project's Codes/Bin and the engine's Bin.
    String binDir                    = ConcatPaths({projectDirStr, "Codes", "Bin"});
    String tkBin                     = ConcatPaths({m_toolkitPath, "Bin"});
    String sdlSuffix                 = buildConfig == "Debug" ? "d" : "";
    const String exeFile             = ConcatPaths({binDir, projectName});
    const String pakFile             = ConcatPaths({projectDirStr, "MinResources.pak"});
    const String engineLib           = ConcatPaths({tkBin, "libToolKit" + sdlSuffix + ".so"});
    const String sdlLib              = ConcatPaths({m_toolkitPath, "Dependency", "Intermediate", "Linux", buildConfig, "libSDL2-2.0" + sdlSuffix + ".so"});
    const String imguiLib            = ConcatPaths({m_toolkitPath, "Dependency", "Intermediate", "Linux", buildConfig, "libimgui" + sdlSuffix + ".so"});
    const String engineSettingsPath  = ConcatPaths({projectDirStr, "Config", "Linux", "Engine.settings"});
    const String destEngineSettings  = ConcatPaths({publishConfigDir, "Engine.settings"});

    TK_LOG("Linux build done, moving files\n");

    // Copy executable
    std::filesystem::copy(exeFile, publishBinDir, std::filesystem::copy_options::overwrite_existing, m_errorCode);
    if (CheckErrorReturn("Copy exe to " + publishBinDir))
    {
      return -1;
    }

    // Copy engine shared library next to the executable.
    std::filesystem::copy(engineLib, publishBinDir, std::filesystem::copy_options::overwrite_existing, m_errorCode);
    if (CheckErrorReturn("Copy libToolKit.so to " + publishBinDir))
    {
      return -1;
    }

    // Copy the vendored shared deps so the binary finds them without
    // LD_LIBRARY_PATH (the exe's RUNPATH points to its own directory + the
    // deps dir; placing the .so files next to the exe covers both).
    std::filesystem::copy(sdlLib, publishBinDir, std::filesystem::copy_options::overwrite_existing, m_errorCode);
    if (CheckErrorReturn("Copy SDL2.so to " + publishBinDir))
    {
      return -1;
    }
    std::filesystem::copy(imguiLib, publishBinDir, std::filesystem::copy_options::overwrite_existing, m_errorCode);
    if (CheckErrorReturn("Copy imgui.so to " + publishBinDir))
    {
      return -1;
    }

    // Copy pak
    std::filesystem::copy(pakFile, publishDirectory, std::filesystem::copy_options::overwrite_existing, m_errorCode);
    if (CheckErrorReturn("Copy MinResources.pak to " + publishDirectory))
    {
      return -1;
    }

    // Copy engine settings
    std::filesystem::copy(engineSettingsPath,
                          destEngineSettings,
                          std::filesystem::copy_options::overwrite_existing,
                          m_errorCode);
    if (CheckErrorReturn("Copy Engine.settings to " + engineSettingsPath))
    {
      return -1;
    }

    TK_SUCCESS("Building for LINUX has been completed successfully.\n");
    TK_LOG("Output files location: %s\n", std::filesystem::absolute(publishDirectory).string().c_str());

    PlatformHelpers::OpenExplorer(publishDirectory);
    return 0;
  }

  void Packer::AndroidPrepareIcon()
  {
    String assetsPath  = NormalizePath("Android/app/src/main/res");
    String projectName = activeProjectName;
    String resLocation = ConcatPaths({workspacePath, projectName, assetsPath});

    TK_LOG("Preparing Icons\n");
    int refWidth, refHeight, refComp;
    unsigned char* refImage = ImageLoad(m_icon.c_str(), &refWidth, &refHeight, &refComp, 0);
    if (refImage == nullptr)
    {
      TK_LOG("Can not load icon image!\n");
      return;
    }

    // search each folder in res folder and find icons, replace that icons with new ones
    for (const auto& entry : std::filesystem::directory_iterator(resLocation))
    {
      if (!entry.is_directory())
      {
        continue;
      }

      for (auto& file : std::filesystem::directory_iterator(entry))
      {
        String name;
        String extension;
        String path = file.path().string();
        DecomposePath(path, nullptr, &name, &extension);
        // if this is image replace with new one, don't touch if this is background image
        if (name.find("background") == std::string::npos && extension == ".png")
        {
          int width, height, comp;
          // get the image that we want to replace
          unsigned char* img = ImageLoad(path.c_str(), &width, &height, &comp, 0);
          assert(img && "cannot load android icon");
          int res;
          res = ImageResize(refImage, refWidth, refHeight, 0, img, width, height, 0, comp);
          assert(res && "cannot resize android icon");
          // write resized image
          res = WritePNG(path.c_str(), width, height, comp, img, 0);
          assert(res && "cannot write to android icon");
          ImageFree(img);
        }
      }
    }
    ImageFree(refImage);
  }

  void Packer::AndroidRunOnPhone(bool isAPKUnsigned)
  {
    // adb path is in: 'android-sdk/platform-tools'
    String sdkPath = String(getenv("ANDROID_HOME"));
    if (sdkPath.empty())
    {
      TK_WRN("ANDROID_HOME environment variable is not set.\n ");
      return;
    }

    m_workingDirectory = std::filesystem::current_path();
    std::filesystem::current_path(ConcatPaths({sdkPath, "platform-tools"}), m_errorCode);

    TK_LOG("Trying to execute the app on your phone...\n");

    const auto checkIfFailedFn = [this](int execResult, const String& command) -> bool
    {
      if (execResult == 1)
      {
        TK_LOG("%s command failed! exec result: %i\n", command.c_str(), execResult);
        TK_WRN("Make sure that an android device is connected to your PC\n");
        TK_WRN("if still doesn't work uninstall application and rebuild.\n");

        RestoreWorkingDirectory();
        return true;
      }

      return false;
    };

    String apkPath        = NormalizePath("Android/app/build/outputs/apk");

    String buildType      = m_publishConfig == PublishConfig::Debug ? "debug" : "release";
    apkPath               = ConcatPaths({apkPath, buildType});

    String releaseApkFile = isAPKUnsigned ? "app-release-unsigned.apk" : "app-release.apk";

    apkPath = ConcatPaths({apkPath, m_publishConfig == PublishConfig::Debug ? "app-debug.apk" : releaseApkFile});

    String projectName = activeProjectName;
    String apkLocation = ConcatPaths({workspacePath, projectName, apkPath});
    String packageName = "com.otyazilim.toolkit." + projectName + "/com.otyazilim.toolkit.ToolKitActivity";

    int execResult;
    execResult = PlatformHelpers::SysComExec({String("adb"), String("install"), apkLocation}, false, true, nullptr);
    if (checkIfFailedFn(execResult, "adb install " + apkLocation))
    {
      return;
    }

    execResult = PlatformHelpers::SysComExec(
        {String("adb"), String("shell"), String("am"), String("start"), String("-n"), packageName},
        true,
        true,
        nullptr);
    if (checkIfFailedFn(execResult, "adb shell am start -n " + packageName))
    {
      return;
    }

    RestoreWorkingDirectory();
  }

  void Packer::SetAndroidOptions()
  {
    TK_LOG("Editing Build Gradle\n");
    String projectName     = activeProjectName;
    String applicationName = m_appName;
    String mainPath        = NormalizePath("Android/app");

    // get file from template
    String gradleFileText  = GetFileManager()->ReadAllText(
        std::filesystem::absolute(ConcatPaths({m_templateGameFolderPath, mainPath, "build.gradle"})).string());

    // replace template values with our settings
    ReplaceFirstStringInPlace(gradleFileText, "minSdkVersion 19", "minSdkVersion " + std::to_string(m_minSdk));
    ReplaceFirstStringInPlace(gradleFileText, "maxSdkVersion 34", "maxSdkVersion " + std::to_string(m_maxSdk));
    ReplaceFirstStringInPlace(gradleFileText, "compileSdkVersion 33", "compileSdkVersion " + std::to_string(m_maxSdk));
    ReplaceFirstStringInPlace(gradleFileText, "__TK_NAMESPACE__", "com.otyazilim.toolkit." + projectName);

    String oriantationNames[3] {"fullSensor", "landscape", "portrait"};
    ReplaceFirstStringInPlace(gradleFileText, "__ACTIVITY_ORIENTATION__", oriantationNames[(int) m_oriantation]);
    ReplaceFirstStringInPlace(gradleFileText, "__GAME_NAME__", m_appName);

    // Add ABI-specific configuration
    String abiNames[4] {"'armeabi-v7a'", "'arm64-v8a'", "'x86'", "'x86_64'"};
    int abiCount = 4;
    String abis;
    if (m_androidABI == AndroidABI::All)
    {
      for (int i = 0; i < abiCount - 1; i++)
      {
        abis += abiNames[i] + ",";
      }
      abis += abiNames[abiCount - 1];
    }
    else
    {
      abis = abiNames[(int) m_androidABI - 1];
    }

    ReplaceFirstStringInPlace(gradleFileText, "__ANDROID_ABI__", abis);

    String mainLocation = ConcatPaths({workspacePath, projectName, mainPath});
    String gradleLoc    = ConcatPaths({mainLocation, "build.gradle"});

    GetFileManager()->WriteAllText(gradleLoc, gradleFileText);
  }

  int Packer::AndroidPublish()
  {
    TK_LOG("Building for android\n");
    Path m_workingDirectory = std::filesystem::current_path();

    String projectName      = activeProjectName;
    if (projectName.empty())
    {
      TK_ERR("No project is loaded.\n");
      return -1;
    }

    const String assetsPath             = NormalizePath("Android/app/src/main/assets");
    const String projectLocation        = ConcatPaths({workspacePath, projectName});
    const String sceneResourcesPath     = ConcatPaths({projectLocation, "MinResources.pak"});
    const String androidResourcesPath   = ConcatPaths({projectLocation, assetsPath, "MinResources.pak"});
    const String engineSettingsPath     = ConcatPaths({ResourcePath(), "..", "Config", "Android", "Engine.settings"});
    const String destEngineSettingsPath = ConcatPaths({projectLocation, assetsPath, "Config", "Engine.settings"});

    const std::filesystem::copy_options copyOption = std::filesystem::copy_options::overwrite_existing;

    std::filesystem::copy(sceneResourcesPath, androidResourcesPath, copyOption, m_errorCode);
    if (CheckErrorReturn("Copy " + sceneResourcesPath + " and " + androidResourcesPath))
    {
      return -1;
    }

    std::filesystem::copy(engineSettingsPath, destEngineSettingsPath, copyOption, m_errorCode);
    if (CheckErrorReturn("Copy " + engineSettingsPath + " and " + destEngineSettingsPath))
    {
      return -1;
    }

    SetAndroidOptions();

    String androidPath = ConcatPaths({projectLocation, "Android"});
    std::filesystem::current_path(androidPath, m_errorCode);
    if (CheckErrorReturn("Setting current path to " + androidPath))
    {
      return -1;
    }

    AndroidPrepareIcon();

    TK_LOG("Building android apk, Gradle scripts running...\n");
    String buildType     = m_publishConfig == PublishConfig::Debug ? "debug" : "release";

    // clean apk output directory
    String buildLocation = ConcatPaths({projectLocation, "Android/app/build/outputs/apk"});

    if (!CheckSystemFile(buildLocation))
    {
      std::filesystem::create_directories(buildLocation, m_errorCode);
      if (CheckErrorReturn("Creating directory " + buildLocation))
      {
        return -1;
      }
    }

    for (auto& folder : std::filesystem::directory_iterator(buildLocation))
    {
      if (!folder.is_directory())
      {
        continue;
      }

      if (folder.path().filename().string() != buildType)
      {
        continue;
      }

      for (auto& file : std::filesystem::directory_iterator(folder))
      {
        if (file.is_directory())
        {
          continue;
        }

        std::filesystem::remove(file);
      }
    }

    // use "gradlew bundle" command to build .aab project or use "gradlew assemble" to release build
    String command    = m_publishConfig == PublishConfig::Debug ? "gradlew assembleDebug" : "gradlew assembleRelease";

    int compileResult = std::system(command.c_str());
    if (compileResult != 0)
    {
      TK_ERR("Android build failed.\n");
      return -1;
    }

    buildLocation      = ConcatPaths({buildLocation, buildType});

    bool apkIsUnsigned = false;
    // See if the apk is unsigned or not
    if (CheckSystemFile(ConcatPaths({buildLocation, "app-release-unsigned.apk"})))
    {
      apkIsUnsigned = true;
    }

    const String publishDirStr   = ConcatPaths({ResourcePath(), "..", "Publish", "Android"});
    const String apkName         = m_publishConfig == PublishConfig::Debug ? "app-debug.apk"
                                   : apkIsUnsigned                         ? "app-release-unsigned.apk"
                                                                           : "app-release.apk";

    const String apkPathStr      = ConcatPaths({buildLocation, apkName});

    projectName                  = m_appName;
    projectName                 += m_publishConfig == PublishConfig::Debug ? "_debug.apk" : "_release.apk";

    const String publishApkPath  = ConcatPaths({publishDirStr, projectName});

    // Create directories
    std::filesystem::create_directories(publishDirStr, m_errorCode);
    if (CheckErrorReturn("Creating directory " + publishDirStr))
    {
      return -1;
    }

    std::filesystem::copy(apkPathStr, publishApkPath, std::filesystem::copy_options::overwrite_existing, m_errorCode);
    if (CheckErrorReturn("Copy " + apkPathStr + " and " + publishApkPath))
    {
      return -1;
    }

    // Tell user about where the location of output files is
    TK_SUCCESS("Building for ANDROID has been completed successfully.\n");
    TK_LOG("Output files location: %s\n", std::filesystem::absolute(publishDirStr).string().c_str());

    PlatformHelpers::OpenExplorer(publishDirStr);

    if (compileResult != 0)
    {
      TK_ERR("Compiling failed.\n");
      return -1;
    }

    std::filesystem::current_path(m_workingDirectory, m_errorCode); // set work directory back
    if (CheckErrorReturn("Setting current directory to " + m_workingDirectory.string()))
    {
      return -1;
    }

    if (m_deployAfterBuild)
    {
      AndroidRunOnPhone(apkIsUnsigned);
    }

    return 0;
  }

  int Packer::PluginPublish()
  {
    TK_LOG("Building for Plugin\n");
    m_workingDirectory = std::filesystem::current_path();

    // Run emscripten for web build.
    String buildConfig;
    if (m_publishConfig == PublishConfig::Debug)
    {
      buildConfig = "Debug";
    }
    else if (m_publishConfig == PublishConfig::Develop)
    {
      buildConfig = "RelWithDebInfo";
    }
    else
    {
      buildConfig = "Release";
    }

    // Switch to cmake directory for the game build.
    Path newWorkDir = Path(ConcatPaths({ResourcePath(), ".."})).lexically_normal();
    if (m_platform == PublishPlatform::EditorPlugin)
    {
      newWorkDir = m_appName;
    }

    std::filesystem::current_path(newWorkDir, m_errorCode);
    if (CheckErrorReturn("Setting current directory to " + newWorkDir.string()))
    {
      return -1;
    }

    // Compile game / editor plugin.
    // -A x64 is a Visual Studio generator flag; skip it on other platforms.
    // Always tell the project where the engine is (Packer already resolved it
    // from Path.txt), so the project CMake does not need TOOLKIT_DIR in its env.
#ifdef _WIN32
    String cmd     = "cmake -S . -B ./Intermediate/Plugin -A x64";
#else
    String cmd     = "cmake -S . -B ./Intermediate/Plugin";
#endif
    if (!m_toolkitPath.empty())
    {
      cmd += " -DTOOLKIT_DIR=\"" + m_toolkitPath + "\"";
    }
    int compileRes = std::system(cmd.c_str());
    if (compileRes != 0)
    {
      TK_ERR("Cmake configure failed. %s\n", cmd.c_str());
      return -1;
    }

    cmd        = "cmake --build ./Intermediate/Plugin --config " + buildConfig;
    compileRes = std::system(cmd.c_str());
    if (compileRes != 0)
    {
      TK_ERR("Cmake build failed. %s\n", cmd.c_str());
      return -1;
    }

    std::filesystem::current_path(m_workingDirectory, m_errorCode);
    if (CheckErrorReturn("Setting current directory to " + m_workingDirectory.string()))
    {
      return -1;
    }

    TK_SUCCESS("Building for plugin has been completed successfully.\n");
    return 0;
  }

  int Packer::WebPublish()
  {
    TK_LOG("Building for Web\n");
    m_workingDirectory = std::filesystem::current_path();

    // Run emscripten for web build.
    String buildConfig;
    if (m_publishConfig == PublishConfig::Debug)
    {
      buildConfig = "Debug";
    }
    else if (m_publishConfig == PublishConfig::Develop)
    {
      buildConfig = "RelWithDebInfo";
    }
    else
    {
      buildConfig = "Release";
    }

    // Switch to cmake directory for the game build.
    Path newWorkDir = Path(ConcatPaths({ResourcePath(), ".."})).lexically_normal();
    std::filesystem::current_path(newWorkDir, m_errorCode);
    if (CheckErrorReturn("Setting current directory to " + newWorkDir.string()))
    {
      return -1;
    }

    // Compile game.
    String cmd     = "emcmake cmake -S . -B ./Intermediate/Web -DTK_CXX_EXTRA:STRING=-pthread -DTK_PLATFORM=Web "
                     "-DCMAKE_BUILD_TYPE=" +
                     buildConfig;

    int compileRes = std::system(cmd.c_str());
    if (compileRes != 0)
    {
      TK_ERR("Cmake configure failed. %s\n", cmd.c_str());
      return -1;
    }

    cmd        = "emmake cmake --build ./Intermediate/Web";
    compileRes = std::system(cmd.c_str());
    if (compileRes != 0)
    {
      TK_ERR("Cmake build failed. %s\n", cmd.c_str());
      return -1;
    }

    std::filesystem::current_path(m_workingDirectory, m_errorCode);
    if (CheckErrorReturn("Setting current directory to " + m_workingDirectory.string()))
    {
      return -1;
    }

    // Move files to a directory
    String projectName      = activeProjectName;
    String publishDirectory = ConcatPaths({ResourcePath(), "..", "Publish", "Web"});
    String firstPart        = ConcatPaths({ResourcePath(), "..", "Codes", "Bin", projectName}) + ".";
    String files[]          = {firstPart + "data",
                               firstPart + "html",
                               firstPart + "js",
                               firstPart + "wasm",
                               firstPart + "worker.js"};

    std::filesystem::create_directories(publishDirectory, m_errorCode);
    if (CheckErrorReturn("Create directories " + publishDirectory))
    {
      return -1;
    }

    for (int i = 0; i < ArraySize(files); i++)
    {
      std::filesystem::copy(files[i].c_str(),
                            publishDirectory,
                            std::filesystem::copy_options::overwrite_existing,
                            m_errorCode);

      if (CheckErrorReturn("Copy " + files[i] + " to " + publishDirectory))
      {
        return -1;
      }
    }

    // Create run script
    std::ofstream runBatchFile(ConcatPaths({publishDirectory, "Run.bat"}).c_str());
    runBatchFile << "emrun ./" + projectName + ".html";
    runBatchFile.close();

    if (CheckErrorReturn("Setting current directory to " + m_workingDirectory.string()))
    {
      return -1;
    }

    // Output user about where are the output files
    TK_SUCCESS("Building for web has been completed successfully.\n");
    TK_LOG("Output files location: %s\n", std::filesystem::absolute(publishDirectory).string().c_str());

    PlatformHelpers::OpenExplorer(publishDirectory);
    return 0;
  }

  int ToolKitMain(int argc, char* argv[])
  {
    // Initialize ToolKit to serialize resources
    Main* g_proxy = new Main();
    Main::SetProxy(g_proxy);

    // Headless mode: use NullBackend (no GPU), dummy SDL video driver.
    // The packer only serializes resources — it never renders.
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    RenderSystem::UseNullBackend();

    g_proxy->PreInit();

    String publishArguments = GetFileManager()->ReadAllText("PublishArguments.txt");
    ReplaceStringInPlace(publishArguments, "\r\n", "\n"); // Normalize new lines.

    StringArray arguments;
    Split(publishArguments, "\n", arguments);

    Packer packer {};
    activeProjectName         = NormalizePath(arguments[0]);
    workspacePath             = NormalizePath(arguments[1]);
    packer.m_appName          = NormalizePath(arguments[2]);
    packer.m_deployAfterBuild = std::atoi(arguments[3].c_str());
    packer.m_minSdk           = std::atoi(arguments[4].c_str());
    packer.m_maxSdk           = std::atoi(arguments[5].c_str());
    packer.m_oriantation      = (Oriantation) std::atoi(arguments[6].c_str());
    packer.m_androidABI       = (AndroidABI) std::atoi(arguments[7].c_str()); // Read ABI value
    packer.m_platform         = (PublishPlatform) std::atoi(arguments[8].c_str());
    packer.m_icon             = arguments[9];
    packer.m_icon             = std::filesystem::absolute(packer.m_icon).string();
    packer.m_publishConfig    = (PublishConfig) std::atoi(arguments[10].c_str());
    packer.m_onlyPack         = std::atoi(arguments[11].c_str());

    // Set resource root to project's Resources folder
    g_proxy->m_resourceRoot   = ConcatPaths({workspacePath, activeProjectName, "Resources"});

    // Resolve the engine root via the per-user config directory (cross-platform).
    // The editor writes the engine's parent directory to Path.txt on startup
    // (Editor/Source/main.cpp), mirroring the logic game-project CMake uses.
    String configDir          = PlatformHelpers::GetUserConfigDir();
    String toolkitPath;
    if (!configDir.empty())
    {
      String pathFile         = ConcatPaths({configDir, "ToolKit", "Config", "Path.txt"});
      toolkitPath             = GetFileManager()->ReadAllText(pathFile);
      NormalizePathInplace(toolkitPath);
    }
    packer.m_toolkitPath            = toolkitPath;
    packer.m_templateGameFolderPath = ConcatPaths({toolkitPath, "Templates", "Game"});
    g_proxy->SetConfigPath(ConcatPaths({toolkitPath, "Config"}));

    GetLogger()->SetWriteConsoleFn([](LogType lt, String ms) -> void { printf("%s", ms.c_str()); });

    // Init SDL with dummy driver (no video / GPU required).
    // Only events and gamecontroller are needed for the engine to boot.
    SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER);

    // No window, no GL context, no InitGraphics.
    // RenderSystem already created a NullBackend (UseNullBackend was called
    // before PreInit), so all GPU calls are no-ops.
    g_proxy->m_renderSys->SetPresentCallback([]() { /* headless, no swap */ });
    g_proxy->Init();

    int result = packer.Publish();
    packer.RestoreWorkingDirectory();

    if (result != 0)
    {
      system("pause");
    }

    return result;
  }

} // namespace ToolKit

int main(int argc, char* argv[]) { return ToolKit::ToolKitMain(argc, argv); }