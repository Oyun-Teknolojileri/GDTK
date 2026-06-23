/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "Anchor.h"
#include "AndroidBuildWindow.h"
#include "App.h"
#include "ConsoleWindow.h"
#include "EditorBackendBindings.h"
#include "EditorCamera.h"
#include "EditorCanvas.h"
#include "EditorEnvironmentComponent.h"
#include "EditorViewport2d.h"
#include "Gizmo.h"
#include "Grid.h"
#include "Mod.h"
#include "PopupWindows.h"
#include "PreviewViewport.h"
#include "Stats.h"
#include "UI.h"

#include <Common/PlatformHelper.h>
#include <Common/SDLEventPool.h>
#include <Common/SplashScreen.h>
#include <FileManager.h>
#include <Platform.h>
#include <PluginManager.h>
#include <SDL.h>
#include <Types.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/imgui.h>
#include <locale.h>

SDL_Window* g_window            = nullptr;
void* g_context                 = nullptr;

// Main loop signal handle.
bool g_running                  = true;

// ToolKit Application main handle.
ToolKit::Editor::App* g_app     = nullptr;

ToolKit::SplashScreen* g_splash = nullptr;
static bool s_initDone          = false;

namespace ToolKit
{
  namespace Editor
  {

    // ToolKit main handle.
    Main* g_proxy                             = nullptr;

    // External event pool that collect and convert system events to toolkit events.
    SDLEventPool<TK_PLATFORM>* g_sdlEventPool = nullptr;

    void HandleArguments(char* argv[], int argc)
    {
      if (!g_app)
      {
        return;
      }
      String workspacePath;
      String projectName;
      for (int i = 1; i < argc; ++i)
      {
        if (strcmp(argv[i], "--workspace") == 0 && i + 1 < argc)
        {
          workspacePath = argv[i + 1];
          if (workspacePath.front() == '"' && workspacePath.back() == '"')
          {
            workspacePath = workspacePath.substr(1, workspacePath.length() - 2);
          }
          ++i;
        }
        else if (strcmp(argv[i], "--project-name") == 0 && i + 1 < argc)
        {
          projectName = argv[i + 1];
          if (projectName.front() == '"' && projectName.back() == '"')
          {
            projectName = projectName.substr(1, projectName.length() - 2);
          }
          ++i;
        }
      }

      if (!workspacePath.empty())
      {
        g_app->m_workspace->SetDefaultWorkspace(workspacePath);
        g_app->m_workspace->RefreshProjects();
      }
      if (!projectName.empty())
      {
        Project targetProject;
        targetProject.name = projectName;
        bool found         = false;
        for (const auto& proj : g_app->m_workspace->m_projects)
        {
          if (proj.name == projectName)
          {
            targetProject = proj;
            found         = true;
            break;
          }
        }

        if (found)
        {
          g_app->m_workspace->SetActiveProject(targetProject);
        }
      }
    }

