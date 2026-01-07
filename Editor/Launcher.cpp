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
#include <Common/Win32Utils.h>

#define IMGUI_USER_CONFIG "tk_imconfig.h"
#include <imgui/imgui.h>

extern bool g_running;
extern bool g_launcherRunning;

namespace ToolKit
{
  namespace Editor
  {
    Launcher::Launcher(Workspace* workspace, App* app)
        : m_workspace(workspace), m_app(app)
    {
      if (m_logoTexture == nullptr)
      {
        m_logoTexture = GetTextureManager()->Create<Texture>(TexturePath(m_logoPath.c_str(), true));
        if (m_logoTexture)
        {
          m_logoTexture->Init();
        }
      }

      m_defaultProjectThumbnail = m_logoTexture;

      if (m_launchIconTexture == nullptr)
      {
        m_launchIconTexture =
            GetTextureManager()->Create<Texture>(TexturePath(m_launchIconPath.c_str(), true));
        if (m_launchIconTexture)
        {
          m_launchIconTexture->Init();
        }
      }

      if (m_folderIconTexture == nullptr)
      {
        m_folderIconTexture =
            GetTextureManager()->Create<Texture>(TexturePath(m_folderIconPath.c_str(), true));
        if (m_folderIconTexture)
        {
          m_folderIconTexture->Init();
        }
      }

      if (m_shortcutIconTexture == nullptr)
      {
        m_shortcutIconTexture =
            GetTextureManager()->Create<Texture>(TexturePath(m_shortcutIconPath.c_str(), true));
        if (m_shortcutIconTexture)
        {
          m_shortcutIconTexture->Init();
        }
      }
    }

    Launcher::~Launcher()
    {
    }

