/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EditorTypes.h"
#include <memory>
#include <functional>

namespace ToolKit
{
  namespace Editor
  {
    // Forward declarations
    class App;
    enum class PublishPlatform;
    enum class PublishConfig;

    struct BuildConfig;
    struct MacOSBuildConfig;
    struct WindowsBuildConfig;
    struct PluginBuildConfig;

    /**
     * Manages asynchronous build operations for the Editor.
     * This class handles the lifecycle of build processes, ensuring that
     * all resources remain valid during async operations.
     */
    class AsyncBuildManager : public std::enable_shared_from_this<AsyncBuildManager>
    {
    public:
      AsyncBuildManager(App* app);
      ~AsyncBuildManager();

      // Main entry point for builds
      void StartBuild(PublishPlatform platform, PublishConfig config, const BuildConfig& buildConfig, bool isCapturingOutput = true);

      // Cancel any ongoing build
      void CancelBuild();

      // Check if a build is in progress
      bool IsBuildActive() const { return m_buildActive; }

    private:
      void BuildPlugin(PublishPlatform platform, PublishConfig config, const BuildConfig& buildConfig);


#ifdef TK_WIN
      void DirectWindowsBuild(PublishConfig publishConfig);
      void OnWindowsConfigureComplete(int exitCode, PublishConfig publishConfig, const String& buildCmd);
      void OnWindowsBuildComplete(int exitCode, PublishConfig publishConfig);
#endif // TK_WIN

#ifdef TK_MAC
      // macOS-specific build functions (only declared on macOS)
      void DirectMacOSBuild(PublishConfig publishConfig);
      MacOSBuildConfig CreateMacOSBuildConfig(PublishConfig publishConfig);
      void CreateMacOSPublishDirectories(const MacOSBuildConfig& config);
      void BundleSDL2Library(const MacOSBuildConfig& config);
      void GenerateInfoPlist(const MacOSBuildConfig& config);
      void CopyMacOSResources(const MacOSBuildConfig& config);
      void CopyMacOSConfig(const MacOSBuildConfig& config);
      void OnMacOSConfigureComplete(int exitCode, PublishConfig publishConfig, const String& buildCmd);
      void OnMacOSBuildComplete(int exitCode, PublishConfig publishConfig);
#endif // TK_MAC


      int ExecCommand(StringView cmd, bool async, bool showConsole,
                     std::function<void(int)> callback, bool captureOutput = false);

      void OnBuildComplete(PublishPlatform platform, int exitCode);

      String GetBuildConfigString(PublishConfig publishConfig);
      String GetToolkitPath();

    private:
      App* m_app;
      bool m_buildActive;
      bool m_isCapturingOutput;
      std::unique_ptr<BuildConfig> m_currentBuildConfig;

      String m_bundleIdentifier;
      String m_minMacOSVersion;
    };

  } // namespace Editor
} // namespace ToolKit