    // Populates the per-user config dir with stock Workspace.settings,
    // Editor.settings, etc. so the editor has a working set on first
    // launch. The platform-specific config dir is resolved centrally
    // by PlatformHelpers::GetUserConfigDir() (declared in Common/PlatformHelper.h)
    // -- no #ifdef here.
    void CreateAppData()
    {
      String appData = PlatformHelpers::GetUserConfigDir();
      if (appData.empty())
      {
        return;
      }

      std::array<String, 8> files = {
          "Workspace.settings",
          "Editor.settings",
          "UILayout.ini",
          "Engine.settings",
          "GamePluginBuild.bat",
          "DarkTheme.settings",
          "GreyTheme.settings",
          "LightTheme.settings",
      };

      String cfgPath              = ConcatPaths({String(appData), "ToolKit", "Config"});

      // Create ToolKit Config directory if not exist
      bool doesConfigFolderExists = true;
      if (!CheckSystemFile(cfgPath))
      {
        doesConfigFolderExists = std::filesystem::create_directories(cfgPath);
      }

      // Copy config files if they don't exist
      if (doesConfigFolderExists)
      {
        for (int i = 0; i < (int) files.size(); i++)
        {
          String targetFile = ConcatPaths({cfgPath, files[i]});
          if (!CheckSystemFile(targetFile))
          {
            String sourceFile = ConcatPaths({ConfigPath(), files[i]});
            if (CheckSystemFile(sourceFile))
            {
              std::filesystem::copy(sourceFile, targetFile, std::filesystem::copy_options::overwrite_existing);
            }
          }
        }
      }

      // Update GamePluginBuild.bat with correct BUILD_CONFIG
      String buildBatPath   = ConcatPaths({cfgPath, "GamePluginBuild.bat"});
      String buildConfigStr = (TKDebug == 1) ? "Debug" : "RelWithDebInfo";

      // Read the batch file lines, replace the placeholder line, then write back
      {
        std::ifstream inFile(buildBatPath);
        if (inFile.is_open())
        {
          String line;
          StringArray lines;
          const String token = "set BUILD_CONFIG=__ENGINE_CONFIG__";

          while (std::getline(inFile, line))
          {
            if (line.find(token) != String::npos)
            {
              line = "set BUILD_CONFIG=" + buildConfigStr;
            }
            lines.push_back(line);
          }
          inFile.close();

          std::ofstream outFile(buildBatPath, std::ios::trunc);
          if (outFile.is_open())
          {
            for (String& l : lines)
            {
              outFile << l << "\n";
            }
            outFile.close();
          }
        }
      }

      // Create Path.txt file with parent directory of current path
      String pathFile = ConcatPaths({cfgPath, "Path.txt"});

      std::fstream file;
      file.open(pathFile, std::ios::trunc | std::ios::out);
      if (file.is_open())
      {
        std::filesystem::path path = std::filesystem::current_path();
        if (path.has_parent_path())
        {
          String utf8Path = PathToString(path.parent_path());
          utf8Path.erase(remove(utf8Path.begin(), utf8Path.end(), '\"'), utf8Path.end());
          UnixifyPath(utf8Path);

          file << utf8Path;
        }
        file.close();
      }

      Main::GetInstance()->SetConfigPath(cfgPath);
    }

    void ProcessEvent(const SDL_Event& e)
    {
      if (e.type == SDL_WINDOWEVENT)
      {
        if (e.window.event == SDL_WINDOWEVENT_RESIZED)
        {
          g_app->OnResize(e.window.data1, e.window.data2);
        }

        if (e.window.event == SDL_WINDOWEVENT_MAXIMIZED)
        {
          g_app->m_windowMaximized = true;
        }

        if (e.window.event == SDL_WINDOWEVENT_RESTORED)
        {
          g_app->m_windowMaximized = false;
        }
      }

      if (e.type == SDL_DROPFILE)
      {
        g_app->ManageDropfile(e.drop.file);
      }

      if (e.type == SDL_QUIT)
      {
        g_app->OnQuit();
      }

      ImGui_ImplSDL2_ProcessEvent(&e);
    }

    void PreInit()
    {
      PlatformHelpers::SetWorkingDirectoryToBinFolder();

      g_sdlEventPool = new SDLEventPool<TK_PLATFORM>();

      // PreInit Main
      g_proxy        = new Main();
      Main::SetProxy(g_proxy);
      CreateAppData();
      g_proxy->PreInit();

      // Platform dependent function assignments.
      GetPluginManager()->FreeModule      = &PlatformHelpers::TKFreeModule;
      GetPluginManager()->LoadModule      = &PlatformHelpers::TKLoadModule;
      GetPluginManager()->GetFunction     = &PlatformHelpers::TKGetFunction;
      GetPluginManager()->GetCreationTime = &PlatformHelpers::GetCreationTime;
      GetLogger()->SetPlatformConsoleFn([](LogType type, const String& msg) -> void
                                        { ToolKit::PlatformHelpers::OutputLog((int) type, msg.c_str()); });
    }

