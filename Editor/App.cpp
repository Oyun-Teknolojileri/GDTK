/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "App.h"

#include "AndroidBuildWindow.h"
#include "ConsoleWindow.h"
#include "EditorCamera.h"
#include "EditorMetaKeys.h"
#include "EditorViewport2d.h"
#include "Grid.h"
#include "OutlinerWindow.h"
#include "OverlayUI.h"
#include "PluginWindow.h"
#include "PopupWindows.h"
#include "PropInspectorWindow.h"
#include "RenderSettingsWindow.h"
#include "StatsWindow.h"
#include "StatusBar.h"

#include <DirectionComponent.h>
#include <FileManager.h>
#include <Mesh.h>
#include <PluginManager.h>
#include <Resource.h>
#include <SDL.h>
#include <Stats.h>
#include <UIManager.h>

#include <sstream>

extern ToolKit::Editor::App* g_app; // Defined in main cpp to provide global handle for app.
extern bool g_running;              // Defined in main cpp that controls the main loop's life time.

extern "C" TK_EDITOR_API ToolKit::Editor::App* TK_STDCAL GetApp() { return g_app; };

namespace ToolKit
{
  namespace Editor
  {

    App::App(int windowWidth, int windowHeight)
    {
      UI::Init();

      m_cursor           = nullptr;
      RenderSystem* rsys = GetRenderSystem();
      rsys->SetAppWindowSize((uint) windowWidth, (uint) windowHeight);
      SetStatusMsg(g_statusOk);
    }

    App::~App()
    {
      Destroy();
      UI::UnInit();
    }

    void App::Init()
    {
      ImplementMetaKeys();
      AssignManagerReporters();
      CreateEditorEntities();

      ModManager::GetInstance()->Init();
      ModManager::GetInstance()->SetMod(true, ModId::Select);
      ActionManager::GetInstance()->Init();

      m_workspace.Init();

      // Load the last scene or create a new scene.
      String lastScene = m_workspace.GetActiveProject().scene;
      if (lastScene.empty())
      {
        CreateNewScene();
      }
      else
      {
        // ApplyProjectSettings uses OpenSceneAsync to load the last open scene,
        // In the mean time, we have to show a dummy scene.
        if (SceneManager* sceneMan = GetSceneManager())
        {
          ScenePtr defaultScene = sceneMan->Create<Scene>(ScenePath("Empty" + SCENE, true));
          sceneMan->SetCurrentScene(defaultScene);
        }
      }

      ApplyProjectSettings(false);

      if (!CheckFile(m_workspace.GetActiveWorkspace()))
      {
        StringInputWindowPtr wsDir = MakeNewPtr<StringInputWindow>("Set Workspace Directory##SetWsdir", false);
        wsDir->m_hint              = "User/Documents/ToolKit";
        wsDir->m_inputLabel        = "Workspace Directory";
        wsDir->m_name              = "Set Workspace Directory";
        wsDir->AddToUI();

        wsDir->m_taskFn = [](const String& val) -> void
        {
          String cmd = "SetWorkspaceDir --path \"" + val + "\"";
          g_app->GetConsole()->ExecCommand(cmd);
        };
      }
      else
      {
        m_workspace.RefreshProjects();
      }

      m_simulatorSettings.Resolution = EmulatorResolution::Custom;
      m_publishManager               = new PublishManager();
      GetRenderSystem()->SetClearColor(g_wndBgColor);
    }

    void App::DestroyEditorEntities()
    {
      SafeDel(m_publishManager);

      // Editor objects.
      m_2dGrid = nullptr;
      m_grid   = nullptr;
      m_origin = nullptr;
      m_cursor = nullptr;

      if (m_dbgArrow)
      {
        GetCurrentScene()->RemoveEntity(m_dbgArrow->GetIdVal());
        m_dbgArrow = nullptr;
      }
      if (m_dbgFrustum)
      {
        GetCurrentScene()->RemoveEntity(m_dbgFrustum->GetIdVal());
        m_dbgFrustum = nullptr;
      }

      m_perFrameDebugObjects.clear();
    }

    void App::CreateNewScene()
    {
      String sceneName = "NewScene" + SCENE;
      String file      = CreateIncrementalFileFullPath(ScenePath(sceneName), " ");
      DecomposePath(file, nullptr, &sceneName, nullptr);

      EditorScenePtr scene = MakeNewPtr<EditorScene>();
      scene->SetFile(file);
      scene->m_name     = sceneName + SCENE;
      scene->m_newScene = true;
      SetCurrentScene(scene);
    }

    void App::ImplementMetaKeys()
    {
      if (ObjectFactory* objFactory = GetObjectFactory())
      {
        // Action to take when a class with given metakey is registered.
        auto registerMeta = [this](StringView metaKeyValue, StringArray& metaKeyValueArray)
        {
          if (std::find(metaKeyValueArray.begin(), metaKeyValueArray.end(), metaKeyValue) == metaKeyValueArray.end())
          {
            // Add key values and construct menus. Effectively add new classes to dynamic menus.
            metaKeyValueArray.push_back(String(metaKeyValue));
            ReconstructDynamicMenus();
          }
        };

        // Action to take when a class with given metakey is unregistered.
        auto unregisterMeta = [this](StringView metaKeyValue, StringArray& metaKeyValueArray)
        {
          for (int i = static_cast<int>(metaKeyValueArray.size()) - 1; i >= 0; --i)
          {
            if (metaKeyValueArray[i] == metaKeyValue)
            {
              // Remove key values and construct menus. Effectively drop registered class from dynamic menus.
              metaKeyValueArray.erase(metaKeyValueArray.begin() + i);
              ReconstructDynamicMenus();
            }
          }
        };

        struct MetaBinding
        {
          StringView metaKey;
          StringArray* metaKeyValueArray;
        };

        std::array<MetaBinding, 2> metaBindings = {
            {{EntityMenuMetaKey, &m_customObjectMetaValues}, {ComponentMenuMetaKey, &m_customComponentMetaValues}}
        };

        for (auto& binding : metaBindings)
        {
          objFactory->m_metaProcessorRegisterMap[binding.metaKey] =
              [this, listPtr = binding.metaKeyValueArray, registerMeta](StringView metaKeyVal)
          { registerMeta(metaKeyVal, *listPtr); };

          objFactory->m_metaProcessorUnRegisterMap[binding.metaKey] =
              [this, listPtr = binding.metaKeyValueArray, unregisterMeta](StringView metaKeyVal)
          { unregisterMeta(metaKeyVal, *listPtr); };
        }
      }
    }