    void Launcher::ShowLauncherWindow()
    {
      UI::BeginUI();

      const float padding = 1.0f;

      ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                              viewport->Pos.y + viewport->Size.y * 0.5f);
      ImVec2 windowSize = ImVec2(m_windowWidth, m_windowHeight);
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 5.0f));

      ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoScrollbar;

      ImGui::Begin("Engine Launcher", nullptr, windowFlags);

      ImDrawList* drawList = ImGui::GetWindowDrawList();
      ImVec2 windowPos = ImGui::GetWindowPos();
      ImVec2 headerSize(windowSize.x, 50.0f);
      ImU32 headerColor = ImGui::GetColorU32(ImGuiCol_TitleBg);
      drawList->AddRectFilled(windowPos, 
                              ImVec2(windowPos.x + headerSize.x, windowPos.y + headerSize.y),
                              headerColor);

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

      float logoSize = 32.0f;
      float logoPadding = 15.0f;
      ImVec2 logoPos(logoPadding, (headerSize.y - logoSize) * 0.5f);
      
      ImGui::SetCursorPos(logoPos);
      if (m_logoTexture && m_logoTexture->m_textureId != 0)
      {
        ImGui::Image(Convert2ImGuiTexture(m_logoTexture), ImVec2(logoSize, logoSize));
      }
      else
      {
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
      
      ImGui::SetCursorPosX(panelPadding);
      ImGui::Text("Projects");
      ImGui::Spacing();
      
      float bottomPanelHeight = 40.0f;
      float bottomPadding = 20.0f;
      float spacingBetweenPanels = 10.0f;
      float titleHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
      float listHeight = windowSize.y - headerSize.y - titleHeight - bottomPanelHeight - bottomPadding - panelPadding - spacingBetweenPanels;

      float toolsPanelWidth = 180.0f;
      float projectsPanelWidth = panelWidth - toolsPanelWidth - panelSpacing;
      if (projectsPanelWidth < 200.0f)
      {
        projectsPanelWidth = 200.0f;
      }

      float listStartY = ImGui::GetCursorPosY();

      ImGui::SetCursorPos(ImVec2(panelPadding, listStartY));
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_FrameBg));
      ImGui::BeginChild("ProjectsListPanel", ImVec2(projectsPanelWidth, listHeight), true);
      ImGui::PopStyleColor();
      
      if (m_workspace && !m_workspace->m_projects.empty())
      {
        ImGui::BeginChild("ProjectsList", ImVec2(-1, -1), false);
        
        const float cardSize = 120.0f;
        const float cardSpacing = 15.0f;
        const float gridPadding = 15.0f;
        const int itemsPerRow = 6;
        
        ImGui::SetCursorPos(ImVec2(gridPadding, gridPadding));
        
        const size_t numProjects = m_workspace->m_projects.size();

        if (m_selectedProjectIndex >= (int)numProjects)
        {
          m_selectedProjectIndex = -1;
        }
        
        float availableWidth = ImGui::GetContentRegionAvail().x - gridPadding * 2;
        int actualItemsPerRow = (int)((availableWidth + cardSpacing) / (cardSize + cardSpacing));
        if (actualItemsPerRow < 1) actualItemsPerRow = 1;
        if (actualItemsPerRow > itemsPerRow) actualItemsPerRow = itemsPerRow;
        
        for (size_t i = 0; i < numProjects; ++i)
        {
          const Project& project = m_workspace->m_projects[i];
          
          ImGui::PushID((int)i);
          
          int row = (int)(i / actualItemsPerRow);
          int col = (int)(i % actualItemsPerRow);
          
          if (col == 0 && i > 0)
          {
            ImGui::NewLine();
          }
          
          ImVec2 cardSizeVec(cardSize, cardSize);
          
          if (ImGui::InvisibleButton("##projectCard", cardSizeVec))
          {
            m_selectedProjectIndex = (int)i;
          }
          
          bool isHovered = ImGui::IsItemHovered();
          bool isActive = ImGui::IsItemActive();
          
          ImVec2 itemPos = ImGui::GetItemRectMin();
          ImVec2 itemMax = ImGui::GetItemRectMax();
          
          ImDrawList* childDrawList = ImGui::GetWindowDrawList();
          
          bool isSelected = (m_selectedProjectIndex == (int)i);
          
          if (isHovered && ImGui::IsMouseDoubleClicked(0))
          {
            m_selectedProjectIndex = (int)i;
            m_workspace->SetActiveProject(project);
            g_launcherRunning = false;
          }
 
          if (isHovered || isSelected)
          {
            ImVec4 panelBgColor = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
            ImVec4 hoverColor = panelBgColor;
            
            if (isHovered)
            {
              hoverColor.x = (hoverColor.x + 0.02f > 1.0f) ? 1.0f : (hoverColor.x + 0.02f);
              hoverColor.y = (hoverColor.y + 0.02f > 1.0f) ? 1.0f : (hoverColor.y + 0.02f);
              hoverColor.z = (hoverColor.z + 0.02f > 1.0f) ? 1.0f : (hoverColor.z + 0.02f);
            }
            
            ImU32 bgColor = ImGui::GetColorU32(hoverColor);
            childDrawList->AddRectFilled(itemPos, itemMax, bgColor, 4.0f);
            
            if (isHovered)
            {
              float hoverBorderPadding = 2.0f;
              ImVec2 hoverBorderMin = ImVec2(itemPos.x + hoverBorderPadding, itemPos.y + hoverBorderPadding);
              ImVec2 hoverBorderMax = ImVec2(itemMax.x - hoverBorderPadding, itemMax.y - hoverBorderPadding);
              ImU32 borderColor = ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
              childDrawList->AddRect(hoverBorderMin, hoverBorderMax, borderColor, 4.0f, 0, 1.0f);
            }
            
            if (isSelected)
            {
              float selectionBorderPadding = 1.0f;
              ImVec2 selectionBorderMin = ImVec2(itemPos.x - selectionBorderPadding, itemPos.y - selectionBorderPadding);
              ImVec2 selectionBorderMax = ImVec2(itemMax.x + selectionBorderPadding, itemMax.y + selectionBorderPadding);
              ImU32 borderColor = ImGui::GetColorU32(ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
              childDrawList->AddRect(selectionBorderMin, selectionBorderMax, borderColor, 4.0f, 0, 2.0f);
            }
          }
          
          const char* projectName = project.name.c_str();
          float textHeight = ImGui::GetTextLineHeight();
          float textWidth = ImGui::CalcTextSize(projectName).x;
          
          float imagePadding = 4.0f;
          float imageHeight = cardSize - textHeight - 4.0f - imagePadding * 2;
          float imageWidth = cardSize - imagePadding * 2;
          float displaySize = glm::min(imageWidth, imageHeight);
          float imageX = itemPos.x + (cardSize - displaySize) * 0.5f;
          float imageY = itemPos.y + imagePadding + (imageHeight - displaySize) * 0.5f;
          ImVec2 imagePos = ImVec2(imageX, imageY);
          ImVec2 imageMax = ImVec2(imageX + displaySize, imageY + displaySize);

          const String thumbnailPath = ConcatPaths({m_workspace->GetActiveWorkspace(), project.name, "thumbnail.png"});
          bool thumbnailExists = CheckSystemFile(thumbnailPath);
          TexturePtr projectThumbnail = GetTextureManager()->Create<Texture>(thumbnailPath);
          if (projectThumbnail)
          {
            projectThumbnail->Init();
          }
          TexturePtr thumbTexture = thumbnailExists ? projectThumbnail : m_logoTexture;
          if (thumbTexture && thumbTexture->m_textureId != 0)
          {
            ImVec2 uvMin(0, 0);
            ImVec2 uvMax(1, 1);
            
            if (thumbTexture->m_width > 0 && thumbTexture->m_height > 0)
            {
              float texAspect = (float)thumbTexture->m_width / (float)thumbTexture->m_height;
              if (texAspect > 1.0f)
              {
                float uvWidth = 1.0f / texAspect;
                float uvOffset = (1.0f - uvWidth) * 0.5f;
                uvMin.x = uvOffset;
                uvMax.x = uvOffset + uvWidth;
              }
              else if (texAspect < 1.0f)
              {
                float uvHeight = texAspect;
                float uvOffset = (1.0f - uvHeight) * 0.5f;
                uvMin.y = uvOffset;
                uvMax.y = uvOffset + uvHeight;
              }
            }
            
            childDrawList->AddImageRounded(Convert2ImGuiTexture(thumbTexture),
                                          imagePos,
                                          imageMax,
                                          uvMin, uvMax,
                                          ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 4.0f);
          }
          else
          {
            ImU32 iconBgColor = ImGui::GetColorU32(ImGuiCol_Button);
            childDrawList->AddRectFilled(imagePos, imageMax, iconBgColor, 4.0f);
          }
          
          float textY = itemPos.y + imageHeight + 2.0f;
          ImVec2 textPos = ImVec2(itemPos.x + (cardSize - textWidth) * 0.5f, textY);
          childDrawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), projectName);
          
          ImGui::SameLine(0.0f, cardSpacing);
          
          ImGui::PopID();
        }
        
        ImGui::EndChild();
      }
      else if (m_workspace)
      {
        m_selectedProjectIndex = -1;
        ImGui::SetCursorPosX((panelWidth - ImGui::CalcTextSize("No projects found. Create a new project to get started.").x) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::Text("No projects found. Create a new project to get started.");
        ImGui::PopStyleColor();
      }
      
      ImGui::EndChild();

      ImGui::SetCursorPos(ImVec2(panelPadding + projectsPanelWidth + panelSpacing, listStartY));
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg));
      ImGui::BeginChild("ToolsPanel", ImVec2(toolsPanelWidth, listHeight), true);
      ImGui::PopStyleColor();
      {
        float buttonWidth = toolsPanelWidth - 20.0f;
        float buttonHeight = 28.0f;

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);

        bool hasSelection = m_workspace && 
                            m_selectedProjectIndex >= 0 && 
                            m_selectedProjectIndex < (int)m_workspace->m_projects.size();

        ImGui::BeginDisabled(!hasSelection);

        auto drawToolButton = [&](const char* id,
                                  TexturePtr icon,
                                  const char* label,
                                  auto onClick)
        {
          ImVec2 cursorPos = ImGui::GetCursorScreenPos();
          ImVec2 size(buttonWidth, buttonHeight);

          ImGui::InvisibleButton(id, size);
          bool pressed = ImGui::IsItemClicked() && hasSelection;
          bool hovered = ImGui::IsItemHovered() && hasSelection;
          bool held    = ImGui::IsItemActive()  && hasSelection;

          ImDrawList* dl = ImGui::GetWindowDrawList();
          ImVec2 min = cursorPos;
          ImVec2 max = ImVec2(cursorPos.x + size.x, cursorPos.y + size.y);

          ImVec4 col = ImGui::GetStyle().Colors[ImGuiCol_Button];
          if (!hasSelection)
          {
            col.w *= 0.5f;
          }
          else if (held)
          {
            col = ImGui::GetStyle().Colors[ImGuiCol_ButtonActive];
          }
          else if (hovered)
          {
            col = ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered];
          }

          dl->AddRectFilled(min, max, ImGui::GetColorU32(col), 4.0f);

          float iconSize = buttonHeight - 8.0f;
          ImVec2 iconMin = ImVec2(min.x + 6.0f, min.y + (size.y - iconSize) * 0.5f);
          ImVec2 iconMax = ImVec2(iconMin.x + iconSize, iconMin.y + iconSize);

          if (icon && icon->m_textureId != 0)
          {
            dl->AddImage(Convert2ImGuiTexture(icon), iconMin, iconMax);
          }

          ImVec2 textSize = ImGui::CalcTextSize(label);
          float textX = iconMax.x + 6.0f;
          float textY = min.y + (size.y - textSize.y) * 0.5f;
          dl->AddText(ImVec2(textX, textY), ImGui::GetColorU32(ImGuiCol_Text), label);

          if (pressed)
          {
            onClick();
          }

          ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y * 0.5f));
        };

        ImGui::SetCursorPosX((toolsPanelWidth - buttonWidth) * 0.5f);
        drawToolButton("##tool_launch", m_launchIconTexture, "Launch", [&]()
        {
          const Project& selected = m_workspace->m_projects[m_selectedProjectIndex];
          m_workspace->SetActiveProject(selected);
          g_launcherRunning = false;
        });

        ImGui::SetCursorPosX((toolsPanelWidth - buttonWidth) * 0.5f);
        drawToolButton("##tool_open_folder", m_folderIconTexture, "Open in Folder", [&]()
        {
          const Project& selected = m_workspace->m_projects[m_selectedProjectIndex];
          String projectFolder = ConcatPaths({ m_workspace->GetActiveWorkspace(), selected.name });

          ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
          if (platform_io.Platform_OpenInShellFn)
          {
            platform_io.Platform_OpenInShellFn(ImGui::GetCurrentContext(), projectFolder.c_str());
          }
        });

        if (m_createProjectShortcutOnDesktopFn)
        {
          ImGui::SetCursorPosX((toolsPanelWidth - buttonWidth) * 0.5f);
          drawToolButton("##tool_shortcut",
                         m_shortcutIconTexture,
                         "Create Shortcut",
                         [&]()
                         {
                           const Project& selected = m_workspace->m_projects[m_selectedProjectIndex];
                           String workspacePath = m_workspace->GetActiveWorkspace();
                           String projectName = selected.name;
                           String args = "--workspace \"" + workspacePath + "\" --project-name \"" + projectName + "\"";
                           m_createProjectShortcutOnDesktopFn(selected.name, args);
                         });
        }

        ImGui::EndDisabled();
      }
      ImGui::EndChild();
      
      ImGui::SetCursorPosY(windowSize.y - bottomPanelHeight - bottomPadding);
      
      ImGui::SetCursorPosX(panelPadding);
      float workspacePanelWidth = panelWidth * 0.5f;
      ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg));
      ImGui::BeginChild("WorkspacePanel", ImVec2(workspacePanelWidth, bottomPanelHeight), true);
      ImGui::PopStyleColor();
      
      float contentHeight = ImGui::GetTextLineHeight();
      float verticalOffset = (bottomPanelHeight - contentHeight) * 0.5f;
      ImGui::SetCursorPosY(verticalOffset);
      
      HandleWorkspace();
      
      ImGui::EndChild();

      if (m_workspace && m_app->IsWorkspaceSane(false, false))
      {
        float buttonWidth = 150.0f;
        float buttonHeight = 35.0f;
        
        ImGui::SetCursorPosY(windowSize.y - buttonHeight - bottomPadding);
        float buttonX = windowSize.x - buttonWidth - panelPadding;
        ImGui::SetCursorPosX(buttonX);
        
        if (ImGui::Button("+ New Project", ImVec2(buttonWidth, buttonHeight)))
        {
          m_showNewProjectPopup = true;
        }
      }

      ShowWorkspacePopup();
      ShowNewProjectPopup();

      ImGui::PopStyleVar(4);

      ImGui::End();

      UI::EndUI();
    }

    void Launcher::HandleWorkspace()
    {
      if (!m_workspace)
      {
        return;
      }

      String currentWorkspace = m_workspace->GetActiveWorkspace();
      bool hasWorkspace = !currentWorkspace.empty() && CheckSystemFile(currentWorkspace);

      float panelWidth = ImGui::GetWindowWidth();
      float textPadding = 10.0f;
      float buttonPadding = 10.0f;
      
      if (!hasWorkspace)
      {
        float buttonWidth = 150.0f;
        
        ImGui::SetCursorPosX(textPadding);
        float textY = ImGui::GetCursorPosY();
        ImGui::Text("Workspace:");
        
        ImGui::SetCursorPos(ImVec2(panelWidth - buttonWidth - buttonPadding, textY - 5.0f));
        if (ImGui::Button("Set Workspace", ImVec2(buttonWidth, 0.0f)))
        {
          m_showWorkspacePopup = true;
          m_workspacePathOnUI = m_workspace->GetDefaultWorkspace();
        }
      }
      else
      {
        float editButtonWidth = 80.0f;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        
        float textLabelWidth = ImGui::CalcTextSize("Workspace:").x;
        float availableWidth = panelWidth - textLabelWidth - spacing - editButtonWidth - textPadding - buttonPadding - spacing;
        
        String displayPath = currentWorkspace;
        float pathWidth = ImGui::CalcTextSize(displayPath.c_str()).x;
        
        if (pathWidth > availableWidth)
        {
          displayPath = currentWorkspace;
          while (pathWidth > availableWidth && displayPath.length() > 0)
          {
            displayPath.pop_back();
            String testPath = displayPath + "...";
            pathWidth = ImGui::CalcTextSize(testPath.c_str()).x;
          }
          displayPath += "...";
        }
        
        ImGui::SetCursorPosX(textPadding);
        float textY = ImGui::GetCursorPosY();
        ImGui::Text("Workspace:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        ImGui::Text("%s", displayPath.c_str());
        ImGui::PopStyleColor();
        
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
 
      float padding = 20.0f;
      float textHeight = 20.0f;
      float inputHeight = 25.0f;
      float buttonHeight = 25.0f;
      float spacing = 5.0f;
      
      float minWidth = 500.0f;
      float minHeight = padding + textHeight + spacing + inputHeight + 8.0f + 
                        buttonHeight + padding * 0.3f;
      
      ImVec2 maxSize(viewport->Size.x * 0.9f, viewport->Size.y * 0.6f);
      
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
        float innerPad = 10.0f;

        ImGui::SetCursorPosX(innerPad);
        ImGui::Text("Workspace Path:");
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        
        ImGui::SetCursorPosX(innerPad);
        ImGui::PushItemWidth(ImGui::GetWindowWidth() - innerPad * 2);
        bool enterPressed = ImGui::InputText("##workspacePath", &m_workspacePathOnUI, 
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();
        
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
        
        float buttonWidth = 120.0f;
        float buttonSpacing = 15.0f;
        float totalWidth = buttonWidth * 2 + buttonSpacing;
        float currentWidth = ImGui::GetWindowWidth();
        float startX = (currentWidth - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(startX);
        
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
        
        ImGui::SameLine(0.0f, buttonSpacing);
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

      float padding = 20.0f;
      float tabHeight = ImGui::GetFrameHeight();
      float textHeight = ImGui::GetTextLineHeight();
      float inputHeight = ImGui::GetFrameHeight();
      float buttonHeight = ImGui::GetFrameHeight();
      float spacing = 8.0f;
      float separatorHeight = 2.0f;
      
      float minWidth = 500.0f;
      float minHeight = padding * 2 + tabHeight + spacing + 
                        textHeight + spacing + inputHeight + spacing + 
                        separatorHeight + spacing + buttonHeight + padding;
      
      ImVec2 popupSize(minWidth, minHeight);
      
      ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(popupSize, ImGuiCond_Appearing);
      ImGui::OpenPopup("New Project");

      if (ImGui::BeginPopupModal("New Project", &m_showNewProjectPopup, 
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
      {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 15.0f));
        
        float innerPad = 15.0f;
        float tabBarPadding = 15.0f;

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);
        
        ImGui::SetCursorPosX(tabBarPadding);
        if (ImGui::BeginTabBar("##NewProjectTabs"))
        {
          if (ImGui::BeginTabItem("Local"))
          {
            m_newProjectTabLocal = true;
            ImGui::EndTabItem();
          }
          if (ImGui::BeginTabItem("Remote"))
          {
            m_newProjectTabLocal = false;
            ImGui::EndTabItem();
          }
          ImGui::EndTabBar();
        }
        
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);

        if (m_isCloning)
        {
          ImGui::BeginDisabled();
        }

        bool enterPressed = false;
        
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        
        if (m_newProjectTabLocal)
        {
          ImGui::SetCursorPosX(innerPad);
          ImGui::Text("Project Name:");
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
          
          ImGui::SetCursorPosX(innerPad);
          ImGui::PushItemWidth(ImGui::GetWindowWidth() - innerPad * 2);
          enterPressed = ImGui::InputText("##newProjectName", &m_newProjectName, 
                                          ImGuiInputTextFlags_EnterReturnsTrue);
          ImGui::PopItemWidth();
        }
        else
        {
          ImGui::SetCursorPosX(innerPad);
          ImGui::Text("Git URL:");
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
          
          ImGui::SetCursorPosX(innerPad);
          ImGui::PushItemWidth(ImGui::GetWindowWidth() - innerPad * 2);
          enterPressed = ImGui::InputText("##newProjectUrl", &m_newProjectPathOrUrl,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
          ImGui::PopItemWidth();
        }
        
        ImGui::PopStyleVar();

        if (m_isCloning)
        {
          ImGui::EndDisabled();
        }
        
        if (m_isCloning)
        {
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);
          ImGui::SetCursorPosX(innerPad);
          ImGui::Text("Cloning repository...");
          if (!m_cloneProgress.empty())
          {
            ImGui::SetCursorPosX(innerPad);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::TextWrapped("%s", m_cloneProgress.c_str());
            ImGui::PopStyleColor();
          }
        }
        else
        {
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);
        }
        
        float availableHeight = ImGui::GetContentRegionAvail().y;
        float buttonAreaHeight = ImGui::GetFrameHeight() + spacing * 2;
        if (availableHeight > buttonAreaHeight)
        {
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + availableHeight - buttonAreaHeight);
        }
        
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = 120.0f;
        float buttonSpacing = 15.0f;
        float totalWidth = buttonWidth * 2 + buttonSpacing;
        float currentWidth = ImGui::GetWindowWidth();
        float startX = (currentWidth - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(startX);
        
        bool wasCloning = m_isCloning;
        if (m_isCloning)
        {
          ImGui::BeginDisabled();
        }
        
        bool createClicked = ImGui::Button("Create", ImVec2(buttonWidth, 0));
        bool shouldCreate = (createClicked || (enterPressed && !m_isCloning));
        
        if (shouldCreate)
        {
          bool canCreate = false;
          String projectName = m_newProjectName;
          
          if (m_newProjectTabLocal)
          {
            canCreate = !m_newProjectName.empty();
          }
          else
          {
            if (!m_newProjectPathOrUrl.empty())
            {
              String url = m_newProjectPathOrUrl;
              if (url.size() > 4 && url.substr(url.size() - 4) == ".git")
              {
                url = url.substr(0, url.size() - 4);
              }
              size_t lastSlash = url.find_last_of("/");
              size_t lastColon = url.find_last_of(":");
              size_t startPos = (lastSlash != String::npos) ? lastSlash + 1 : 
                               ((lastColon != String::npos) ? lastColon + 1 : 0);
              if (startPos < url.size())
              {
                projectName = url.substr(startPos);
                canCreate = !projectName.empty();
              }
            }
          }
          
          if (canCreate)
          {
            if (!m_newProjectTabLocal)
            {
              m_isCloning = true;
              m_cloneProgress = "Starting git clone...";
              
              String workspacePath = m_workspace->GetActiveWorkspace();
              String targetPath = ConcatPaths({workspacePath, projectName});
              
              String gitCmd = "git clone \"" + m_newProjectPathOrUrl + "\" \"" + targetPath + "\"";
              
              PlatformHelpers::SysComExec(gitCmd, true, false, 
                [this, targetPath](int exitCode) -> void
                {
                  m_isCloning = false;
                  if (exitCode == 0)
                  {
                    m_cloneProgress = "Clone completed successfully!";
                    m_workspace->RefreshProjects();
                    m_showNewProjectPopup = false;
                    ImGui::CloseCurrentPopup();
                    m_newProjectName.clear();
                    m_newProjectPathOrUrl.clear();
                    m_cloneProgress.clear();
                  }
                  else
                  {
                    m_cloneProgress = "Git clone failed with exit code: " + std::to_string(exitCode);
                  }
                });
            }
            else
            {
              g_launcherRunning = false;
              m_app->OnNewProject(projectName);
              m_workspace->SetActiveProject({projectName, ""});
              m_showNewProjectPopup = false;
              ImGui::CloseCurrentPopup();
              m_newProjectName.clear();
              m_newProjectPathOrUrl.clear();
            }
          }
        }
        
        ImGui::SameLine(0.0f, buttonSpacing);
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)) && !m_isCloning)
        {
          m_showNewProjectPopup = false;
          ImGui::CloseCurrentPopup();
          m_newProjectName.clear();
          m_newProjectPathOrUrl.clear();
          m_isCloning = false;
          m_cloneProgress.clear();
        }
        
        if (wasCloning)
        {
          ImGui::EndDisabled();
        }
        
        ImGui::PopStyleVar();

        ImGui::EndPopup();
      }
    }
  } // namespace Editor
} // namespace ToolKit
