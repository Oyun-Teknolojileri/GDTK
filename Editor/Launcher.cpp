/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Launcher.h"
#include "UI.h"
#include "PopupWindows.h"
#include "App.h"
#include "EditorTypes.h"

#include <Util.h>
#include <Texture.h>

#define IMGUI_USER_CONFIG "tk_imconfig.h"
#include <imgui/imgui.h>

extern bool g_running; // Defined in main.cpp
extern bool g_launcherRunning; // Defined in main.cpp

namespace ToolKit
{
  namespace Editor
  {
    Launcher::Launcher(int windowWidth, int windowHeight, App* app)
        : m_windowWidth(windowWidth), m_windowHeight(windowHeight), m_app(app)
    {
      // Load logo texture
      if (m_logoTexture == nullptr)
      {
        m_logoTexture = GetTextureManager()->Create<Texture>(TexturePath(m_logoPath.c_str(), true));
        if (m_logoTexture)
        {
          m_logoTexture->Init();
        }
      }
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

      const float padding     = 1.0f;
      const float listItemHeight = 80.0f; // Height of each project list item

      // Center the window - larger and more professional
      ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImVec2 center           = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                              viewport->Pos.y + viewport->Size.y * 0.5f);
      ImVec2 windowSize       = ImVec2(1000.0f, 700.0f);
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