    void App::Destroy()
    {
      // UI.
      DeleteWindows();

      DestroyEditorEntities();

      GetCurrentScene()->Destroy(false);

      GetAnimationPlayer()->Destroy();

      GetUIManager()->DestroyLayers();
      GetUIManager()->ClearViewportsToUpdateLayers();

      ModManager::GetInstance()->UnInit();
      ActionManager::GetInstance()->UnInit();

      GetLogger()->SetWriteConsoleFn(nullptr);
      GetLogger()->SetClearConsoleFn(nullptr);
    }

    void App::Frame(float deltaTime)
    {
      m_deltaTime = deltaTime;

      // Update Mods.
      ModManager::GetInstance()->Update(deltaTime);

      EditorViewportRawPtrArray viewports;
      for (WindowPtr wnd : m_windows)
      {
        if (EditorViewport* edView = wnd->As<EditorViewport>())
        {
          viewports.push_back(edView);
        }

        bool skipDispatch = false;
        if (m_gameMod == GameMod::Playing)
        {
          if (!m_simulatorSettings.Windowed)
          {
            if (wnd->m_name == g_3dViewport)
            {
              // Skip 3d viewport if game is playing on it.
              skipDispatch = true;
            }
          }
        }

        if (!skipDispatch)
        {
          wnd->DispatchSignals();
        }
      }

      bool playOnSimulationWnd = m_gameMod == GameMod::Playing && m_simulatorSettings.Windowed;

      if (playOnSimulationWnd)
      {
        viewports.push_back(m_simulationViewport.get());
      }

      // Update simulation status.
      UpdateSimulation();

      UI::BeginUI();
      UI::ShowUI();

      // Render Viewports.
      for (EditorViewport* viewport : viewports)
      {
        viewport->Update(deltaTime);

        if (viewport->IsShown())
        {
          GetRenderSystem()->AddRenderTask({[this, viewport, deltaTime](Renderer* renderer) -> void
                                            {
                                              viewport->m_editorRenderer->m_params.App      = g_app;
                                              viewport->m_editorRenderer->m_params.LitMode  = m_sceneLightingMode;
                                              viewport->m_editorRenderer->m_params.Viewport = viewport;
                                              viewport->m_editorRenderer->Render(renderer);
                                            }});
        }
      }

      GetRenderSystem()->AddRenderTask({[](Renderer* renderer) -> void
                                        {
                                          Stats::BeginGpuScope("EditorUI");
                                          renderer->SetFramebuffer(nullptr, GraphicBitFields::None);
                                          UI::EndUI(); // Render UI.
                                          Stats::EndGpuScope();
                                        }});

      m_totalFrameCount = GetRenderSystem()->GetFrameCount();
    }

    void App::OnResize(uint width, uint height)
    {
      RenderSystem* rsys = GetRenderSystem();
      rsys->SetAppWindowSize(width, height);
    }

    void App::OnNewScene(const String& name)
    {
      ClearSession();
      CreateNewScene();
    }

    void App::OnSaveScene()
    {
      // Prevent overriding default scene.
      EditorScenePtr currScene = GetCurrentScene();
      if (GetSceneManager()->GetDefaultResource(Scene::StaticClass()) == currScene->GetFile())
      {
        currScene->SetFile(ScenePath("New Scene" + SCENE));
        return OnSaveAsScene();
      }

      auto saveFn = [this]() -> void
      {
        // Serialize engine settings.
        g_app->m_workspace.SerializeEngineSettings();

        EditorScenePtr cScene = g_app->GetCurrentScene();
        cScene->Save(false);

        String rootFolder;
        String relPath = GetRelativeResourcePath(cScene->GetFile(), &rootFolder);
        String msg     = "Saved to: " + ConcatPaths({rootFolder, relPath});

        TK_LOG(msg.c_str());
        SetStatusMsg(g_statusSceneSaved);

        FolderWindowRawPtrArray folderWindows = g_app->GetAssetBrowsers();
        for (FolderWindow* folderWnd : folderWindows)
        {
          folderWnd->UpdateContent();
        }
      };

      // File existence check.
      String fullPath = currScene->GetFile();
      if (currScene->m_newScene && CheckFile(fullPath))
      {
        String msg                   = "Scene " + fullPath + " exist on the disk.\nOverride the existing scene ?";
        YesNoWindowPtr overrideScene = MakeNewPtr<YesNoWindow>("Override existing file##OvrdScn", msg);
        overrideScene->AddToUI();

        overrideScene->m_yesCallback = [&saveFn]() { saveFn(); };
        overrideScene->m_noCallback  = []()
        {
          g_app->GetConsole()->AddLog("Scene has not been saved.\n"
                                      "A scene with the same name exist. Use File->SaveAs.",
                                      LogType::Error);
        };
      }
      else
      {
        saveFn();
      }
    }

    void App::OnSaveAsScene()
    {
      StringInputWindowPtr inputWnd = MakeNewPtr<StringInputWindow>("SaveScene##SvScn1", true);
      inputWnd->m_inputLabel        = "Name";
      inputWnd->m_hint              = "Scene name";
      inputWnd->AddToUI();

      inputWnd->m_taskFn = [](const String& val)
      {
        String path;
        EditorScenePtr currScene = g_app->GetCurrentScene();
        DecomposePath(currScene->GetFile(), &path, nullptr, nullptr);

        String fullPath = NormalizePath(ConcatPaths({path, val + SCENE}));

        currScene->SetFile(fullPath);
        currScene->m_name = val;
        g_app->OnSaveScene();
      };
    }

    void App::OnQuit()
    {
      if (m_gameMod != GameMod::Stop)
      {
        SetGameMod(GameMod::Stop);
        return;
      }

      if (!m_onQuit)
      {
        YesNoWindowPtr reallyQuit = MakeNewPtr<YesNoWindow>("Quiting... Are you sure?##ClsApp");

        reallyQuit->m_yesCallback = [this]()
        {
          Serialize(nullptr, nullptr);
          g_running = false;
        };

        reallyQuit->m_noCallback = [this]() { m_onQuit = false; };
        reallyQuit->AddToUI();

        m_onQuit = true;
      }
    }

