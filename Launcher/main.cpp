/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Launcher.h"

#include <Common/SDLEventPool.h>
#include <Common/Win32Utils.h>
#include <EngineSettings.h>
#include <FileManager.h>
#include <Image.h>
#include <Object.h>
#include <RenderSystem.h>
#include <SDL.h>
#include <TKOpenGL.h>
#include <Types.h>

#define IMGUI_USER_CONFIG "tk_imconfig.h"
#include <ImGui/backends/imgui_impl_opengl3.h>
#include <ImGui/backends/imgui_impl_sdl2.h>
#include <imgui/imgui.h>
#include <locale.h>

#include <array>
#include <filesystem>
#include <fstream>

SDL_HitTestResult LauncherHitTest(SDL_Window* window, const SDL_Point* area, void* data)
{
  int w;
  SDL_GetWindowSize(window, &w, nullptr);
  if (area->y < 50 && area->x < w - 40)
  {
    return SDL_HITTEST_DRAGGABLE;
  }
  return SDL_HITTEST_NORMAL;
}

SDL_Window* g_window   = nullptr;
void* g_context        = nullptr;
bool g_running         = true;
bool g_launcherRunning = true;

namespace ToolKit
{
  namespace Launcher
  {
    static Main* g_proxy                             = nullptr;
    static SDLEventPool<TK_PLATFORM>* g_sdlEventPool = nullptr;
    static LauncherApp* g_launcher                   = nullptr;

    void CreateAppData()
    {
      StringView appData = getenv("APPDATA");
      if (appData.empty())
      {
        return;
      }

      String cfgPath = ConcatPaths({String(appData), "ToolKit", "Config"});
      if (!CheckSystemFile(cfgPath))
      {
        std::filesystem::create_directories(cfgPath);
      }

      std::array<String, 5> configFiles = {"Workspace.settings",
                                           "Engine.settings",
                                           "DarkTheme.settings",
                                           "GreyTheme.settings",
                                           "LightTheme.settings"};
      for (const String& f : configFiles)
      {
        String target = ConcatPaths({cfgPath, f});
        if (!CheckSystemFile(target))
        {
          String source = ConcatPaths({ConfigPath(), f});
          if (CheckSystemFile(source))
          {
            std::filesystem::copy(source, target, std::filesystem::copy_options::overwrite_existing);
          }
        }
      }

      String pathFile = ConcatPaths({cfgPath, "Path.txt"});
      std::fstream file(pathFile, std::ios::trunc | std::ios::out);
      if (file.is_open())
      {
        std::filesystem::path path = std::filesystem::current_path();
        if (path.has_parent_path())
        {
          String utf8Path = path.parent_path().u8string();
          utf8Path.erase(remove(utf8Path.begin(), utf8Path.end(), '\"'), utf8Path.end());
          UnixifyPath(utf8Path);
          file << utf8Path;
        }
        file.close();
      }

      Main::GetInstance()->SetConfigPath(cfgPath);
    }

    void PreInit()
    {
      PlatformHelpers::SetWorkingDirectoryToBinFolder();

      g_sdlEventPool = new SDLEventPool<TK_PLATFORM>();
      g_proxy        = new Main();
      Main::SetProxy(g_proxy);
      CreateAppData();
      g_proxy->PreInit();

      GetLogger()->SetPlatformConsoleFn([](LogType type, const String& msg)
                                        { PlatformHelpers::OutputLog((int) type, msg.c_str()); });
    }

    void Init()
    {
      EngineSettings& settings = GetEngineSettings();
      settings.Load(EngineSettingsPath());

      SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
      SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
      if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
      {
        g_running = false;
        return;
      }

      SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
      SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
      SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
      SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
      SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

      SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);

      g_window = SDL_CreateWindow("ToolKit Launcher",
                                  SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED,
                                  1024,
                                  768,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);

      if (g_window == nullptr)
      {
        g_running = false;
        return;
      }

      SDL_SetWindowHitTest(g_window, LauncherHitTest, nullptr);