    void Init(int argc, char* argv[])
    {
      EngineSettings& settings  = GetEngineSettings();
      const String settingsFile = EngineSettingsPath();
      settings.Load(settingsFile);

      // Init SDL
      if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) < 0)
      {
        g_running = false;
      }
      else
      {
        EditorBackendBindings::PrepareWindowAttributes();

        const uint32_t windowFlags = EditorBackendBindings::GetSDLWindowFlags() | SDL_WINDOW_HIDDEN |
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS;

        g_window                   = SDL_CreateWindow(settings.m_window->GetNameVal().c_str(),
                                                      SDL_WINDOWPOS_CENTERED,
                                                      SDL_WINDOWPOS_CENTERED,
                                                      512,
                                                      512,
                                                      windowFlags);

        if (g_window == nullptr)
        {
          TK_ERR("SDL_CreateWindow Error: %s", SDL_GetError());
          g_running = false;
        }
        else
        {
          String splashFile = TexturePath("splash.png", true);
          String fontFile   = FontPath("LiberationSans-Regular.ttf", true);
          String infoText   = TKVersionStr + "  " + (TKVulkan ? "Vulkan" : "OpenGL");
          g_splash          = new ToolKit::SplashScreen(splashFile, fontFile, infoText);
          g_splash->Show(512, 512);
          g_splash->SetProgress(25.0f);
          g_splash->SetInfoText("Initializing Engine");

          g_context = EditorBackendBindings::CreateGraphicsContext(g_window);

#ifndef TK_VULKAN // vulkan does not have a context like this
          if (g_context == nullptr)
          {
            g_running = false;
          }
          else
#endif
          {
            // Init graphics backend.
            ToolKit::IGraphicsBackend::BackendInitParams initParams;
            EditorBackendBindings::FillBackendInitParams(initParams, g_window);
            initParams.errorCallback = [](const std::string& msg) -> void
            {
              if (g_app == nullptr)
              {
                return;
              }

              if (g_app->m_showGraphicsApiErrors)
              {
                TK_ERR(msg.c_str());
              }

              GetLogger()->WritePlatformConsole(LogType::Error, msg.c_str());
            };
            g_proxy->m_renderSys->InitGraphics(initParams);

            // Backend (GL context or Vulkan swapchain) now exists, so the backbuffer's actual sRGB
            // status can be queried. On GL this reads the SRGB_CAPABLE attribute the driver
            // settled on; on Vulkan it inspects the picked swapchain format. Must run *before*
            // anything that reads m_backbufferFormatIsSRGB (e.g. UI::Init's gamma-encode flag).
            g_proxy->m_renderSys->m_backbufferFormatIsSRGB = EditorBackendBindings::IsBackbufferSrgb();

            g_proxy->m_renderSys->SetPresentCallback([]() { EditorBackendBindings::PresentBackbuffer(g_window); });

            // Init Main.
            // Register app specific classes to toolkit.
            for (auto fn : GetRegisterFnList())
            {
              fn();
            }

            // Override editor classes.
            ObjectFactory* objFactory = GetObjectFactory();
            objFactory->Override<EditorDirectionalLight, DirectionalLight>();
            objFactory->Override<EditorPointLight, PointLight>();
            objFactory->Override<EditorSpotLight, SpotLight>();
            objFactory->Override<EditorScene, Scene>();
            objFactory->Override<EditorCamera, Camera>();
            objFactory->Override<EditorCanvas, Canvas>();
            objFactory->Override<EditorEnvironmentComponent, EnvironmentComponent>();

            // Override SceneManager.
            SafeDel(g_proxy->m_sceneManager);
            g_proxy->m_sceneManager = new EditorSceneManager();
            g_proxy->Init();

            GetFileManager()->m_ignorePakFile = true;

            // Set defaults
            EditorBackendBindings::SetSwapInterval(0);

            // Get the display bounds for the primary display
            SDL_Rect displayBounds;
            if (SDL_GetDisplayUsableBounds(0, &displayBounds) == 0)
            {
              // Clamp the requested window size to the display bounds
              uint width  = settings.m_window->GetWidthVal();
              uint height = settings.m_window->GetHeightVal();
              settings.m_window->SetWidthVal(glm::min(width, (uint) displayBounds.w));
              settings.m_window->SetHeightVal(glm::min(height, (uint) displayBounds.h));
            }
            else
            {
              TK_ERR("SDL_GetDisplayBounds Error: %s", SDL_GetError());
            }

            g_app = new App(settings.m_window->GetWidthVal(), settings.m_window->GetHeightVal());

            HandleArguments(argv, argc);
            g_app->m_displayBounds  = UVec2(displayBounds.w, displayBounds.h);
            g_app->m_sysComExecFn   = &ToolKit::PlatformHelpers::SysComExec;
            g_app->m_shellOpenDirFn = &ToolKit::PlatformHelpers::OpenExplorer;

            g_splash->SetProgress(50.0f);
            g_splash->SetInfoText("Initializing Editor");

            g_app->Init();

            g_splash->SetProgress(100.0f);
            g_splash->SetInfoText("Ready");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            g_splash->Hide();
            delete g_splash;
            g_splash = nullptr;

            SDL_ShowWindow(g_window);
            SDL_SetWindowBordered(g_window, SDL_TRUE);
            SDL_SetWindowResizable(g_window, SDL_TRUE);
            PlatformHelpers::UpdateAppIcon(); // Sdl wipes the editor icon. This fixes it.

            // Register update functions
            TKUpdateFn preUpdateFn = [](float deltaTime)
            {
              SDL_Event sdlEvent;
              while (SDL_PollEvent(&sdlEvent))
              {
                g_sdlEventPool->PoolEvent(sdlEvent);
                ProcessEvent(sdlEvent);
              }

              g_app->Frame(deltaTime);
            };

            g_proxy->RegisterPreUpdateFunction(preUpdateFn);

            TKUpdateFn postUpdateFn = [](float deltaTime)
            {
              g_proxy->m_renderSys->Present();
              g_sdlEventPool->ClearPool(); // Clear after consumption.
            };

            g_proxy->RegisterPostUpdateFunction(postUpdateFn);

            // Post init the engine after editor is up.
            g_proxy->PostInit();
          }
        }
      }
    }

    void Exit()
    {
      g_app->Destroy();

      g_proxy->PreUninit();
      g_proxy->Uninit();

      SafeDel(g_app);

      g_proxy->PostUninit();
      SafeDel(g_proxy);

      SafeDel(g_sdlEventPool);
      EditorBackendBindings::DestroyGraphicsContext(g_context);
      g_context = nullptr;
      SDL_DestroyWindow(g_window);
      SDL_Quit();

      g_running = false;
    }

    void TK_Loop()
    {
      while (g_running)
      {
        if (g_proxy->SyncFrameTime())
        {
          g_proxy->FrameBegin();
          g_proxy->FrameUpdate();
          g_proxy->FrameEnd();

          g_app->m_fps = g_proxy->GetCurrentFPS();
        }
      }
    }

    int ToolKit_Main(int argc, char* argv[])
    {
      PreInit();
      Init(argc, argv);

      TK_Loop();

      Exit();
      return 0;
    }

  } // namespace Editor
} // namespace ToolKit

int main(int argc, char* argv[])
{
  setlocale(LC_ALL, ".UTF-8");
  setlocale(LC_NUMERIC, "C");

#if defined(TK_DEBUG) && defined(_WIN32) && defined(_MSC_VER)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  return ToolKit::Editor::ToolKit_Main(argc, argv);
}