    void AlterTextContent(std::fstream& fileEditStream, const String& filePath, const String content)
    {
      fileEditStream.open(filePath, std::ios::out | std::ios::trunc);
      if (fileEditStream.is_open())
      {
        fileEditStream << content;
        fileEditStream.close();
      }
    }

    void TemplateUpdate(const String& file, const String& replaceSoruce, const String& replaceTarget)
    {
      std::fstream fileEditStream;
      fileEditStream.open(file, std::ios::in);
      if (fileEditStream.is_open())
      {
        std::stringstream buffer;
        buffer << fileEditStream.rdbuf();
        String content = buffer.str();
        ReplaceFirstStringInPlace(content, replaceSoruce.data(), replaceTarget);
        fileEditStream.close();

        AlterTextContent(fileEditStream, file, content);
      }
    }

    // note: only copy template folder
    void App::OnNewProject(const String& name)
    {
      if (m_workspace.GetActiveWorkspace().empty())
      {
        TK_ERR("No workspace. Project can't be created.");
        return;
      }

      String fullPath = ConcatPaths({m_workspace.GetActiveWorkspace(), name});
      if (CheckFile(fullPath))
      {
        TK_ERR("Project already exist.");
        return;
      }

      // copy template folder to new workspace
      RecursiveCopyDirectory(ConcatPaths({"..", "Templates", "Game"}),
                             fullPath,
                             {".filters", ".vcxproj", ".user", ".cxx"});

      // Update cmake.
      String currentPath = std::filesystem::current_path().parent_path().u8string();
      String cmakePath   = ConcatPaths({fullPath, "Codes", "CMakeLists.txt"});
      UnixifyPath(cmakePath);
      TemplateUpdate(cmakePath, "__projectname__", name);

      // update vscode includes.
      String cppPropertiesPath = ConcatPaths({fullPath, ".vscode", "c_cpp_properties.json"});
      UnixifyPath(cppPropertiesPath);

      String tkRoot = std::filesystem::absolute(currentPath).u8string();
      UnixifyPath(tkRoot);
      String tkPath = ConcatPaths({tkRoot, "ToolKit"});
      UnixifyPath(tkPath);
      String depPath = ConcatPaths({tkRoot, "Dependency"});
      UnixifyPath(depPath);
      String glmPath = ConcatPaths({tkRoot, "Dependency", "glm"});
      UnixifyPath(glmPath);
      String imguiPath = ConcatPaths({tkRoot, "Dependency", "tkimgui"});
      UnixifyPath(imguiPath);

      String replacement = "\"" + tkRoot + "\",\n" + "\t\t\t\t\"" + tkPath + "\",\n" + "\t\t\t\t\"" + depPath +
                           "\",\n" + "\t\t\t\t\"" + glmPath + "\",\n" + "\t\t\t\t\"" + imguiPath + "\"";

      TemplateUpdate(cppPropertiesPath, "__tk_includes__", replacement);

      OpenProject({name, ""});
    }

    void App::OnNewPlugin(const String& name)
    {
      if (m_workspace.GetActiveWorkspace().empty())
      {
        TK_ERR("No project. There must be an open project to create plugin for.");
        return;
      }

      String fullPath = ConcatPaths({m_workspace.GetPluginDirectory(), name});
      if (CheckSystemFile(fullPath))
      {
        TK_ERR("A plugin with the same name already exist in the project.");
        return;
      }

      // Copy template folder to new project.
      RecursiveCopyDirectory(ConcatPaths({"..", "Templates", "Plugin"}),
                             fullPath,
                             {".filters", ".vcxproj", ".user", ".cxx"});

      // Update cmake.
      String currentPath = std::filesystem::current_path().parent_path().u8string();
      String cmakePath   = ConcatPaths({fullPath, "Codes", "CMakeLists.txt"});
      UnixifyPath(cmakePath);
      TemplateUpdate(cmakePath, "__projectname__", name);

      String pluginSettingsPath = ConcatPaths({fullPath, "Config", "Plugin.settings"});
      UnixifyPath(pluginSettingsPath);
      TemplateUpdate(pluginSettingsPath, "PluginTemplate", name);

      SetStatusMsg(g_statusSucceeded);
      TK_LOG("A new plugin has been created.");

      if (PluginWindowPtr wnd = GetWindow<PluginWindow>(g_pluginWindow))
      {
        wnd->LoadPluginSettings();
      }
    }

    void App::SetGameMod(const GameMod mod)
    {
      if (mod == m_gameMod)
      {
        return;
      }

      GamePlugin* gamePlugin = GetPluginManager()->GetGamePlugin();
      if (gamePlugin == nullptr)
      {
        return;
      }

      if (mod == GameMod::Playing)
      {
        // Transitioning to play from stop.
        if (m_gameMod == GameMod::Stop)
        {
          m_sceneLightingMode  = EditorLitMode::Game;
          m_lastActiveViewport = GetActiveViewport();

          if (m_simulatorSettings.Windowed)
          {
            m_simulationViewport->SetVisibility(true);

            // Match views.
            if (EditorViewportPtr viewport3d = GetViewport(g_3dViewport))
            {
              Mat4 view = viewport3d->GetCamera()->m_node->GetTransform();
              m_simulationViewport->GetCamera()->m_node->SetTransform(view);
            }
          }

          // Register ui viewport
          if (ViewportPtr simViewport = GetSimulationViewport())
          {
            GetUIManager()->RegisterViewport(simViewport);
          }

          // Check if there is a new plugin.
          if (PluginManager* plugMan = GetPluginManager())
          {
            if (Plugin* plg = static_cast<Plugin*>(gamePlugin))
            {
              plg        = plugMan->Reload(plg);
              gamePlugin = static_cast<GamePlugin*>(plg);
            }
          }
        }

        gamePlugin->SetViewport(GetSimulationViewport());
        gamePlugin->m_currentState = PluginState::Running;

        if (m_gameMod == GameMod::Stop)
        {
          // First game plugin must be initialized.
          PluginManager* pm   = GetPluginManager();
          PluginRegister* reg = pm->GetRegister(gamePlugin);
          if (!reg->m_initialized)
          {
            gamePlugin->Init(Main::GetInstance());
            reg->m_initialized = true;
          }

          // Then call on play.
          gamePlugin->OnPlay();
          SetStatusMsg(g_statusGameIsPlaying);
        }

        if (m_gameMod == GameMod::Paused)
        {
          gamePlugin->OnResume();
          SetStatusMsg(g_statusGameIsResumed);
        }

        m_gameMod = mod;
      }

      if (mod == GameMod::Paused)
      {
        gamePlugin->m_currentState = PluginState::Paused;
        gamePlugin->OnPause();

        SetStatusMsg(g_statusGameIsPaused);
        m_gameMod = mod;
      }

      if (mod == GameMod::Stop)
      {
        gamePlugin->m_currentState = PluginState::Stop;
        gamePlugin->OnStop();

        SetStatusMsg(g_statusGameIsStopped);
        m_gameMod = mod;

        ClearPlayInEditorSession();

        m_simulationViewport->SetVisibility(false);
        m_sceneLightingMode = EditorLitMode::EditorLit;
      }
    }

