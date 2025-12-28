/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EngineSettings.h"
#include "Game.h"
#include "Logger.h"
#include "SDL.h"
#include "Util.h"

#include <iostream>
#include <chrono>
#include <filesystem>
#include <unistd.h>
#include <limits.h>       // For PATH_MAX
extern "C" {
  #include <mach-o/dyld.h>  // For _NSGetExecutablePath
}

#define PLATFORM_SDL_FLAGS (SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN)

#define TK_PLATFORM PLATFORM::TKMac

namespace ToolKit
{

  inline void PlatformPreInit(Main* g_proxy)
  {
    // macOS app bundles store the executable in .app/Contents/MacOS/
    // We need to get the executable's directory and set paths relative to the project root.
    // For .app bundles: <publishDir>/Bin/<appName>.app/Contents/MacOS/<appName>
    // For dev builds: <project>/Intermediate/TKMac/Editor/Editor

    char exePath[1024];
    uint32_t size = sizeof(exePath);

    // Get the executable path using _NSGetExecutablePath (macOS-specific)
    if (_NSGetExecutablePath(exePath, &size) == 0)
    {
      // Get the directory containing the executable
      String execDir = exePath;
      size_t lastSlash = execDir.find_last_of('/');
      if (lastSlash != String::npos)
      {
        execDir = execDir.substr(0, lastSlash);
      }

      String projectRoot;

      // Check if we're running from inside an .app bundle
      if (execDir.find(".app/Contents/MacOS") != String::npos)
      {
        // We're in a .app bundle: <publishDir>/Bin/<appName>.app/Contents/MacOS/
        // Go up 4 levels to get to publish root (MacOS->Contents->.app->Bin->MacOS publish dir)
        projectRoot = ConcatPaths({execDir, "..", "..", "..", ".."});
      }
      else
      {
        // Development build: <project>/Intermediate/TKMac/Editor/
        // Go up 3 levels to get to project root
        projectRoot = ConcatPaths({execDir, "..", "..", ".."});
      }

      // Normalize the path to resolve ".." components
      char resolvedPath[1024];
      if (realpath(projectRoot.c_str(), resolvedPath) != nullptr)
      {
        projectRoot = resolvedPath;
      }

      std::cout << "Project root set to: " << projectRoot << "\n";

      g_proxy->m_resourceRoot = ConcatPaths({projectRoot, "Resources"});
      g_proxy->m_defaultResourceRoot = ConcatPaths({projectRoot, "Resources", "Engine"});
      g_proxy->m_cfgPath = ConcatPaths({projectRoot, "Config"});

      // Change the current working directory to the project root
      // This is needed because some code uses relative paths based on CWD
      std::filesystem::current_path(projectRoot);

      std::cout << "Executable path: " << exePath << "\n";
      std::cout << "Project root: " << projectRoot << "\n";
      std::cout << "Default resource root: " << g_proxy->m_defaultResourceRoot << "\n";
    }
    else
    {
      // Fallback (in case _NSGetExecutablePath fails)
      g_proxy->m_resourceRoot = "./../../Resources";
      g_proxy->m_defaultResourceRoot = "./../../Resources/Engine";
      g_proxy->m_cfgPath      = "./../../Config";
    }

    std::cout << "Resource root path set to: " << g_proxy->m_resourceRoot << "\n";
    std::cout << "Config path set to: " << g_proxy->m_cfgPath << "\n";

    // Setup console logging — macOS uses stdout normally.
    GetLogger()->SetWriteConsoleFn([](LogType lt, String ms) -> void {
      std::cout << ms << std::endl;
    });

    // Optional: Also mirror to platform console (so TK_SYSLOG shows in system logs)
    GetLogger()->SetPlatformConsoleFn([](LogType lt, String ms) -> void {
      std::cout << "[macOS] " << ms << std::endl;
    });
  }

  inline void PlatformMainLoop(bool* running, void (*TK_Loop)())
  {
    // Basic frame loop — identical to Windows for now.
    while (*running)
    {
      TK_Loop();
    }
  }

  inline void PlatformAdjustEngineSettings(int availableWidth, int availableHeight, EngineSettings* engineSettings)
  {
    // macOS retina / display scaling handling can be added here if needed.
    // For now, just use what’s available.
    engineSettings->m_window->SetWidthVal(static_cast<uint>(availableWidth));
    engineSettings->m_window->SetHeightVal(static_cast<uint>(availableHeight));
  }

} // namespace ToolKit