      // Modern window styling
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 5.0f));

      ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoScrollbar;

      ImGui::Begin("Engine Launcher", nullptr, windowFlags);

      // Header section - using editor theme colors
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      ImVec2 windowPos = ImGui::GetWindowPos();
      ImVec2 headerSize(windowSize.x, 50.0f);
      ImU32 headerColor = ImGui::GetColorU32(ImGuiCol_TitleBg);
      drawList->AddRectFilled(windowPos, 
                              ImVec2(windowPos.x + headerSize.x, windowPos.y + headerSize.y),
                              headerColor);

      // Close button in top right corner - modern style
      float closeButtonSize = 28.0f;
      float closeButtonPadding = 5.0f;
      ImVec2 closeButtonPos(windowSize.x - closeButtonSize - closeButtonPadding, closeButtonPadding);
      
      ImGui::SetCursorPos(closeButtonPos);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
      
      if (ImGui::Button("x", ImVec2(closeButtonSize, closeButtonSize)))
      {
        g_running = false;
      }
      ImGui::PopStyleColor(3);

      // Logo and Title - side by side
      float logoSize = 32.0f;
      float logoPadding = 15.0f;
      ImVec2 logoPos(logoPadding, (headerSize.y - logoSize) * 0.5f);
      
      // Draw logo from texture
      ImGui::SetCursorPos(logoPos);
      if (m_logoTexture && m_logoTexture->m_textureId != 0)
      {
        ImGui::Image(Convert2ImGuiTexture(m_logoTexture), ImVec2(logoSize, logoSize));
      }
      else
      {
        // Fallback placeholder if texture not loaded
        ImVec2 logoScreenPos = ImVec2(windowPos.x + logoPos.x, windowPos.y + logoPos.y);
        ImU32 logoBgColor = ImGui::GetColorU32(ImGuiCol_FrameBg);
        drawList->AddRectFilled(logoScreenPos, 
                                ImVec2(logoScreenPos.x + logoSize, logoScreenPos.y + logoSize),
                                logoBgColor, 4.0f);
        ImVec2 logoTextPos = ImVec2(logoScreenPos.x + logoSize * 0.5f - 8.0f, 
                                     logoScreenPos.y + logoSize * 0.5f - 6.0f);
        ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
        drawList->AddText(logoTextPos, textColor, "TK");
      }

      // Title next to logo - using editor theme colors
      ImGui::SetCursorPos(ImVec2(logoPos.x + logoSize + 10.0f, (headerSize.y - ImGui::GetTextLineHeight()) * 0.5f));
      ImGui::PushFont(nullptr);
      ImGui::Text("ToolKit Launcher");
      ImGui::PopFont();
      
      ImGui::SetCursorPosY(headerSize.y);
      ImGui::Separator();
      ImGui::Spacing();

      float panelPadding = 15.0f;
      float panelSpacing = 10.0f;
      float panelWidth = windowSize.x - panelPadding * 2;
      
      // Panel 2: Projects List (moved to top)
      ImGui::SetCursorPosX(panelPadding);
      
      // Projects title - small header on the left
      ImGui::Text("Projects");
      ImGui::Spacing();
      
      // Calculate height: window height - header - title - bottom panel height - padding - spacing
      float bottomPanelHeight = 40.0f;
      float bottomPadding = 20.0f;
      float spacingBetweenPanels = 10.0f; // Space between projects list and workspace panel
      float titleHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
      float listHeight = windowSize.y - headerSize.y - titleHeight - bottomPanelHeight - bottomPadding - panelPadding - spacingBetweenPanels;
      ImGui::SetCursorPosX(panelPadding); // Ensure proper left padding
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_FrameBg));
      ImGui::BeginChild("ProjectsListPanel", ImVec2(panelWidth, listHeight), true);
      ImGui::PopStyleColor();
      
      if (m_workspace && !m_workspace->m_projects.empty())
      {
        // Inner scrollable grid - scrollbar only when needed
        ImGui::BeginChild("ProjectsList", ImVec2(-1, -1), false);
        
        const float cardSize = 120.0f;
        const float cardSpacing = 15.0f;
        const float gridPadding = 15.0f; // Padding from edges
        const int itemsPerRow = 6;
        
        // Add padding at the start
        ImGui::SetCursorPos(ImVec2(gridPadding, gridPadding));
        
        const size_t numProjects = m_workspace->m_projects.size();
        
        // Calculate grid layout (account for padding)
        float availableWidth = ImGui::GetContentRegionAvail().x - gridPadding * 2;
        int actualItemsPerRow = (int)((availableWidth + cardSpacing) / (cardSize + cardSpacing));
        if (actualItemsPerRow < 1) actualItemsPerRow = 1;
        if (actualItemsPerRow > itemsPerRow) actualItemsPerRow = itemsPerRow;
        
        for (size_t i = 0; i < numProjects; ++i)
        {
          const Project& project = m_workspace->m_projects[i];
          
          ImGui::PushID((int)i);
          
          // Calculate position in grid
          int row = (int)(i / actualItemsPerRow);
          int col = (int)(i % actualItemsPerRow);
          
          if (col == 0 && i > 0)
          {
            ImGui::NewLine();
          }
          
          // Card item - square style
          ImVec2 cardSizeVec(cardSize, cardSize);
          
          // Use InvisibleButton for clickable area
          if (ImGui::InvisibleButton("##projectCard", cardSizeVec))
          {
            m_workspace->SetActiveProject(project);
            g_launcherRunning = false;
          }
          
          // Check if item is hovered for visual feedback
          bool isHovered = ImGui::IsItemHovered();
          bool isActive = ImGui::IsItemActive();
          
          // Get item position
          ImVec2 itemPos = ImGui::GetItemRectMin();
          ImVec2 itemMax = ImGui::GetItemRectMax();
          
          // Get draw list for this window
          ImDrawList* childDrawList = ImGui::GetWindowDrawList();
          
          // Get panel background color (same as panel)
          ImVec4 panelBgColor = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
          ImU32 bgColor = ImGui::GetColorU32(panelBgColor);
          
          // Very subtle hover effect - slightly lighter
          if (isHovered)
          {
            ImVec4 hoverColor = panelBgColor;
            hoverColor.x = (hoverColor.x + 0.05f > 1.0f) ? 1.0f : (hoverColor.x + 0.05f);
            hoverColor.y = (hoverColor.y + 0.05f > 1.0f) ? 1.0f : (hoverColor.y + 0.05f);
            hoverColor.z = (hoverColor.z + 0.05f > 1.0f) ? 1.0f : (hoverColor.z + 0.05f);
            bgColor = ImGui::GetColorU32(hoverColor);
          }
          
          // Draw background
          childDrawList->AddRectFilled(itemPos, itemMax, bgColor, 4.0f);
          
          // Project icon in center-top - Toolkit PNG
          float iconSize = 64.0f;
          float iconPadding = (cardSize - iconSize) * 0.5f;
          ImVec2 iconPos = ImVec2(itemPos.x + iconPadding, itemPos.y + 10.0f);
          
          // Draw Toolkit logo texture
          if (m_logoTexture && m_logoTexture->m_textureId != 0)
          {
            childDrawList->AddImageRounded(Convert2ImGuiTexture(m_logoTexture),
                                          iconPos,
                                          ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                          ImVec2(0, 0), ImVec2(1, 1),
                                          ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 4.0f);
          }
          else
          {
            // Fallback placeholder
            ImU32 iconBgColor = ImGui::GetColorU32(ImGuiCol_Button);
            childDrawList->AddRectFilled(iconPos, 
                                        ImVec2(iconPos.x + iconSize, iconPos.y + iconSize),
                                        iconBgColor, 4.0f);
          }
          
          // Project name below icon - centered
          float textY = itemPos.y + iconSize + 20.0f;
          const char* projectName = project.name.c_str();
          float textWidth = ImGui::CalcTextSize(projectName).x;
          ImVec2 textPos = ImVec2(itemPos.x + (cardSize - textWidth) * 0.5f, textY);
          childDrawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), projectName);
          
          ImGui::SameLine(0.0f, cardSpacing);
          
          ImGui::PopID();
        }
        
        ImGui::EndChild();
      }
      else if (m_workspace)
      {
        // No projects message
        ImGui::SetCursorPosX((panelWidth - ImGui::CalcTextSize("No projects found. Create a new project to get started.").x) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::Text("No projects found. Create a new project to get started.");
        ImGui::PopStyleColor();
      }
      
      ImGui::EndChild();
      
      // Bottom row: Workspace Panel (left) + New Project Button (right, panelsiz)
      // bottomPanelHeight, bottomPadding, and spacingBetweenPanels already defined above
      ImGui::SetCursorPosY(windowSize.y - bottomPanelHeight - bottomPadding);
      
      // Workspace Panel - bottom left, 1 row
      ImGui::SetCursorPosX(panelPadding);
      float workspacePanelWidth = panelWidth * 0.5f;
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg));
      ImGui::BeginChild("WorkspacePanel", ImVec2(workspacePanelWidth, bottomPanelHeight), true);
      ImGui::PopStyleColor();
      
      // Center the workspace content vertically
      float contentHeight = ImGui::GetTextLineHeight();
      float verticalOffset = (bottomPanelHeight - contentHeight) * 0.5f;
      ImGui::SetCursorPosY(verticalOffset);
      
      HandleWorkspace();
      
      ImGui::EndChild();
      
      // New Project Button - panelsiz, right side (aligned to right with padding)
      if (m_workspace && m_app->IsWorkspaceSane(false, false))
      {
        float buttonWidth = 150.0f;
        float buttonHeight = 35.0f;
        
        // Position button at right side with padding
        ImGui::SetCursorPosY(windowSize.y - buttonHeight - bottomPadding);
        float buttonX = windowSize.x - buttonWidth - panelPadding; // Right aligned with panel padding
        ImGui::SetCursorPosX(buttonX);
        
        // Use editor theme colors instead of custom blue
        if (ImGui::Button("+ New Project", ImVec2(buttonWidth, buttonHeight)))
        {
          m_showNewProjectPopup = true;
        }
      }

      // Show popups before ending main window
      ShowWorkspacePopup();
      ShowNewProjectPopup();

      // Pop style vars
      ImGui::PopStyleVar(4);

      ImGui::End();

      UI::EndUI();
    }

    void Launcher::HandleWorkspace()
    {
      // Initialize workspace if needed
      if (!m_workspace)
      {
        m_workspace = new Workspace();
        m_workspace->Init();
        m_app->m_workspace = m_workspace;
      }

      String currentWorkspace = m_workspace->GetActiveWorkspace();
      bool hasWorkspace = !currentWorkspace.empty() && CheckSystemFile(currentWorkspace);

      // Workspace UI - Text left aligned, button right aligned
      float panelWidth = ImGui::GetWindowWidth();
      float textPadding = 10.0f; // Padding for text from left
      float buttonPadding = 10.0f; // Padding for button from right
      
      if (!hasWorkspace)
      {
        // No workspace set - show button to set workspace
        float buttonWidth = 150.0f;
        
        // Text left aligned with padding
        ImGui::SetCursorPosX(textPadding);
        float textY = ImGui::GetCursorPosY();
        ImGui::Text("Workspace:");
        
        // Button right aligned on same line with padding
        ImGui::SetCursorPos(ImVec2(panelWidth - buttonWidth - buttonPadding, textY));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.6f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.4f, 0.7f, 1.0f));
        
        if (ImGui::Button("Set Workspace", ImVec2(buttonWidth, 0.0f)))
        {
          m_showWorkspacePopup = true;
          m_workspacePathOnUI = m_workspace->GetDefaultWorkspace();
        }
        ImGui::PopStyleColor(3);
      }
      else
      {
        // Workspace exists - show path and edit button
        float editButtonWidth = 80.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        
        // Calculate available space for path text
        float textLabelWidth = ImGui::CalcTextSize("Workspace:").x;
        float availableWidth = panelWidth - textLabelWidth - spacing - editButtonWidth - textPadding - buttonPadding - spacing;
        
        // Truncate path if too long
        String displayPath = currentWorkspace;
        float pathWidth = ImGui::CalcTextSize(displayPath.c_str()).x;
        
        if (pathWidth > availableWidth)
        {
          // Truncate with ellipsis
          displayPath = currentWorkspace;
          while (pathWidth > availableWidth && displayPath.length() > 0)
          {
            displayPath.pop_back();
            String testPath = displayPath + "...";
            pathWidth = ImGui::CalcTextSize(testPath.c_str()).x;
          }
          displayPath += "...";
        }
        
        // Text left aligned with ellipsis if needed, with padding
        ImGui::SetCursorPosX(textPadding);
        float textY = ImGui::GetCursorPosY();
        ImGui::Text("Workspace:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        ImGui::Text("%s", displayPath.c_str());
        ImGui::PopStyleColor();
        
        // Button right aligned on same line with padding
        ImGui::SetCursorPos(ImVec2(panelWidth - editButtonWidth - buttonPadding, textY - 5.0f));
        if (ImGui::Button("Edit", ImVec2(editButtonWidth, 0.0f)))
        {
          m_showWorkspacePopup = true;
          m_workspacePathOnUI = currentWorkspace;
        }
      }
    }

    void Launcher::ShowWorkspacePopup()
    {
      if (!m_showWorkspacePopup)
        return;

      ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                            viewport->Pos.y + viewport->Size.y * 0.5f);
 
      // Calculate content size - use fixed values for stability
      float padding = 40.0f; // Window padding
      float textHeight = 20.0f; // Fixed text height
      float inputHeight = 25.0f; // Fixed input height
      float buttonHeight = 25.0f; // Fixed button height
      float spacing = 8.0f; // Fixed spacing
      
      float minWidth = 500.0f;
      float separatorHeight = 2.0f; // Fixed separator height
      float minHeight = padding * 2 + textHeight + spacing + inputHeight + spacing + 
                        separatorHeight + spacing + buttonHeight;
      
      // Maximum size
      ImVec2 maxSize(viewport->Size.x * 0.9f, viewport->Size.y * 0.6f);
      
      // Use fixed height, dynamic width
      ImVec2 popupSize(
        (minWidth < maxSize.x) ? minWidth : maxSize.x,
        (minHeight < maxSize.y) ? minHeight : maxSize.y
      );
      
      ImGui::OpenPopup("Set Workspace");
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(popupSize, ImGuiCond_Always);
      ImGui::SetNextWindowSizeConstraints(ImVec2(minWidth, minHeight), maxSize);

      if (ImGui::BeginPopupModal("Set Workspace", &m_showWorkspacePopup, 
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
      {
        ImGui::Text("Workspace Path:");
        ImGui::Spacing();
        
        ImGui::PushItemWidth(-1);
        bool enterPressed = ImGui::InputText("##workspacePath", &m_workspacePathOnUI, 
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = 120.0f;
        float buttonSpacing = 15.0f;
        float totalWidth = buttonWidth * 3 + buttonSpacing * 2;
        float currentWidth = ImGui::GetWindowWidth();
        float startX = (currentWidth - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(startX);
        
        ImGui::SameLine(0.0f, buttonSpacing);
        if (ImGui::Button("OK", ImVec2(buttonWidth, 0)) || enterPressed)
        {
          if (!m_workspacePathOnUI.empty())
          {
            m_workspace->SetDefaultWorkspace(m_workspacePathOnUI);
            m_workspace->RefreshProjects();
            m_showWorkspacePopup = false;
            ImGui::CloseCurrentPopup();
          }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
        {
          m_showWorkspacePopup = false;
          ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
      }
    }

    void Launcher::ShowNewProjectPopup()
    {
      if (!m_showNewProjectPopup)
        return;

      ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                            viewport->Pos.y + viewport->Size.y * 0.5f);

      // Calculate content size
      float padding = 40.0f; // Window padding
      float textHeight = ImGui::GetTextLineHeight();
      float inputHeight = ImGui::GetFrameHeight();
      float buttonHeight = ImGui::GetFrameHeight();
      float spacing = ImGui::GetStyle().ItemSpacing.y;
      
      float minWidth = 400.0f;
      float separatorHeight = ImGui::GetFrameHeight() * 0.5f; // Approximate separator height
      float minHeight = padding * 2 + textHeight + spacing + inputHeight + spacing + 
                        separatorHeight + spacing + buttonHeight;
      
      // Maximum size
      ImVec2 maxSize(viewport->Size.x * 0.9f, viewport->Size.y * 0.6f);
      
      // Use minimum of content size and max size
      ImVec2 popupSize(
        (minWidth < maxSize.x) ? minWidth : maxSize.x,
        (minHeight < maxSize.y) ? minHeight : maxSize.y
      );
      
      ImGui::OpenPopup("New Project");
      ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(popupSize, ImGuiCond_Appearing);
      ImGui::SetNextWindowSizeConstraints(ImVec2(minWidth, minHeight), maxSize);

      if (ImGui::BeginPopupModal("New Project", &m_showNewProjectPopup, 
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
      {
        ImGui::Text("Project Name:");
        ImGui::Spacing();
        
        ImGui::PushItemWidth(-1);
        bool enterPressed = ImGui::InputText("##newProjectName", &m_newProjectName, 
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = 120.0f;
        float buttonSpacing = 15.0f;
        float totalWidth = buttonWidth * 2 + buttonSpacing;
        float currentWidth = ImGui::GetWindowWidth();
        float startX = (currentWidth - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(startX);
        if (ImGui::Button("Create", ImVec2(buttonWidth, 0)) || enterPressed)
        {
          if (!m_newProjectName.empty())
          {
            g_launcherRunning = false;
            m_app->OnNewProject(m_newProjectName, false);
            m_workspace->SetActiveProject({m_newProjectName, ""});
            m_showNewProjectPopup = false;
            ImGui::CloseCurrentPopup();
          }
        }
        
        ImGui::SameLine(0.0f, buttonSpacing);
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
        {
          m_showNewProjectPopup = false;
          ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
      }
    }
  } // namespace Editor
} // namespace ToolKit