    bool App::IsCompiling() { return m_isCompiling; }

    void App::CompilePlugin(const String& name)
    {
      String pluginDir               = g_app->m_workspace.GetPluginDirectory();
      m_publishManager->m_appName    = ConcatPaths({pluginDir, name, "Codes"});
      m_publishManager->m_pluginName = name;
      m_publishManager->Publish(PublishPlatform::EditorPlugin, TKDebug ? PublishConfig::Debug : PublishConfig::Deploy);
    }

    void App::LoadGamePlugin()
    {
      ClearSession();

      if (PluginManager* pluginMan = GetPluginManager())
      {
        String pluginPath = m_workspace.GetBinPath();
        pluginMan->Load(pluginPath);
      }
    }

    EditorScenePtr App::GetCurrentScene()
    {
      ScenePtr scene = GetSceneManager()->GetCurrentScene();
      return Cast<EditorScene>(scene);
    }

    void App::SetCurrentScene(const EditorScenePtr& scene) { GetSceneManager()->SetCurrentScene(scene); }

    void App::FocusEntity(EntityPtr entity)
    {
      CameraPtr cam = nullptr;
      if (EditorViewportPtr viewport = GetActiveViewport())
      {
        cam = viewport->GetCamera();
      }
      else if (EditorViewportPtr viewport = GetViewport(g_3dViewport))
      {
        cam = viewport->GetCamera();
      }
      else
      {
        SetStatusMsg(g_statusNo3dViewports);
        return;
      }

      if (!GetCurrentScene()->GetBillboard(entity))
      {
        cam->FocusToBoundingBox(entity->GetBoundingBox(true), 1.1f);
      }
      else
      {
        BoundingBox defaultBBox  = {Vec3(-1.0f), Vec3(1.0f)};
        Vec3 pos                 = entity->m_node->GetTranslation(TransformationSpace::TS_WORLD);
        defaultBBox.max         += pos;
        defaultBBox.min         += pos;
        cam->FocusToBoundingBox(defaultBBox, 1.1f);
      }
    }

    void App::ClearSession()
    {
      // Clear queued render tasks.
      GetRenderSystem()->FlushRenderTasks();
      GetRenderSystem()->FlushGpuPrograms();

      // Clear all the object references from the scene about to be destroyed.
      if (OutlinerWindowPtr wnd = GetOutliner())
      {
        wnd->ClearOutliner();
      }

      for (WindowPtr wnd : m_windows)
      {
        if (EditorViewport* edView = wnd->As<EditorViewport>())
        {
          edView->m_editorRenderer = MakeNewPtr<EditorRenderer>();
        }
      }

      // Clear all animations potentially added from game module.
      GetAnimationPlayer()->Destroy();
      GetUIManager()->DestroyLayers();
      GetUIManager()->ClearViewportsToUpdateLayers();

      m_perFrameDebugObjects.clear();
      GetWorkerManager()->Flush();

      ActionManager::GetInstance()->ClearAllActions();

      if (ModManager* modMan = ModManager::GetInstance())
      {
        modMan->UnInit();
        modMan->Init();
        modMan->SetMod(true, ModId::Select);
      }
    }

    void App::ClearPlayInEditorSession()
    {
      ClearSession();

      if (EditorSceneManager* sceneMan = static_cast<EditorSceneManager*>(GetSceneManager()))
      {
        // Reload to retrieve the original scene, clears the game play modifications.
        if (EditorScene* scene = sceneMan->GetCurrentScene()->As<EditorScene>())
        {
          if (!scene->m_newScene)
          {
            scene->UnInit();
            scene->Load();
            scene->Init();
          }
        }
      }

      // Set back the viewport camera
      EditorViewportPtr viewport = GetActiveViewport();
      if (viewport == nullptr)
      {
        viewport = GetViewport(g_3dViewport);
      }

      if (viewport != nullptr)
      {
        viewport->AttachCamera(NullHandle);
      }
    }

    int App::ExecSysCommand(StringView cmd, bool async, bool showConsole, SysCommandDoneCallback callback)
    {
      if (m_sysComExecFn)
      {
        return m_sysComExecFn(cmd, async, showConsole, callback);
      }

      return -1;
    }

