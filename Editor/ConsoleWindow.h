/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "Window.h"

namespace ToolKit
{
  namespace Editor
  {

    typedef std::pair<String, StringArray> TagArg;
    typedef std::vector<TagArg> TagArgArray;
    typedef TagArgArray::const_iterator TagArgCIt;
    TagArgCIt GetTag(const String& tag, const TagArgArray& tagArgs);
    void ParseVec(Vec3& vec, TagArgCIt tagIt);

    // Commands & Executors.
    const String g_showPickDebugCmd("ShowPickGeometry");
    TK_EDITOR_API void ShowPickDebugExec(TagArgArray tagArgs);

    const String g_showOverlayUICmd("ShowOverlayUI");
    TK_EDITOR_API void ShowOverlayExec(TagArgArray tagArgs);

    const String g_showOverlayUIAlwaysCmd("ShowOverlayUIAlways");
    TK_EDITOR_API void ShowOverlayAlwaysExec(TagArgArray tagArgs);

    const String g_showModTransitionsCmd("ShowModTransitions");
    TK_EDITOR_API void ShowModTransitionsExec(TagArgArray tagArgs);

    const String g_setTransformCmd("SetTransform");
    TK_EDITOR_API void SetTransformExec(TagArgArray tagArgs);

    const String g_transformCmd("Transform");
    TK_EDITOR_API void TransformExec(TagArgArray tagArgs);

    const String g_setCameraTransformCmd("SetCameraTransform");
    TK_EDITOR_API void SetCameraTransformExec(TagArgArray tagArgs);

    const String g_resetCameraCmd("ResetCamera");
    TK_EDITOR_API void ResetCameraExec(TagArgArray tagArgs);

    const String g_getTransformCmd("GetTransform");
    TK_EDITOR_API void GetTransformExec(TagArgArray tagArgs);

    const String g_setTransformOrientationCmd("SetTransformOrientation");
    TK_EDITOR_API void SetTransformOrientationExec(TagArgArray tagArgs);

    const String g_importSlientCmd("ImportSlient");
    TK_EDITOR_API void ImportSlient(TagArgArray tagArgs);

    const String g_selectByTag("SelectByTag");
    TK_EDITOR_API void SelectByTag(TagArgArray tagArgs);

    const String g_lookAt("LookAt");
    TK_EDITOR_API void LookAt(TagArgArray tagArgs);

    const String g_applyTransformToMesh("ApplyTransformToMesh");
    TK_EDITOR_API void ApplyTransformToMesh(TagArgArray tagArgs);

    const String g_saveMesh("SaveMesh");
    TK_EDITOR_API void SaveMesh(TagArgArray tagArgs);

    const String g_showSelectionBoundary("ShowSelectionBoundary");
    TK_EDITOR_API void ShowSelectionBoundary(TagArgArray tagArgs);

    const String g_showGraphicsApiLogs("ShowGraphicsApiLogs");
    TK_EDITOR_API void ShowGraphicsApiLogs(TagArgArray tagArgs);

    const String g_setWorkspaceDir("SetWorkspaceDir");
    TK_EDITOR_API void SetWorkspaceDir(TagArgArray tagArgs);

    const String g_loadPlugin("LoadPlugin");
    TK_EDITOR_API void LoadPlugin(TagArgArray tagArgs);

    const String g_showShadowFrustum("ShowShadowFrustum");
    TK_EDITOR_API void ShowShadowFrustum(TagArgArray tagArgs);

    const String g_selectEffectingLights("SelectAllEffectingLights");
    TK_EDITOR_API void SelectAllEffectingLights(TagArgArray tagArgs);

    const String g_checkSceneHealth("CheckSceneHealth");
    TK_EDITOR_API void CheckSceneHealth(TagArgArray tagArgs);

    const String g_showSceneBoundary("ShowSceneBoundary");
    TK_EDITOR_API void ShowSceneBoundary(TagArgArray tagArgs);

    const String g_showBVHNodes("ShowBVHNodes");
    TK_EDITOR_API void ShowBVHNodes(TagArgArray tagArgs);

    const String g_deleteSelection("DeleteSelection");
    TK_EDITOR_API void DeleteSelection(TagArgArray tagArgs);

    const String g_showProfileTimer("ShowProfileTimer");
    TK_EDITOR_API void ShowProfileTimer(TagArgArray tagArgs);

    const String g_selectSimilar("SelectSimilar");
    TK_EDITOR_API void SelectSimilar(TagArgArray tagArgs);

    // Command errors
    const String g_noValidEntity("No valid entity");

    // ConsoleWindow
    //////////////////////////////////////////

    class TK_EDITOR_API ConsoleWindow : public Window
    {
     public:
      TKDeclareClass(ConsoleWindow, Window);

      ConsoleWindow();
      virtual ~ConsoleWindow();
      void Show() override;

      void AddLog(const String& log, LogType type = LogType::Memo);
      void AddLog(const String& log, const String& tag);
      void ClearLog();
      void ExecCommand(const String& commandLine);
      void ParseCommandLine(const String& commandLine, String& command, TagArgArray& tagArgs);

     private:
      // Command line word processing. Auto-complete and history lookups.
      int TextEditCallback(ImGuiInputTextCallbackData* data);
      void CreateCommand(const String& command, std::function<void(TagArgArray)> executor);

     private:
      // States.
      bool m_scrollToBottom = false;

      // Buffers.
      StringArray m_items;
      std::mutex m_itemMutex;
      StringArray m_commands;
      std::unordered_map<String, std::function<void(TagArgArray&)>> m_commandExecutors;

      // Command text
      String m_command    = "";
      String m_filter     = "";
      bool m_reclaimFocus = false;

      StringArray m_history;
      // -1: new line, 0..History.Size-1 browsing history.
      int m_historyPos = -1;
    };

  } // namespace Editor
} // namespace ToolKit
