/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EditorTypes.h"

namespace ToolKit
{
  namespace Editor
  {

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
      String projectDir;
      String buildConfig;
      String toolkitPath;
      String publishDirectory;
      String publishBinDir;
      String publishConfigDir;
    };

    struct MacOSBuildConfig : public BuildConfig
    {
      // macOS-specific paths
      String cmakeBundlePath;    // Intermediate CMake output: ProjectDir/Codes/Bin/appName.app
      String targetBundlePath;   // Final location: publishBinDir/appName.app

      // Bundle internal paths
      String contentsPath;       // targetBundlePath/Contents
      String macOSPath;          // Contents/MacOS
      String resourcesPath;      // Contents/Resources
      String frameworksPath;     // Contents/Frameworks
      String executablePath;     // MacOS/appName
    };

    struct WindowsBuildConfig : public BuildConfig
    {
      String architecture;       // "x64", "Win32"
      String executablePath;     // publishBinDir/appName.exe
      String sdl2DllPath;        // SDL2.dll or SDL2d.dll path
    };

    struct AndroidBuildConfig : public BuildConfig
    {
      String packageName;        // com.company.appname
      String gradlePath;         // projectDir/Android
      String assetsPath;         // Android/app/src/main/assets
      String apkOutputPath;      // Android/app/build/outputs/apk/...
      int minSdk;
      int maxSdk;
      MobileOriantation orientation;
      AndroidABI abi;
    };

    struct WebBuildConfig : public BuildConfig
    {
      String htmlOutputPath;     // publishBinDir/appName.html
      String wasmOutputPath;     // publishBinDir/appName.wasm
      String dataOutputPath;     // publishBinDir/appName.data
    };

    struct PluginBuildConfig : public BuildConfig
    {
      String pluginName;
      String pluginOutputPath;   // GetBinPath()/pluginName.dll or .so
      bool isEditorPlugin;
    };

    class TK_EDITOR_API PublishManager
    {
     public:
      void Publish(PublishPlatform platform, PublishConfig publishConfig);
      void Pack();

     private:
      String ConstructPublishArgs(PublishPlatform platform, PublishConfig publishConfig, bool packOnly);
      void DirectPluginBuild(PublishPlatform platform, PublishConfig publishConfig);
      void DirectMacOSBuild(PublishConfig publishConfig);

      // Helper functions
      String GetBuildConfigString(PublishConfig publishConfig);
      String GetToolkitPath();

      // macOS build helper functions
      MacOSBuildConfig CreateMacOSBuildConfig(PublishConfig publishConfig);
      void CreateMacOSPublishDirectories(const MacOSBuildConfig& config);
      void BundleSDL2Library(const MacOSBuildConfig& config);
      void GenerateInfoPlist(const MacOSBuildConfig& config);
      void CopyMacOSResources(const MacOSBuildConfig& config);
      void CopyMacOSConfig(const MacOSBuildConfig& config);

      // macOS build callback functions
      void OnMacOSConfigureComplete(int exitCode, PublishConfig publishConfig, const String& buildCmd);
      void OnMacOSBuildComplete(int exitCode, PublishConfig publishConfig);

     public:
      TexturePtr m_icon = nullptr;
      String m_appName;
      String m_pluginName;
      bool m_deployAfterBuild = false;
      int m_minSdk            = 27;
      int m_maxSdk            = 32;

      MobileOriantation m_oriantation;
      bool m_isBuilding        = false;
      AndroidABI m_selectedABI = AndroidABI::All;

      // macOS specific settings
      String m_bundleIdentifier = "com.otsoftware.game";
      String m_minMacOSVersion  = "11.0";
    };

  } // namespace Editor
} // namespace ToolKit
