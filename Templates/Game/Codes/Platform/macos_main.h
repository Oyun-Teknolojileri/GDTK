/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 * Author: erendgrmnc
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
    char exePath[1024];
    uint32_t size = sizeof(exePath);

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

      if (execDir.find(".app/Contents/MacOS") != String::npos)
      {
        projectRoot = ConcatPaths({execDir, "..", "..", "..", ".."});
      }
      else
      {
        projectRoot = ConcatPaths({execDir, "..", "..", ".."});
      }
      char resolvedPath[1024];
      if (realpath(projectRoot.c_str(), resolvedPath) != nullptr)
      {
        projectRoot = resolvedPath;
      }

      std::cout << "Project root set to: " << projectRoot << "\n";

      g_proxy->m_resourceRoot = ConcatPaths({projectRoot, "Resources"});
      g_proxy->m_defaultResourceRoot = ConcatPaths({projectRoot, "Resources", "Engine"});
      g_proxy->m_cfgPath = ConcatPaths({projectRoot, "Config"});

      std::filesystem::current_path(projectRoot);

      std::cout << "Executable path: " << exePath << "\n";
      std::cout << "Project root: " << projectRoot << "\n";
      std::cout << "Default resource root: " << g_proxy->m_defaultResourceRoot << "\n";
    }
    else
    {
      g_proxy->m_resourceRoot = "./../../Resources";
      g_proxy->m_defaultResourceRoot = "./../../Resources/Engine";
      g_proxy->m_cfgPath      = "./../../Config";
    }

    std::cout << "Resource root path set to: " << g_proxy->m_resourceRoot << "\n";
    std::cout << "Config path set to: " << g_proxy->m_cfgPath << "\n";

    GetLogger()->SetWriteConsoleFn([](LogType lt, String ms) -> void {
      std::cout << ms << std::endl;
    });

    GetLogger()->SetPlatformConsoleFn([](LogType lt, String ms) -> void {
      std::cout << "[macOS] " << ms << std::endl;
    });
  }

  inline void PlatformMainLoop(bool* running, void (*TK_Loop)())
  {
    while (*running)
    {
      TK_Loop();
    }
  }

  inline void PlatformAdjustEngineSettings(int availableWidth, int availableHeight, EngineSettings* engineSettings)
  {
    engineSettings->m_window->SetWidthVal(static_cast<uint>(availableWidth));
    engineSettings->m_window->SetHeightVal(static_cast<uint>(availableHeight));
  }

} // namespace ToolKit
