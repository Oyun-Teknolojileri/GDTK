/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Anchor.h"
#include "AndroidBuildWindow.h"
#include "App.h"
#include "ConsoleWindow.h"
#include "EditorCamera.h"
#include "EditorCanvas.h"
#include "EditorViewport2d.h"
#include "Gizmo.h"
#include "Grid.h"
#include "Mod.h"
#include "PopupWindows.h"
#include "PreviewViewport.h"
#include "SplashScreenRenderPath.h"
#include "Stats.h"
#include "UI.h"

#include <Common/SDLEventPool.h>
#if defined(_WIN32)
  #include <Common/Win32Utils.h>
#elif defined(__APPLE__)
  #include <Common/MacUtils.h>
  #include <mach-o/dyld.h>  // For _NSGetExecutablePath
#endif
#include <FileManager.h>
#include "imgui/backends/imgui_impl_sdl2.h"
#include <PluginManager.h>
#include <SDL.h>
#include <TKOpenGL.h>
#include <Types.h>
#include <locale.h>

#include <array>
#include <chrono>

SDL_Window* g_window        = nullptr;
SDL_GLContext g_context     = nullptr;

// Main loop signal handle.
bool g_running              = true;

// ToolKit Application main handle.
ToolKit::Editor::App* g_app = nullptr;

namespace ToolKit
{
  namespace Editor
  {

    // ToolKit main handle.
    Main* g_proxy                             = nullptr;

    // External event pool that collect and convert system events to toolkit events.
    SDLEventPool<TK_PLATFORM>* g_sdlEventPool = nullptr;

    // Get the project root directory from the executable path
    String GetProjectRoot()
    {
#if defined(__APPLE__)
        char exePath[1024];
        uint32_t size = sizeof(exePath);

        if (_NSGetExecutablePath(exePath, &size) == 0)
        {
            String execDir = exePath;
            size_t lastSlash = execDir.find_last_of('/');
            if (lastSlash != String::npos)
            {
                execDir = execDir.substr(0, lastSlash);
            }

            // Go up 3 levels: Editor -> TKMac -> Intermediate -> Project Root
            String projectRoot = ConcatPaths({execDir, "..", "..", ".."});

            char resolvedPath[1024];
            if (realpath(projectRoot.c_str(), resolvedPath) != nullptr)
            {
                return String(resolvedPath);
            }
        }
        return "";
#elif defined(_WIN32)
        // Windows implementation would go here if needed
        return "";
#else
        return "";
#endif
    }

