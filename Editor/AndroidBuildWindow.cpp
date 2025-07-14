/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "AndroidBuildWindow.h"

#include "App.h"
#include "FolderWindow.h"
#include "PublishManager.h"

namespace ToolKit::Editor
{
  TKDefineClass(AndroidBuildWindow, Window);

  AndroidBuildWindow::AndroidBuildWindow() { m_name = "Android Build"; }

  void AndroidBuildWindow::Show()
  {
    // Enable the close button and auto-resize flag
    bool isOpen = true;
    if (!ImGui::Begin(m_name.c_str(), &isOpen, ImGuiWindowFlags_AlwaysAutoResize))
    {
      ImGui::End();
      return;
    }

    if (!isOpen)
    {
      RemoveFromUI(); // Handle close button
      ImGui::End();
      return;
    }

    ImGui::InputText("Name", &m_appName);

    ImGui::Text("Icon");
    ImGui::SameLine();

    int iconId = m_icon ? m_icon->m_textureId : m_defaultIcon->m_textureId;
    ImGui::ImageButton("##icon", ConvertUIntImGuiTexture(iconId), ImVec2(64, 64));

    if (ImGui::BeginDragDropTarget())
    {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("BrowserDragZone"))
      {
        const FileDragData& dragData = FolderView::GetFileDragData();
        DirectoryEntry& entry        = *dragData.Entries[0]; // get first entry
        m_icon                       = GetTextureManager()->Create<Texture>(entry.GetFullPath());
        m_icon->Init(false);
      }

      ImGui::EndDragDropTarget();
    }

    ImGui::InputInt("Min SDK", &m_minSdk);
    ImGui::InputInt("Max SDK", &m_maxSdk);

    ImGui::Text("Select Orientation:");

    const char* orientations[] = {"Automatic", "Landscape", "Portrait"};
    ImGui::Combo("##OrientationCombo", (int*) &m_selectedOriantation, orientations, 3);

    ImGui::Text("Select Android ABI:");
    const char* abiOptions[] = {"All", "armeabi-v7a", "arm64-v8a", "x86", "x86_64"};
    ImGui::Combo("##ABICombo", (int*) &m_selectedABI, abiOptions, IM_ARRAYSIZE(abiOptions));

    ImGui::Checkbox("Deploy After Build", &m_deployAfterBuild);
    UI::HelpMarker(TKLoc,
                   "When build finish if this check is true "
                   "ToolKit will try to run the application on your android device.",
                   2.0f);

    if (ImGui::Button("Cancel"))
    {
      RemoveFromUI();
    }

    ImGui::SameLine();

    if (ImGui::Button("Build"))
    {
      PublishManager* publisher     = GetApp()->m_publishManager;
      publisher->m_minSdk           = m_minSdk;
      publisher->m_maxSdk           = m_maxSdk;
      publisher->m_appName          = m_appName;
      publisher->m_icon             = m_icon;
      publisher->m_oriantation      = (MobileOriantation) m_selectedOriantation;
      publisher->m_deployAfterBuild = m_deployAfterBuild;
      publisher->m_selectedABI      = m_selectedABI; // Pass the selected ABI

      GetApp()->m_publishManager->Publish(PublishPlatform::Android, m_publishType);
      RemoveFromUI();
    }

    ImGui::End();
  }

  void AndroidBuildWindow::OpenBuildWindow(PublishConfig publishType)
  {
    if (m_appName.empty())
    {
      m_appName = GetApp()->m_workspace.GetActiveProject().name;
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