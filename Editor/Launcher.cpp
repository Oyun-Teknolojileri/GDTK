/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Launcher.h"
#include "UI.h"
#include "PopupWindows.h"

#define IMGUI_USER_CONFIG "tk_imconfig.h"
#include <imgui/imgui.h>

extern bool g_running; // Defined in main.cpp
extern bool g_launcherRunning; // Defined in main.cpp

namespace ToolKit
{
  namespace Editor
  {
    Launcher::Launcher(int windowWidth, int windowHeight)
        : m_windowWidth(windowWidth), m_windowHeight(windowHeight)
    {
    }

    Launcher::~Launcher()
    {
      if (m_workspace)
      {
        SafeDel(m_workspace);
      }
    }

    void Launcher::ShowLauncherWindow()
    {
      UI::BeginUI();

      const float cardSize   = 150.0f;
      const float cardSpacing = 20.0f;
      const int itemsPerRow   = 4;

      // Center the window
      ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImVec2 center           = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                              viewport->Pos.y + viewport->Size.y * 0.5f);
      ImVec2 windowSize       = ImVec2(800.0f, 600.0f);
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

      ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;


      ImGui::Begin("Engine Launcher", nullptr, windowFlags);

      // Close button in top right corner
      float closeButtonSize = 30.0f;
      float closeButtonPadding = 10.0f;
      ImVec2 closeButtonPos(windowSize.x - closeButtonSize - closeButtonPadding, closeButtonPadding);
      
      ImGui::SetCursorPos(closeButtonPos);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.0f, 0.0f, 0.5f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.0f, 0.0f, 0.7f));
      
      if (ImGui::Button("X", ImVec2(closeButtonSize, closeButtonSize)))
      {
        g_running = false; // Close the application
      }
      ImGui::PopStyleColor(3);

      // Title
      ImGui::SetCursorPosY(closeButtonPadding + 5.0f);
      ImGui::SetCursorPosX((windowSize.x - ImGui::CalcTextSize("ToolKit Engine Launcher").x) * 0.5f);
      ImGui::Text("ToolKit Engine Launcher");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      HandleWorkspace();

      // Project cards grid
      float startX = (windowSize.x - (itemsPerRow * cardSize + (itemsPerRow - 1) * cardSpacing)) * 0.5f;
      ImGui::SetCursorPosX(startX);

      if (m_workspace)
      {
        const size_t numProjects = m_workspace->m_projects.size();
        for (size_t i = 0; i < numProjects; ++i)
        {
          const Project& project = m_workspace->m_projects[i];
          if (i > 0 && i % itemsPerRow == 0)
          {
            ImGui::SetCursorPosX(startX);
          }

          ImGui::BeginGroup();

          // Card square/button
          ImVec2 cardPos = ImGui::GetCursorScreenPos();
          ImVec2 cardButtonSize(cardSize, cardSize);

          ImGui::PushID((int)i);
          if (ImGui::Button("", cardButtonSize))
          {
            m_workspace->SetActiveProject(project);
            g_launcherRunning = false; // Close launcher to open project
          }
          ImGui::PopID();

          // Card background/hover effect
          if (ImGui::IsItemHovered())
          {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(cardPos,
                                    ImVec2(cardPos.x + cardSize, cardPos.y + cardSize),
                                    ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                    4.0f);
          }

          // Text below the card
          ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cardSize - ImGui::CalcTextSize(project.name.c_str()).x) * 0.5f);
          ImGui::Text("%s", project.name.c_str());

          ImGui::EndGroup();

          // Spacing between cards
          if ((i + 1) % itemsPerRow != 0)
          {
            ImGui::SameLine(0.0f, cardSpacing);
          }
          else
          {
            ImGui::Spacing();
            ImGui::Spacing();
          }
        }
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Bottom buttons
      float buttonWidth = 120.0f;
      float buttonSpacing = 20.0f;
      float totalButtonWidth = buttonWidth * 2 + buttonSpacing;
      float buttonStartX = (windowSize.x - totalButtonWidth) * 0.5f;

      ImGui::SetCursorPosX(buttonStartX);
      if (ImGui::Button("New Project", ImVec2(buttonWidth, 0)))
      {
        // TODO New project action
      }

      ImGui::End();

      UI::EndUI();
    }

    void Launcher::HandleWorkspace()
    {
      if (!m_workspace)
      {
        m_workspace = new Workspace();
        m_workspace->Init();

        if (CheckFile(m_workspace->GetActiveWorkspace()))
        {
          m_workspace->RefreshProjects();
        }

        m_workspacePathOnUI = m_workspace->GetDefaultWorkspace();
      }

      // Workspace path input UI
      ImGui::Text("Workspace Path:");
      ImGui::SameLine();
      ImGui::PushItemWidth(400.0f);
      ImGui::InputText("##workspacePath", &m_workspacePathOnUI);
      ImGui::PopItemWidth();
      ImGui::SameLine();
      if (ImGui::Button("Enter", ImVec2(80.0f, 0.0f)))
      {
        m_workspace->SetDefaultWorkspace(m_workspacePathOnUI);
      }
    }
  } // namespace Editor
} // namespace ToolKit
