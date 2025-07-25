/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include <Types.h>

extern struct SDL_Window* g_window;
extern void* g_context;

#ifdef TK_WIN
  #if defined(TK_EDITOR_DLL_EXPORT)
    #define TK_EDITOR_API __declspec(dllexport)
  #else
    #define TK_EDITOR_API __declspec(dllimport)
  #endif
#endif

namespace ToolKit
{
  namespace Editor
  {

    typedef std::shared_ptr<class AndroidBuildWindow> AndroidBuildWindowPtr;
    typedef std::shared_ptr<class PreviewViewport> PreviewViewportPtr;
    typedef std::shared_ptr<class EditorViewport> EditorViewportPtr;
    typedef std::vector<EditorViewportPtr> EditorViewportPtrArray;
    typedef std::vector<class EditorViewport*> EditorViewportRawPtrArray;
    typedef std::shared_ptr<class MaterialView> MaterialViewPtr;
    typedef std::shared_ptr<class MaterialWindow> MaterialWindowPtr;
    typedef std::shared_ptr<class ConsoleWindow> ConsoleWindowPtr;
    typedef std::shared_ptr<class EditorBillboardBase> EditorBillboardPtr;
    typedef std::vector<EditorBillboardPtr> BillboardPtrArray;
    typedef std::vector<class EditorBillboardBase*> BillboardRawPtrArray;
    typedef std::shared_ptr<class SkyBillboard> SkyBillboardPtr;
    typedef std::shared_ptr<class LightBillboard> LightBillboardPtr;
    typedef std::shared_ptr<class EditorRenderer> EditorRendererPtr;
    typedef std::shared_ptr<class FolderWindow> FolderWindowPtr;
    typedef std::vector<class FolderWindow*> FolderWindowRawPtrArray;
    typedef std::vector<class View*> ViewRawPtrArray;
    typedef std::shared_ptr<class EditorScene> EditorScenePtr;
    typedef std::shared_ptr<class ThreePointLightSystem> ThreePointLightSystemPtr;
    typedef std::shared_ptr<class EditorDirectionalLight> EditorDirectionalLightPtr;
    typedef std::shared_ptr<class EditorPointLight> EditorPointLightPtr;
    typedef std::shared_ptr<class EditorSpotLight> EditorSpotLightPtr;
    typedef std::shared_ptr<class AnchorHandle> AnchorHandlePtr;
    typedef std::shared_ptr<class Anchor> AnchorPtr;
    typedef std::shared_ptr<class EditorCamera> EditorCameraPtr;
    typedef std::shared_ptr<class Gizmo> GizmoPtr;
    typedef std::shared_ptr<class Axis3d> Axis3dPtr;
    typedef std::shared_ptr<class Cursor> CursorPtr;
    typedef std::shared_ptr<class GridFragmentShader> GridFragmentShaderPtr;
    typedef std::shared_ptr<class Grid> GridPtr;
    typedef std::vector<class Action*> ActionRawPtrArray;
    typedef std::shared_ptr<class GizmoPass> GizmoPassPtr;
    typedef std::shared_ptr<class PropInspectorWindow> PropInspectorWindowPtr;
    typedef std::shared_ptr<class RenderSettingsWindow> RenderSettingsWindowPtr;
    typedef std::shared_ptr<class SimulationWindow> SimulationWindowPtr;
    typedef std::shared_ptr<class StatsWindow> StatsWindowPtr;
    typedef std::shared_ptr<class StatusBar> StatusBarPtr;
    typedef std::shared_ptr<class OutlinerWindow> OutlinerWindowPtr;
    typedef std::shared_ptr<class PluginSettingsWindow> PluginSettingsWindowPtr;
    typedef std::shared_ptr<class PluginWindow> PluginWindowPtr;
    typedef std::shared_ptr<class MultiChoiceCraeteWindow> MultiChoiceCraeteWindowPtr;
    typedef std::shared_ptr<class StatusBar> StatusBarPtr;
    typedef std::shared_ptr<struct DynamicMenu> DynamicMenuPtr;
    typedef std::vector<DynamicMenuPtr> DynamicMenuPtrArray;
    typedef std::shared_ptr<class Window> WindowPtr;
    typedef std::vector<WindowPtr> WindowPtrArray;
    typedef std::shared_ptr<class EditorCanvas> EditorCanvasPtr;

    typedef std::function<void(int)> SysCommandDoneCallback;
    typedef std::function<int(StringView, bool, bool, SysCommandDoneCallback)> SysCommandExecutionFn;
    typedef std::function<void(const StringView)> ShellOpenDirFn;

    // UI Strings
    const String g_consoleStr("Console");
    const String g_assetBrowserStr("Asset Browser");
    const String g_outlinerStr("Outliner");
    const String g_propInspector("Property Inspector");
    const String g_renderSettings("Render Settings");
    const String g_statsView("Statistics");
    const String g_matInspector("Material Inspector");
    const String g_simulationWindowStr("Simulation");
    const String g_pluginWindow("Plugins");
    const String g_memoStr("Mem");
    const String g_errorStr("Err");
    const String g_warningStr("Wrn");
    const String g_successStr("Suc");
    const String g_commandStr("#");
    const String g_newSceneStr("New Scene");
    const String g_viewportStr("Viewport");
    const String g_2dViewport("2D Viewport");
    const String g_3dViewport("3D Viewport");
    const String g_IsoViewport("ISO Viewport");
    const String g_simulationViewStr("Simulation Viewport");
    const String g_workspaceFile("Workspace.settings");
    const String g_uiLayoutFile("UILayout.ini");
    const String g_editorSettingsFile("Editor.settings");
    const String g_statusNoTerminate("#nte"); //!< Keep the status message with progress indicator.
    const String g_statusBar("Status Bar");
    static const StringView XmlNodePath("path");