    void App::ResetUI()
    {
      DeleteWindows();

      String defaultEditorSettings = ConcatPaths({ConfigPath(), g_editorSettingsFile});
      if (CheckFile(defaultEditorSettings) && CheckFile(m_workspace.GetActiveWorkspace()))
      {
        // Try reading defaults.
        SerializationFileInfo serializeInfo;
        serializeInfo.File = defaultEditorSettings;

        // Prevent loading last scene.
        Project project    = m_workspace.GetActiveProject();
        m_workspace.SetScene("");

        DeSerialize(serializeInfo, nullptr);
        m_workspace.SetScene(project.scene);

        String settingsFile = ConcatPaths({ConfigPath(), g_uiLayoutFile});
        ImGui::LoadIniSettingsFromDisk(settingsFile.c_str());
      }
      else
      {
        // 3d viewport.
        float w              = (float) GetEngineSettings().m_window->GetWidthVal();
        float h              = (float) GetEngineSettings().m_window->GetHeightVal();
        Vec2 vpSize          = Vec2(w, h) * 0.8f;
        EditorViewportPtr vp = MakeNewPtr<EditorViewport>();
        vp->Init(vpSize);
        vp->m_name = g_3dViewport;
        vp->GetCamera()->m_node->SetTranslation({5.0f, 3.0f, 5.0f});
        vp->GetCamera()->GetComponent<DirectionComponent>()->LookAt(Vec3(0.0f));
        m_windows.push_back(vp);

        // 2d viewport.
        vp = MakeNewPtr<EditorViewport2d>();
        vp->Init(vpSize);
        vp->m_name = g_2dViewport;
        vp->GetCamera()->m_node->SetTranslation(Z_AXIS);
        m_windows.push_back(vp);

        // Isometric viewport.
        vp = MakeNewPtr<EditorViewport>();
        vp->Init(vpSize);
        vp->m_name = g_IsoViewport;
        vp->GetCamera()->m_node->SetTranslation({0.0f, 10.0f, 0.0f});
        vp->GetCamera()->SetLens(-10.0f, 10.0f, -10.0f, 10.0f, 0.01f, 1000.0f);
        vp->GetCamera()->SetOrthographicScaleVal(0.02f);
        vp->GetCamera()->GetComponent<DirectionComponent>()->Pitch(glm::radians(-90.0f));
        vp->m_cameraAlignment = CameraAlignment::Top;
        vp->m_orbitLock       = true;
        m_windows.push_back(vp);

        ConsoleWindowPtr console = MakeNewPtr<ConsoleWindow>();
        m_windows.push_back(console);

        FolderWindowPtr assetBrowser = MakeNewPtr<FolderWindow>();
        assetBrowser->IterateFolders(true);
        assetBrowser->m_name = g_assetBrowserStr;
        m_windows.push_back(assetBrowser);

        OutlinerWindowPtr outliner = MakeNewPtr<OutlinerWindow>();
        outliner->m_name           = g_outlinerStr;
        m_windows.push_back(outliner);

        PropInspectorWindowPtr inspector = MakeNewPtr<PropInspectorWindow>();
        inspector->m_name                = g_propInspector;
        m_windows.push_back(inspector);

        m_windows.push_back(MakeNewPtr<SimulationWindow>());

        CreateSimulationViewport();
      }
    }

    void App::DeleteWindows()
    {
      GetRenderSystem()->FlushRenderTasks();
      m_windows.clear();

      for (size_t i = 0; i < EditorViewport::m_overlays.size(); i++)
      {
        SafeDel(EditorViewport::m_overlays[i]);
      }

      m_simulationViewport = nullptr;

      UI::m_volatileWindows.clear();
    }

    void App::ReconstructDynamicMenus()
    {
      m_customObjectsMenu.clear();
      ConstructDynamicMenu(m_customObjectMetaValues, m_customObjectsMenu);

      m_customComponentsMenu.clear();
      ConstructDynamicMenu(m_customComponentMetaValues, m_customComponentsMenu);
    }

    int App::Import(const String& fullPath, const String& subDir, bool overwrite)
    {
      bool doSearch = !UI::SearchFileData.missingFiles.empty();
      if (!CanImport(fullPath) && !doSearch)
      {
        if (ConsoleWindowPtr console = GetConsole())
        {
          console->AddLog("Import failed: " + fullPath, LogType::Error);
          console->AddLog("File format is not supported.\n"
                          "Suported formats are fbx, glb, gltf, obj.",
                          LogType::Error);
        }
        return -1;
      }

      bool importFileExist          = CheckFile(fullPath);

      // Set the execute path.
      std::filesystem::path pathBck = std::filesystem::current_path();
      std::filesystem::path path    = pathBck.u8string() + ConcatPaths({"", "..", "Utils", "Import"});
      std::filesystem::current_path(path);

      std::filesystem::path cpyDir = ".";
      if (!subDir.empty())
      {
        cpyDir += GetPathSeparator() + subDir;
      }

      // Try reimport after search paths provided.
      bool reImport = doSearch || UI::SearchFileData.showSearchFileWindow;
      if (importFileExist || reImport)
      {
        int result = -1; // execution result.
        if (!doSearch)
        {
          String name, ext;
          DecomposePath(fullPath, nullptr, &name, &ext);
          String finalPath = fullPath;

          if (name == "importList" && ext == ".txt")
          {
            finalPath = "importList.txt";
          }

          String cmd = "Import \"";
          if (!subDir.empty())
          {
            cmd += finalPath + "\" -t \"" + subDir;
          }
          else
          {
            cmd += finalPath;
          }

          cmd    += "\" -s " + std::to_string(UI::ImportData.Scale);
          cmd    += " -o " + std::to_string(UI::ImportData.optimize);

          // Execute command
          result  = ExecSysCommand(cmd.c_str(), false, false);
          if (result != 0)
          {
            TK_ERR("Import failed!");
          }
        }

        // Move assets.
        String meshFile;
        if (result != -1 || doSearch)
        {
          std::ifstream copyList("out.txt");
          if (copyList.is_open())
          {
            // Check files.
            StringArray missingFiles;
            for (String line; std::getline(copyList, line);)
            {
              if (!CheckFile(line))
              {
                missingFiles.push_back(line);
              }
            }

            if (!missingFiles.empty())
            {
              if (g_app->m_importSlient)
              {
                g_app->GetConsole()->AddLog("Import: " + fullPath + " failed.", LogType::Error);
                goto Fail;
              }

              // Try search.
              size_t numFound = 0;
              for (String& searchPath : UI::SearchFileData.searchPaths)
              {
                for (String& missingFile : missingFiles)
                {
                  String name, ext;
                  DecomposePath(missingFile, nullptr, &name, &ext);
                  String missingFullPath = ConcatPaths({searchPath, name + ext});
                  if (CheckFile(missingFullPath))
                  {
                    numFound++;
                    std::filesystem::copy(missingFullPath, cpyDir, std::filesystem::copy_options::overwrite_existing);
                  }
                }
              }

              if (numFound < missingFiles.size())
              {
                // Retry.
                UI::SearchFileData.missingFiles = missingFiles;
                goto Retry;
              }
              else
              {
                UI::SearchFileData.missingFiles.clear();
              }
            }

            copyList.clear();
            copyList.seekg(0, std::ios::beg);
            for (String line; std::getline(copyList, line);)
            {
              String ext;
              DecomposePath(line, nullptr, nullptr, &ext);
              const String selfDir = '.' + GetPathSeparatorAsStr();
              if (line.rfind(selfDir) == 0)
              {
                line = line.substr(2, -1);
              }

              String fullPath;
              if (ext == SCENE)
              {
                fullPath = PrefabPath(line);
              }

              if (ext == MESH || ext == SKINMESH)
              {
                fullPath = MeshPath(line);
                meshFile = fullPath;
              }

              if (ext == SKELETON)
              {
                fullPath = SkeletonPath(line);
              }

              if (ext == ANIM)
              {
                fullPath = AnimationPath(line);
              }

              if (SupportedImageFormat(ext))
              {
                fullPath = TexturePath(line);
              }

              if (ext == MATERIAL)
              {
                fullPath = MaterialPath(line);
              }

              String path, name;
              DecomposePath(fullPath, &path, &name, &ext);

              std::error_code fileOpErr;
              std::filesystem::create_directories(path, fileOpErr);
              if (fileOpErr)
              {
                TK_ERR("Folder creation failed: %s", fileOpErr.message().c_str());
              }
              else
              {
                std::filesystem::copy(line, fullPath, std::filesystem::copy_options::overwrite_existing, fileOpErr);
                if (fileOpErr)
                {
                  TK_ERR("File copy failed: %s", fileOpErr.message().c_str());
                }
              }
            }
          }
        }
        else
        {
          goto Fail;
        }

        std::filesystem::current_path(pathBck);
        if (!meshFile.empty())
        {
          String ext;
          DecomposePath(meshFile, nullptr, nullptr, &ext);
          MeshPtr mesh;
          if (ext == SKINMESH)
          {
            mesh = GetMeshManager()->Create<SkinMesh>(meshFile);
          }
          else
          {
            mesh = GetMeshManager()->Create<Mesh>(meshFile);
          }

          FolderWindowRawPtrArray folderWindows = g_app->GetAssetBrowsers();
          for (FolderWindow* folderWnd : folderWindows)
          {
            folderWnd->UpdateContent();
          }
        }

        UI::SearchFileData.showSearchFileWindow = false;
        return 0;
      }
      else
      {
        goto Fail;
      }

    Retry:
      UI::SearchFileData.showSearchFileWindow = true;

    Fail:
      std::filesystem::current_path(pathBck);
      return -1;
    }