    // Windows util function for creating ToolKit config files in AppData.
    void CreateAppData()
    {
        // Determine base path for config files
        const char* rawAppData = nullptr;

    #if defined(_WIN32)
        rawAppData = std::getenv("APPDATA");
        if (!rawAppData)
        {
            // fallback to HOME if APPDATA not set (unlikely on Windows)
            rawAppData = std::getenv("HOME");
        }
    #else
        // macOS / Linux fallback: use HOME directory
        rawAppData = std::getenv("HOME");
    #endif

        if (!rawAppData)
        {
            TK_ERR("Could not determine a base directory for config files.");
            return;
        }

        StringView appData(rawAppData);

        // Build config folder path
        String cfgPath = ConcatPaths({String(appData), "ToolKit", "Config"});

        // Create ToolKit Config directory if it doesn't exist
        if (!CheckSystemFile(cfgPath))
        {
            if (!std::filesystem::create_directories(cfgPath))
            {
                TK_ERR("Failed to create config directory: %s", cfgPath.c_str());
                return;
            }
        }

        // Get the project root to find source config files
        String projectRoot = GetProjectRoot();
        String sourceConfigPath;

        if (!projectRoot.empty())
        {
            sourceConfigPath = ConcatPaths({projectRoot, "Config"});
            std::cout << "Project root: " << projectRoot << "\n";
            std::cout << "Source config path: " << sourceConfigPath << "\n";
        }
        else
        {
            TK_ERR("Could not determine project root directory.");
            return;
        }

        // Default config files
        std::array<String, 5> files = {
            "Workspace.settings",
            "Editor.settings",
            "UILayout.ini",
            "Engine.settings",
#if defined(_WIN32)
            "GamePluginBuild.bat"
#else
            "GamePluginBuild.sh"
#endif
        };

        // Copy missing config files from source
        for (auto& fileName : files)
        {
            String targetFile = ConcatPaths({cfgPath, fileName});
            if (!CheckSystemFile(targetFile))
            {
                String sourceFile = ConcatPaths({sourceConfigPath, fileName});
                std::cout << "Copying: " << sourceFile << " -> " << targetFile << "\n";

                if (CheckSystemFile(sourceFile))
                {
                    std::filesystem::copy(
                        sourceFile,
                        targetFile,
                        std::filesystem::copy_options::overwrite_existing
                    );
                }
                else
                {
                    TK_WRN("Source config file not found: %s", sourceFile.c_str());
                }
            }
        }

        // Update GamePluginBuild script with correct BUILD_CONFIG
#if defined(_WIN32)
        String buildScriptPath = ConcatPaths({cfgPath, "GamePluginBuild.bat"});
        const String setToken = "set BUILD_CONFIG=__ENGINE_CONFIG__";
        const String setPrefix = "set BUILD_CONFIG=";
#else
        String buildScriptPath = ConcatPaths({cfgPath, "GamePluginBuild.sh"});
        const String setToken = "BUILD_CONFIG=\"__ENGINE_CONFIG__\"";
        const String setPrefix = "BUILD_CONFIG=\"";
#endif
        String buildConfigStr = (TKDebug == 1) ? "Debug" : "RelWithDebInfo";

        if (CheckSystemFile(buildScriptPath))
        {
            StringArray lines;
            std::ifstream inFile(buildScriptPath);
            String line;

            while (std::getline(inFile, line))
            {
                if (line.find(setToken) != String::npos)
                {
#if defined(_WIN32)
                    line = setPrefix + buildConfigStr;
#else
                    line = setPrefix + buildConfigStr + "\"";
#endif
                }
                lines.push_back(line);
            }
            inFile.close();

            std::ofstream outFile(buildScriptPath, std::ios::trunc);
            for (auto& l : lines)
            {
                outFile << l << "\n";
            }
            outFile.close();

#if !defined(_WIN32)
            // Make shell script executable on Unix-like systems
            std::filesystem::permissions(buildScriptPath,
                std::filesystem::perms::owner_all |
                std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                std::filesystem::perms::others_read | std::filesystem::perms::others_exec);
#endif
        }

        // Create Path.txt file with project root path
        String pathFile = ConcatPaths({cfgPath, "Path.txt"});
        std::ofstream file(pathFile, std::ios::trunc);
        if (file.is_open())
        {
            UnixifyPath(projectRoot);
            file << projectRoot;
            std::cout << "Path.txt created with: " << projectRoot << "\n";
        }

        // Set final config path in Main
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
      g_sdlEventPool = new SDLEventPool<TK_PLATFORM>();

      // PreInit Main
      g_proxy        = new Main();
      Main::SetProxy(g_proxy);

      // Set resource paths based on executable location (cross-platform)
      String projectRoot = GetProjectRoot();
      if (!projectRoot.empty())
      {
        g_proxy->m_resourceRoot = ConcatPaths({projectRoot, "Resources"});
        g_proxy->m_defaultResourceRoot = ConcatPaths({projectRoot, "Resources", "Engine"});
        std::cout << "Editor - Resource root: " << g_proxy->m_resourceRoot << "\n";
        std::cout << "Editor - Default resource root: " << g_proxy->m_defaultResourceRoot << "\n";
      }
      else
      {
        TK_WRN("Could not determine project root, using relative paths");
        g_proxy->m_resourceRoot = ConcatPaths({".", "..", "Resources"});
        g_proxy->m_defaultResourceRoot = ConcatPaths({".", "..", "Resources", "Engine"});
      }

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

    void Init()
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

#ifdef TK_GL_ES_3_0
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

// Opengl debuging & profiling features requires es 3_2 context
#ifdef TK_GL_ES_3_2
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#endif

// macOS / Desktop OpenGL: Use Core Profile 3.3 or higher
#if defined(__APPLE__) && !defined(TK_GL_ES_3_0) && !defined(TK_GL_ES_3_2)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

#ifdef TK_DEBUG
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

        g_window =
            SDL_CreateWindow(settings.m_window->GetNameVal().c_str(),
                             SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED,
                             512,
                             512,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS);

        if (g_window == nullptr)
        {
          TK_ERR("SDL_CreateWindow Error: %s", SDL_GetError());
          g_running = false;
        }
        else
        {
          g_context = SDL_GL_CreateContext(g_window);

          if (g_context == nullptr)
          {
            g_running = false;
          }
          else
          {
            SDL_GL_MakeCurrent(g_window, g_context);

            // Init OpenGl.
            g_proxy->m_renderSys->InitGl(SDL_GL_GetProcAddress,
                                         [](const std::string& msg) -> void
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
                                         });

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

            // Override SceneManager.
            SafeDel(g_proxy->m_sceneManager);
            g_proxy->m_sceneManager = new EditorSceneManager();
            g_proxy->Init();

            GetFileManager()->m_ignorePakFile = true;

            // Set defaults
            SDL_GL_SetSwapInterval(0);

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

            // Init app
            g_app                   = new App(settings.m_window->GetWidthVal(), settings.m_window->GetHeightVal());
            g_app->m_displayBounds  = UVec2(displayBounds.w, displayBounds.h);
            g_app->m_sysComExecFn   = &ToolKit::PlatformHelpers::SysComExec;
            g_app->m_shellOpenDirFn = &ToolKit::PlatformHelpers::OpenExplorer;

            // Register update functions
            TKUpdateFn preUpdateFn  = [](float deltaTime)
            {
              SDL_Event sdlEvent;
              while (SDL_PollEvent(&sdlEvent))
              {
                g_sdlEventPool->PoolEvent(sdlEvent);
                ProcessEvent(sdlEvent);
              }

              static bool showSplashScreen                    = true;
              static float elapsedTime                        = 0.0f;
              static SplashScreenRenderPathPtr splashRenderer = nullptr;

              if (showSplashScreen)
              {
                RenderSystem* rsys = GetRenderSystem();

                if (splashRenderer == nullptr)
                {
                  SDL_ShowWindow(g_window);
                  splashRenderer = MakeNewPtr<SplashScreenRenderPath>();
                  splashRenderer->Init({512, 512});
                }

                if (elapsedTime < 1000.0f)
                {
                  elapsedTime += deltaTime;
                  rsys->AddRenderTask({[](Renderer* renderer) -> void { splashRenderer->Render(renderer); }});
                }
                else
                {
                  showSplashScreen = false;
                  splashRenderer   = nullptr;
                  g_app->Init();

                  SDL_SetWindowBordered(g_window, SDL_TRUE);
                  SDL_SetWindowResizable(g_window, SDL_TRUE);
                  PlatformHelpers::UpdateAppIcon(); // Sdl wipes the editor icon. This fixes it.
                }
              }
              else
              {
                g_app->Frame(deltaTime);
              }
            };

            g_proxy->RegisterPreUpdateFunction(preUpdateFn);

            TKUpdateFn postUpdateFn = [](float deltaTime)
            {
              SDL_GL_MakeCurrent(g_window, g_context);
              SDL_GL_SwapWindow(g_window);

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
      g_proxy->PostUninit();

      SafeDel(g_app);
      SafeDel(g_proxy);

      SafeDel(g_sdlEventPool);
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
      Init();

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

#ifdef TK_DEBUG
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  return ToolKit::Editor::ToolKit_Main(argc, argv);
}