    // Status messages.
    const String g_statusOk("OK");
    const String g_statusSceneSaved("Scene saved");
    const String g_statusGameIsPlaying("Game is playing");
    const String g_statusGameIsResumed("Game is resumed");
    const String g_statusGameIsPaused("Game is paused");
    const String g_statusGameIsStopped("Game is stopped");
    const String g_statusCompiling("Compiling");
    const String g_statusCompileFailed("Compile failed");
    const String g_statusCmakeBuildFailed("CMake build failed");
    const String g_statusCmakeGenerateFailed("CMake generate failed");
    const String g_statusCompiled("Compiled");
    const String g_statusNo3dViewports("No 3D viewports !");
    const String g_statusNo2dViewports("No 2D viewports !");
    const String g_statusDropDiscarded("Drop discarded");
    const String g_statusLoading("Loading");
    const String g_statusComplate("Complate");
    const String g_statusRemoved("Removed");
    const String g_statusFailed("Failed");
    const String g_statusSucceeded("Succeeded");
    const String g_statusMaterialComponentAdded("MaterialComponent added.");
    const String g_statusMaxZoom("Max zoom");
    const String g_statusMinZoom("Min zoom");
    const String g_statusEntitiesCopied("Entities copied");
    const String g_statusInvalidCharater("Invalid character");
    const String g_statusPublishing("Publishing");
    const String g_statusPacking("Packing");

    // Colors and materials.
    // Reversed gamma correction for all colors
    const String g_gridMaterialName("TK_EDITOR_GRID");
    const Vec3 g_gridAxisBlue                  = Vec3(0.196f, 0.541f, 0.905f);
    const Vec3 g_gridAxisRed                   = Vec3(0.89f, 0.239f, 0.341f);
    const Vec3 g_gridAxisGreen                 = Vec3(0.537f, 0.831f, 0.07f);
    const Vec3 g_gizmoRed                      = Vec3(0.89f, 0.239f, 0.341f);
    const Vec3 g_gizmoGreen                    = Vec3(0.537f, 0.831f, 0.07f);
    const Vec3 g_gizmoBlue                     = Vec3(0.196f, 0.541f, 0.905f);
    const Vec3 g_cameraGizmoColor              = Vec3(0.0f);
    const Vec3 g_lightGizmoColor               = Vec3(0.0f);
    const Vec3 g_environmentGizmoColor         = Vec3(0.0f);
    const Vec3 g_gizmoLocked                   = Vec3(0.3f);
    const std::vector<Vec3> g_gizmoColor       = {g_gizmoRed, g_gizmoGreen, g_gizmoBlue};
    const Vec4 g_wndBgColor                    = Vec4(0.007024517f, 0.00959683f, 0.018735119f, 1.0f);

    const Vec4 g_selectBoxWindowColor          = Vec4(0.4f, 0.4f, 0.4f, 0.4f);
    const Vec4 g_selectBoxBorderColor          = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    const Vec4 g_selectHighLightPrimaryColor   = Vec4(1.0f, 0.627f, 0.156f, 1.0f);
    const Vec4 g_selectHighLightSecondaryColor = Vec4(0.898f, 0.352f, 0.031f, 1.0f);

    const Vec3 g_anchorColor                   = Vec3(1.f);
    const Vec4 g_anchorGuideLineColor          = Vec4(0.032276204f, 0.681419299f, 0.681419299f, 0.456263458f);

    const Vec4 g_consoleErrorColor             = Vec4(1.0f, 0.4f, 0.4f, 1.0f);
    const Vec4 g_consoleCommandColor           = Vec4(1.0f, 0.8f, 0.6f, 1.0f);
    const Vec4 g_consoleWarningColor           = Vec4(0.9f, 0.8f, 0.1f, 1.0f);
    const Vec4 g_consoleSuccessColor           = Vec4(0.4f, 0.94f, 0.4f, 1.0f);
    const Vec4 g_consoleMemoColor              = Vec4(0.7f, 0.7f, 0.7f, 1.0f);

    const Vec4 g_blueTintButtonColor           = Vec4(0.043f, 0.173f, 0.325f, 1.0f);
    const Vec4 g_blueTintButtonHoverColor      = Vec4(0.032f, 0.208f, 0.456f, 1.0f);
    const Vec4 g_blueTintButtonActiveColor     = Vec4(0.018f, 0.247f, 0.612f, 1.0f);

    const Vec4 g_greenTintButtonColor          = Vec4(0.0949f, 0.325f, 0.044f, 1.0f);
    const Vec4 g_greenTintButtonHoverColor     = Vec4(0.099f, 0.456f, 0.032f, 1.0f);
    const Vec4 g_greenTintButtonActiveColor    = Vec4(0.095f, 0.612f, 0.018f, 1.0f);

    const Vec4 g_redTintButtonColor            = Vec4(0.325f, 0.043f, 0.043f, 1.0f);
    const Vec4 g_redTintButtonHoverColor       = Vec4(0.456f, 0.032f, 0.032f, 1.0f);
    const Vec4 g_redTintButtonActiveColor      = Vec4(0.612f, 0.018f, 0.018f, 1.0f);

    // Editor settings.
    const size_t g_maxUndoCount                = 50;
    const UVec2 g_max2dGridSize(100 * 100 * 2);

#define Convert2ImGuiTexture(TexturePtr) (void*) (intptr_t) (TexturePtr->m_textureId) // NOLINT

#define ConvertUIntImGuiTexture(uint)    (void*) (intptr_t) (uint)

  } // namespace Editor
} // namespace ToolKit