    bool App::CanImport(const String& fullPath)
    {
      String ext;
      DecomposePath(fullPath, nullptr, nullptr, &ext);

      if (SupportedMeshFormat(ext))
      {
        return true;
      }

      if (ext == ".txt")
      {
        // Hopefully, list of valid objects. Not a poem.
        return true;
      }

      if (SupportedImageFormat(ext))
      {
        return true;
      }

      return false;
    }

    void App::ManageDropfile(const StringView& fileName)
    {
      TKAsyncTask(WorkerManager::MainThread,
                  [this, fileName]() -> void
                  {
                    const FolderWindowRawPtrArray& assetBrowsers = g_app->GetAssetBrowsers();

                    String log = "File isn't imported because it's not dropped into Asset Browser";

                    for (FolderWindow* folderWindow : assetBrowsers)
                    {
                      if (folderWindow->MouseHovers())
                      {
                        FolderView* activeView = folderWindow->GetActiveView();
                        if (activeView == nullptr)
                        {
                          log = "Activate a resource folder by selecting it from the Asset Browser.";
                        }
                        else
                        {
                          UI::ImportData.ActiveView = activeView;
                          UI::ImportData.Files.push_back(fileName.data());
                          UI::ImportData.ShowImportWindow = true;
                        }
                      }
                    }

                    if (!UI::ImportData.ShowImportWindow)
                    {
                      SetStatusMsg(g_statusDropDiscarded);
                      TK_WRN(log.c_str());
                    }
                  });
    }

    void App::OpenSceneAsync(const String& fullPath)
    {
      // Start progress in loading bar.
      SetStatusMsg(g_statusLoading + ": 0.00%%");
      auto progressReportFn = [this](float progress) -> void
      { SetStatusMsg(g_statusLoading + ": " + Format("%.2f", progress) + "%%"); };

      TKAsyncTask(WorkerManager::BackgroundPool,
                  [this, fullPath, progressReportFn]() -> void
                  {
                    // Load scene in background.
                    EditorScenePtr scene = GetSceneManager()->Create<EditorScene>(fullPath, progressReportFn);
                    SetStatusMsg(g_statusComplate);

                    // Initiate and set the scene in the main thread.
                    TKAsyncTask(WorkerManager::MainThread,
                                [this, fullPath]() -> void
                                {
                                  ClearSession();
                                  GetCurrentScene()->Destroy(false);
                                  GetSceneManager()->Remove(GetCurrentScene()->GetFile());

                                  // Get the loaded scene.
                                  EditorScenePtr scene = GetSceneManager()->Create<EditorScene>(fullPath);
                                  if (IsLayer(fullPath))
                                  {
                                    if (EditorViewport2dPtr viewport = GetWindow<EditorViewport2d>(g_2dViewport))
                                    {
                                      UILayerPtr layer = MakeNewPtr<UILayer>(scene);

                                      UIManager* uiMan = GetUIManager();
                                      uiMan->AddLayer(viewport->m_viewportId, layer);
                                    }
                                    else
                                    {
                                      SetStatusMsg(g_statusNo2dViewports);
                                    }
                                  }

                                  SetCurrentScene(scene);
                                  scene->Init();
                                  m_workspace.SetScene(scene->m_name);
                                });
                  });
    }

    void App::MergeScene(const String& fullPath)
    {
      EditorScenePtr scene = GetSceneManager()->Create<EditorScene>(fullPath);
      scene->Load();
      scene->Init();
      GetCurrentScene()->Merge(scene);
    }

    void App::LinkScene(const String& fullPath) { GetSceneManager()->GetCurrentScene()->LinkPrefab(fullPath); }

