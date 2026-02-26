/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 * Author: erendgrmnc
 */

#pragma once

#include "EditorTypes.h"

namespace ToolKit
{
  namespace Editor
  {
    // Forward declaration
    class App;
    class AsyncBuildManager;

    enum class PublishConfig
    {
      Debug   = 0, // Debug build
      Develop = 1, // Release build
      Deploy  = 2  // Release build with calling packer
    };

    enum class PublishPlatform
    {
      Web,
      Windows,
      Linux,
      Android,
      MacOS,
      GamePlugin,
      EditorPlugin
    };

    enum class AndroidABI
    {
      All,
      ArmeabiV7a,
      Arm64V8a,
      X86,
      X86_64
    };

    enum class MobileOriantation
    {
      Undefined,
      Landscape,
      Portrait
    };

    struct BuildConfig
    {
      String appName;
      String pluginName;
      String projectDir;
      String buildConfig;
      String toolkitPath;
      String publishDirectory;
      String publishBinDir;
      String publishConfigDir;
      TexturePtr icon       = nullptr;
      bool deployAfterBuild = false;
    };

    struct MacOSBuildConfig : public BuildConfig
    {
      // macOS-specific settings
      String bundleIdentifier = "com.otsoftware.game";
      String minMacOSVersion  = "11.0";

      // macOS-specific paths
      String cmakeBundlePath;  // Intermediate CMake output: ProjectDir/Codes/Bin/appName.app
      String targetBundlePath; // Final location: publishBinDir/appName.app

      // Bundle internal paths
      String contentsPath;   // targetBundlePath/Contents
      String macOSPath;      // Contents/MacOS
      String resourcesPath;  // Contents/Resources
      String frameworksPath; // Contents/Frameworks
      String executablePath; // MacOS/appName
    };

    struct WindowsBuildConfig : public BuildConfig
    {
      String architecture;   // "x64", "Win32"
      String executablePath; // publishBinDir/appName.exe
      String sdl2DllPath;    // SDL2.dll or SDL2d.dll path
    };

    struct AndroidBuildConfig : public BuildConfig
    {
      String packageName;   // com.company.appname
      String gradlePath;    // projectDir/Android
      String assetsPath;    // Android/app/src/main/assets
      String apkOutputPath; // Android/app/build/outputs/apk/...
      int minSdk;
      int maxSdk;
      MobileOriantation orientation;
      AndroidABI abi;
    };

    struct WebBuildConfig : public BuildConfig
    {
      String htmlOutputPath; // publishBinDir/appName.html
      String wasmOutputPath; // publishBinDir/appName.wasm
      String dataOutputPath; // publishBinDir/appName.data
    };

    struct PluginBuildConfig : public BuildConfig
    {
      String pluginName;
      String pluginOutputPath; // GetBinPath()/pluginName.dll or .so
      bool isEditorPlugin;
    };

    class TK_EDITOR_API PublishManager
    {
     public:
      PublishManager(App* app);
      ~PublishManager();

      void Publish(PublishPlatform platform, PublishConfig publishConfig, bool isAsync = true);
      void Pack();

     private:
      String ConstructPublishArgs(PublishPlatform platform, PublishConfig publishConfig, bool packOnly);

      std::shared_ptr<AsyncBuildManager> m_buildManager;

     public:
      // UI-bound settings that are used to populate build configs
      TexturePtr m_icon = nullptr;
      String m_appName;
      String m_pluginName;
      bool m_deployAfterBuild = false;

      // Android-specific settings
      int m_minSdk            = 27;
      int m_maxSdk            = 32;
      MobileOriantation m_oriantation;
      AndroidABI m_selectedABI  = AndroidABI::All;

      // macOS-specific settings
      String m_bundleIdentifier = "com.otsoftware.game";
      String m_minMacOSVersion  = "11.0";

      bool m_isBuilding         = false;
    };

  } // namespace Editor
} // namespace ToolKit
