/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 * Author: erendgrmnc
 */

#include "MacOSBuildWindow.h"

#include "App.h"
#include "FolderWindow.h"
#include "PublishManager.h"

namespace ToolKit::Editor
{
  TKDefineClass(MacOSBuildWindow, Window);

  MacOSBuildWindow::MacOSBuildWindow() { m_name = "macOS Build"; }

  void MacOSBuildWindow::Show()
  {
    bool isOpen = true;
    if (!ImGui::Begin(m_name.c_str(), &isOpen, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::End();
      return;
    }

    if (!isOpen)
    {
      RemoveFromUI();
      ImGui::End();
      return;
    }

    ImGui::InputText("App Name", &m_appName);
    ImGui::InputText("Bundle Identifier", &m_bundleIdentifier);
    UI::HelpMarker(TKLoc, "Bundle identifier in reverse domain format (e.g., com.company.appname)", 2.0f);

    ImGui::Text("Icon");
    ImGui::SameLine();

    int iconId = m_icon ? m_icon->m_textureId : m_defaultIcon->m_textureId;
    ImGui::ImageButton("##icon", ConvertUIntImGuiTexture(iconId), ImVec2(64, 64));

    if (ImGui::BeginDragDropTarget())
    {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BrowserDragZone"))
      {
        const FileDragData& dragData = FolderView::GetFileDragData();
        DirectoryEntry& entry        = *dragData.Entries[0];
        m_icon                       = GetTextureManager()->Create<Texture>(entry.GetFullPath());
        m_icon->Init(false);
      }

      ImGui::EndDragDropTarget();
    }

    ImGui::Text("Minimum macOS Version:");
    const char* macOSVersions[] = {"10.15", "11.0", "12.0", "13.0", "14.0"};
    int currentVersionIdx       = 1; // Default to 11.0
    for (int i = 0; i < 5; i++)
    {
      if (m_minMacOSVersion == macOSVersions[i])
      {
        currentVersionIdx = i;
        break;
      }
    }

    if (ImGui::Combo("##MacOSVersion", &currentVersionIdx, macOSVersions, 5))
    {
      m_minMacOSVersion = macOSVersions[currentVersionIdx];
    }

    ImGui::Checkbox("Deploy After Build", &m_deployAfterBuild);
    UI::HelpMarker(TKLoc, "When build finishes, automatically open the .app bundle", 2.0f);

    if (ImGui::Button("Cancel"))
    {
      RemoveFromUI();
    }

    ImGui::SameLine();

    if (ImGui::Button("Build"))
    {
      PublishManager* publisher      = GetApp()->m_publishManager;
      publisher->m_appName           = m_appName;
      publisher->m_icon              = m_icon;
      publisher->m_bundleIdentifier  = m_bundleIdentifier;
      publisher->m_minMacOSVersion   = m_minMacOSVersion;
      publisher->m_deployAfterBuild  = m_deployAfterBuild;

      GetApp()->m_publishManager->Publish(PublishPlatform::MacOS, m_publishType);
      RemoveFromUI();
    }

    ImGui::End();
  }

  void MacOSBuildWindow::OpenBuildWindow(PublishConfig publishType)
  {
    if (m_appName.empty())
    {
      m_appName = GetApp()->m_workspace.GetActiveProject().name;
    }

    if (m_bundleIdentifier == "com.otsoftware.game")
    {
      String sanitizedName = m_appName;
      std::transform(sanitizedName.begin(), sanitizedName.end(), sanitizedName.begin(), ::tolower);
      m_bundleIdentifier = "com.otsoftware." + sanitizedName;
    }

    if (m_defaultIcon == nullptr)
    {
      m_defaultIcon = GetTextureManager()->Create<Texture>(TexturePath(ConcatPaths({"ToolKit", "Icons", "app.png"})));
      m_defaultIcon->Init(false);
    }

    m_publishType = publishType;

    AddToUI();
  }
} // namespace ToolKit::Editor