      int srgbFlag = 0;
      SDL_GL_GetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, &srgbFlag);
      g_proxy->m_renderSys->m_backbufferFormatIsSRGB = (srgbFlag == 1);

      g_context                                      = SDL_GL_CreateContext(g_window);
      if (g_context == nullptr)
      {
        g_running = false;
        return;
      }

      SDL_GL_MakeCurrent(g_window, g_context);

      g_proxy->m_renderSys->InitGl(SDL_GL_GetProcAddress,
                                   [](const std::string& msg)
                                   { GetLogger()->WritePlatformConsole(LogType::Error, msg.c_str()); });

      g_proxy->Init();

      for (auto fn : GetRegisterFnList())
      {
        fn();
      }

      GetFileManager()->m_ignorePakFile = true;
      SDL_GL_SetSwapInterval(0);

      // ImGui.
      IMGUI_CHECKVERSION();
      ImGui::CreateContext();

      ImGuiIO& io                           = ImGui::GetIO();
      io.ConfigFlags                       |= ImGuiConfigFlags_DockingEnable;
      io.ConfigWindowsMoveFromTitleBarOnly  = true;

      ImGui_ImplSDL2_InitForOpenGL(g_window, g_context);
      ImGui_ImplOpenGL3_Init("#version 300 es");

      DeserializeThemeSettings();

      g_launcher                                     = new LauncherApp();
      g_launcher->m_sysComExecFn                     = &PlatformHelpers::SysComExec;
      g_launcher->m_createProjectShortcutOnDesktopFn = [](const String& name, const String& args)
      {
#ifdef TK_DEBUG
        String editorExe = std::filesystem::current_path().u8string() + "/Editord.exe";
#else
        String editorExe = std::filesystem::current_path().u8string() + "/Editor.exe";
#endif
        UnixifyPath(editorExe);
        PlatformHelpers::CreateProjectShortcutOnDesktop(name, args, editorExe);
      };

      if (!g_proxy->m_renderSys->m_backbufferFormatIsSRGB)
      {
        io.BackendFlags |= ImGuiBackendFlags_ToolKitGammaEncode;
      }

      TKUpdateFn preUpdateFn = [](float deltaTime)
      {
        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent))
        {
          if (sdlEvent.type == SDL_QUIT)
          {
            g_running = false;
          }
          g_sdlEventPool->PoolEvent(sdlEvent);
          ImGui_ImplSDL2_ProcessEvent(&sdlEvent);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        g_launcher->ShowLauncherWindow();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ImGui::EndFrame();
      };

      g_proxy->RegisterPreUpdateFunction(preUpdateFn);

      TKUpdateFn postUpdateFn = [](float deltaTime)
      {
        SDL_GL_SwapWindow(g_window);
        g_sdlEventPool->ClearPool();
      };

      g_proxy->RegisterPostUpdateFunction(postUpdateFn);

      g_proxy->PostInit();
    }

    void Exit()
    {
      SafeDel(g_launcher);

      ImGui_ImplOpenGL3_Shutdown();
      ImGui_ImplSDL2_Shutdown();
      ImGui::DestroyContext();

      g_proxy->PreUninit();
      g_proxy->Uninit();
      g_proxy->PostUninit();

      SafeDel(g_proxy);
      SafeDel(g_sdlEventPool);

      SDL_DestroyWindow(g_window);
      SDL_Quit();
    }

    int Launcher_Main(int argc, char* argv[])
    {
      PreInit();
      Init();

      while (g_running)
      {
        if (g_proxy->SyncFrameTime())
        {
          g_proxy->FrameBegin();
          g_proxy->FrameUpdate();
          g_proxy->FrameEnd();
        }
      }

      Exit();
      return 0;
    }

  } // namespace Launcher
} // namespace ToolKit

int main(int argc, char* argv[])
{
  setlocale(LC_ALL, ".UTF-8");
  setlocale(LC_NUMERIC, "C");

#ifdef TK_DEBUG
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  return ToolKit::Launcher::Launcher_Main(argc, argv);
}