    void App::ApplyProjectSettings(bool setDefaults)
    {
      if (CheckFile(ConcatPaths({m_workspace.GetConfigDirectory(), g_editorSettingsFile})) && !setDefaults)
      {
        DeSerialize(SerializationFileInfo(), nullptr);
        m_workspace.DeSerializeEngineSettings();
        UI::InitSettings();
      }
      else
      {
        ResetUI();
      }

      // Clear window before restoring the window.
      RenderSystem* rsys = GetRenderSystem();
      rsys->AddRenderTask({[](Renderer* renderer) -> void
                           {
                             renderer->SetFramebuffer(nullptr, GraphicBitFields::AllBits);
                             SDL_GL_SwapWindow(g_window);
                           }});

      rsys->FlushRenderTasks();

      // Restore app window.
      UVec2 size = GetRenderSystem()->GetAppWindowSize();

      // Resize window.
      SDL_SetWindowSize(g_window, size.x, size.y);
      SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

      if (m_windowMaximized)
      {
        SDL_MaximizeWindow(g_window);
      }
    }

    void App::OpenProject(const Project& project)
    {
      ClearSession();
      GetPluginManager()->UnloadGamePlugin();
      m_workspace.SetActiveProject(project);
      m_workspace.Serialize(nullptr, nullptr);
      m_workspace.SerializeEngineSettings();
      CreateNewScene();

      LoadGamePlugin();

      FolderWindowRawPtrArray browsers = GetAssetBrowsers();
      for (FolderWindow* browser : browsers)
      {
        browser->IterateFolders(true);
      }
    }

    void App::PackResources() { m_publishManager->Pack(); }

    void App::SaveAllResources()
    {
      ClassMeta* types[] = {Material::StaticClass(),
                            Mesh::StaticClass(),
                            SkinMesh::StaticClass(),
                            Animation::StaticClass()};

      for (ClassMeta* t : types)
      {
        for (auto& resource : GetResourceManager(t)->m_storage)
        {
          if (!resource.second->IsDynamic())
          {
            String file = resource.second->GetFile();
            if (!IsDefaultResource(file))
            {
              resource.second->m_dirty = true;
              resource.second->Save(true);
            }
          }
        }
      }
    }

    WindowPtr App::GetActiveWindow()
    {
      for (WindowPtr wnd : m_windows)
      {
        if (wnd->IsActive() && wnd->IsVisible())
        {
          return wnd;
        }
      }

      return nullptr;
    }

    EditorViewportPtr App::GetActiveViewport()
    {
      for (WindowPtr wnd : m_windows)
      {
        if (!wnd->IsA<EditorViewport>())
        {
          continue;
        }

        if (wnd->IsActive() && wnd->IsVisible())
        {
          return Cast<EditorViewport>(wnd);
        }
      }

      return m_lastActiveViewport;
    }

    EditorViewportPtr App::GetViewport(const String& name)
    {
      for (WindowPtr wnd : m_windows)
      {
        if (wnd->IsA<EditorViewport>())
        {
          if (wnd->m_name == name)
          {
            return Cast<EditorViewport>(wnd);
          }
        }
      }

      return nullptr;
    }

    ConsoleWindowPtr App::GetConsole()
    {
      for (WindowPtr wnd : m_windows)
      {
        if (wnd->IsA<ConsoleWindow>())
        {
          return Cast<ConsoleWindow>(wnd);
        }
      }

      return nullptr;
    }

    FolderWindowRawPtrArray App::GetAssetBrowsers() { return GetAllWindows<FolderWindow>(g_assetBrowserStr); }

    OutlinerWindowPtr App::GetOutliner() { return GetWindow<OutlinerWindow>(g_outlinerStr); }

    PropInspectorWindowPtr App::GetPropInspector() { return GetWindow<PropInspectorWindow>(g_propInspector); }

    RenderSettingsWindowPtr App::GetRenderSettingsWindow() { return GetWindow<RenderSettingsWindow>(g_renderSettings); }

    StatsWindowPtr App::GetStatsWindow() { return GetWindow<StatsWindow>(g_statsView); }

    PluginWindowPtr App::GetPluginWindow() { return GetWindow<PluginWindow>(g_pluginWindow); }

    void App::ReInitViewports()
    {
      EditorViewportRawPtrArray viewports;
      for (WindowPtr wnd : m_windows)
      {
        if (EditorViewport* edView = wnd->As<EditorViewport>())
        {
          edView->ReInitViewport();
        }
      }
    }

    void App::HideGizmos()
    {
      const EntityPtrArray& entities = GetCurrentScene()->GetEntities();
      for (EntityPtr ntt : entities)
      {
        // Light and camera gizmos
        if (ntt->IsA<Light>() || ntt->IsA<Camera>())
        {
          ntt->SetVisibility(false, false);
        }
      }
    }

    void App::ShowGizmos()
    {
      const EntityPtrArray& entities = GetCurrentScene()->GetEntities();
      for (EntityPtr ntt : entities)
      {
        // Light and camera gizmos
        if (ntt->IsA<Light>() || ntt->IsA<Camera>())
        {
          ntt->SetVisibility(true, false);
        }
      }
    }

    void App::SetStatusMsg(const String& msg)
    {
      LockGuard lock(m_statusMsgMutex);
      m_statusMsg = msg;
    }

    String App::GetStatusMsg()
    {
      LockGuard lock(m_statusMsgMutex);
      return m_statusMsg;
    }

    EditorViewportPtr App::GetSimulationViewport()
    {
      if (m_simulatorSettings.Windowed)
      {
        return m_simulationViewport;
      }

      EditorViewportPtr simWnd = GetViewport(g_3dViewport);
      assert(simWnd != nullptr && "3D Viewport must exist.");

      return simWnd;
    }

    void App::UpdateSimulation()
    {
      if (GamePlugin* plugin = GetPluginManager()->GetGamePlugin())
      {
        if (plugin->m_currentState == PluginState::Stop)
        {
          SetGameMod(GameMod::Stop);
        }

        if (m_gameMod != GameMod::Stop)
        {
          m_simulationViewport->SetVisibility(m_simulatorSettings.Windowed);
        }
      }
    }

    XmlNode* App::SerializeImp(XmlDocument* doc, XmlNode* parent) const
    {
      m_workspace.Serialize(nullptr, nullptr);

      std::ofstream file;
      String cfgPath              = m_workspace.GetConfigDirectory();
      String fileName             = ConcatPaths({cfgPath, g_editorSettingsFile});

      // File or Config folder is missing.
      std::ios::openmode openMode = std::ios::out;
      if (!CheckSystemFile(fileName))
      {
        std::filesystem::create_directories(cfgPath);
        openMode = std::ios::app;
      }

      file.open(fileName.c_str(), openMode);
      if (file.is_open())
      {
        XmlDocumentPtr lclDoc = MakeNewPtr<XmlDocument>();
        XmlDocument* docPtr   = lclDoc.get();

        XmlNode* app          = CreateXmlNode(docPtr, "App");
        WriteAttr(app, docPtr, "version", TKVersionStr);

        XmlNode* settings = CreateXmlNode(docPtr, "Settings", app);
        XmlNode* setNode  = CreateXmlNode(docPtr, "Size", settings);

        UVec2 size        = GetRenderSystem()->GetAppWindowSize();
        WriteAttr(setNode, docPtr, "width", std::to_string(size.x));
        WriteAttr(setNode, docPtr, "height", std::to_string(size.y));
        WriteAttr(setNode, docPtr, "maximized", std::to_string(m_windowMaximized));

        XmlNode* windowsNode = CreateXmlNode(docPtr, "Windows", app);
        for (WindowPtr wnd : m_windows)
        {
          wnd->Serialize(docPtr, windowsNode);
        }

        std::string xml;
        rapidxml::print(std::back_inserter(xml), *lclDoc, 0);

        file << xml;
        file.close();
        lclDoc->clear();
      }

      return nullptr;
    }

    XmlNode* App::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
    {
      String settingsFile = info.File;
      if (settingsFile.empty())
      {
        settingsFile = ConcatPaths({m_workspace.GetConfigDirectory(), g_editorSettingsFile});
      }

      if (!CheckFile(settingsFile))
      {
        settingsFile = ConcatPaths({ConfigPath(), g_editorSettingsFile});
        assert(CheckFile(settingsFile) && "ToolKit/Config/Editor.settings must exist.");
      }

      XmlFilePtr lclFile    = MakeNewPtr<XmlFile>(settingsFile.c_str());
      XmlDocumentPtr lclDoc = MakeNewPtr<XmlDocument>();
      lclDoc->parse<0>(lclFile->data());
      XmlDocument* doc = lclDoc.get();

      if (XmlNode* root = doc->first_node("App"))
      {
        ReadAttr(root, "version", m_version, TKV044);

        if (XmlNode* settings = root->first_node("Settings"))
        {
          if (XmlNode* setNode = settings->first_node("Size"))
          {
            uint width = 0;
            ReadAttr(setNode, "width", width);

            uint height = 0;
            ReadAttr(setNode, "height", height);
            ReadAttr(setNode, "maximized", m_windowMaximized);

            if (width > 0 && height > 0)
            {
              OnResize(width, height);
            }
          }
        }

        DeserializeWindows(root);
      }

      Project activeProject = m_workspace.GetActiveProject();
      if (!activeProject.name.empty())
      {
        LoadGamePlugin();

        if (!activeProject.scene.empty())
        {
          String fullPath = ScenePath(activeProject.scene);
          OpenSceneAsync(fullPath);
        }
      }

      return nullptr;
    }

    void App::DeserializeWindows(XmlNode* parent)
    {
      if (XmlNode* windowsNode = parent->first_node("Windows"))
      {
        const char* xmlRootObject = Object::StaticClass()->Name.c_str();
        const char* xmlObjectType = XmlObjectClassAttr.data();
        ObjectFactory* factory    = GetObjectFactory();

        for (XmlNode* node = windowsNode->first_node(xmlRootObject); node; node = node->next_sibling(xmlRootObject))
        {
          XmlAttribute* typeAttr = node->first_attribute(xmlObjectType);
          if (WindowPtr wnd = MakeNewPtrCasted<Window>(typeAttr->value()))
          {
            wnd->m_version = m_version;
            wnd->DeSerialize(SerializationFileInfo(), node);
            m_windows.push_back(wnd);
          }
        }
      }

      CreateSimulationViewport();
    }

    void App::CreateSimulationViewport()
    {
      m_simulationViewport = CreateOrRetrieveWindow<EditorViewport>(g_simulationViewStr);
      m_simulationViewport->Init({m_simulatorSettings.Width, m_simulatorSettings.Height});

      m_simulationViewport->m_additionalWindowFlags =
          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;

      m_simulationViewport->SetVisibility(false);
    }

    void App::AssignManagerReporters()
    {
      // Register manager reporters
      auto genericReporterFn = [](LogType logType, String msg) -> void
      {
        if (ConsoleWindowPtr console = g_app->GetConsole())
        {
          console->AddLog(msg, logType);
        }
      };

      auto genericClearFn = []() -> void { g_app->GetConsole()->ClearLog(); };

      GetLogger()->SetWriteConsoleFn(genericReporterFn);
      GetLogger()->SetClearConsoleFn(genericClearFn);
    }

    void App::CreateAndSetNewScene(const String& name)
    {
      EditorScenePtr scene = MakeNewPtr<EditorScene>();
      scene->SetFile(ScenePath(name + SCENE));

      scene->m_name     = name;
      scene->m_newScene = true;
      GetSceneManager()->Manage(scene);
      SetCurrentScene(scene);
    }

    void App::CreateEditorEntities()
    {
      // Create editor objects.
      m_cursor   = MakeNewPtr<Cursor>();
      m_origin   = MakeNewPtr<Axis3d>();
      m_dbgArrow = MakeNewPtr<Arrow2d>();
      m_dbgArrow->Generate(AxisLabel::X);

      m_grid = MakeNewPtr<Grid>();
      m_grid->Resize(g_max2dGridSize, AxisLabel::ZX, 0.020f, 3.0);

      m_2dGrid         = MakeNewPtr<Grid>();
      m_2dGrid->m_is2d = true;
      m_2dGrid->Resize(g_max2dGridSize, AxisLabel::XY, 10.0f, 4.0);
    }

    float App::GetDeltaTime() { return m_deltaTime; }

  } // namespace Editor
} // namespace ToolKit
