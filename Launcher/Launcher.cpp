/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "Launcher.h"

#include <RenderSystem.h>
#include <Renderer.h>
#include <Texture.h>
#include <ToolKit.h>
#include <Util.h>
#include <WorkspaceTypes.h>

#define IMGUI_USER_CONFIG "tk_imconfig.h"
#include <RenderSystem.h>
#include <Renderer.h>
#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <cstdio>

extern bool g_running;
extern bool g_launcherRunning;

namespace ToolKit
{
  namespace Launcher
  {
    ImFont *LiberationSans, *LiberationSansBold, *IconFont;

    void DeserializeThemeSettings()
    {
      Vec4Array colors;
      if (!Workspace::DeserializeThemeColors("DarkTheme.settings", colors))
      {
        return;
      }

      if ((int) colors.size() < ImGuiCol_COUNT)
      {
        return;
      }

      ImGuiStyle& style                                = ImGui::GetStyle();
      style.Colors[ImGuiCol_Text]                      = ImVec4(colors[0]);
      style.Colors[ImGuiCol_TextDisabled]              = ImVec4(colors[1]);
      style.Colors[ImGuiCol_WindowBg]                  = ImVec4(colors[2]);
      style.Colors[ImGuiCol_ChildBg]                   = ImVec4(colors[3]);
      style.Colors[ImGuiCol_PopupBg]                   = ImVec4(colors[4]);
      style.Colors[ImGuiCol_Border]                    = ImVec4(colors[5]);
      style.Colors[ImGuiCol_BorderShadow]              = ImVec4(colors[6]);
      style.Colors[ImGuiCol_FrameBg]                   = ImVec4(colors[7]);
      style.Colors[ImGuiCol_FrameBgHovered]            = ImVec4(colors[8]);
      style.Colors[ImGuiCol_FrameBgActive]             = ImVec4(colors[9]);
      style.Colors[ImGuiCol_TitleBg]                   = ImVec4(colors[10]);
      style.Colors[ImGuiCol_TitleBgActive]             = ImVec4(colors[11]);
      style.Colors[ImGuiCol_TitleBgCollapsed]          = ImVec4(colors[12]);
      style.Colors[ImGuiCol_MenuBarBg]                 = ImVec4(colors[13]);
      style.Colors[ImGuiCol_ScrollbarBg]               = ImVec4(colors[14]);
      style.Colors[ImGuiCol_ScrollbarGrab]             = ImVec4(colors[15]);
      style.Colors[ImGuiCol_ScrollbarGrabHovered]      = ImVec4(colors[16]);
      style.Colors[ImGuiCol_ScrollbarGrabActive]       = ImVec4(colors[17]);
      style.Colors[ImGuiCol_CheckMark]                 = ImVec4(colors[18]);
      style.Colors[ImGuiCol_SliderGrab]                = ImVec4(colors[19]);
      style.Colors[ImGuiCol_SliderGrabActive]          = ImVec4(colors[20]);
      style.Colors[ImGuiCol_Button]                    = ImVec4(colors[21]);
      style.Colors[ImGuiCol_ButtonHovered]             = ImVec4(colors[22]);
      style.Colors[ImGuiCol_ButtonActive]              = ImVec4(colors[23]);
      style.Colors[ImGuiCol_Header]                    = ImVec4(colors[24]);
      style.Colors[ImGuiCol_HeaderHovered]             = ImVec4(colors[25]);
      style.Colors[ImGuiCol_HeaderActive]              = ImVec4(colors[26]);
      style.Colors[ImGuiCol_Separator]                 = ImVec4(colors[27]);
      style.Colors[ImGuiCol_SeparatorHovered]          = ImVec4(colors[28]);
      style.Colors[ImGuiCol_SeparatorActive]           = ImVec4(colors[29]);
      style.Colors[ImGuiCol_ResizeGrip]                = ImVec4(colors[30]);
      style.Colors[ImGuiCol_ResizeGripHovered]         = ImVec4(colors[31]);
      style.Colors[ImGuiCol_ResizeGripActive]          = ImVec4(colors[32]);
      style.Colors[ImGuiCol_InputTextCursor]           = ImVec4(colors[33]);
      style.Colors[ImGuiCol_TabHovered]                = ImVec4(colors[34]);
      style.Colors[ImGuiCol_Tab]                       = ImVec4(colors[35]);
      style.Colors[ImGuiCol_TabSelected]               = ImVec4(colors[36]);
      style.Colors[ImGuiCol_TabSelectedOverline]       = ImVec4(colors[37]);
      style.Colors[ImGuiCol_TabDimmed]                 = ImVec4(colors[38]);
      style.Colors[ImGuiCol_TabDimmedSelected]         = ImVec4(colors[39]);
      style.Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(colors[40]);
      style.Colors[ImGuiCol_DockingPreview]            = ImVec4(colors[41]);
      style.Colors[ImGuiCol_DockingEmptyBg]            = ImVec4(colors[42]);
      style.Colors[ImGuiCol_PlotLines]                 = ImVec4(colors[43]);
      style.Colors[ImGuiCol_PlotLinesHovered]          = ImVec4(colors[44]);
      style.Colors[ImGuiCol_PlotHistogram]             = ImVec4(colors[45]);
      style.Colors[ImGuiCol_PlotHistogramHovered]      = ImVec4(colors[46]);
      style.Colors[ImGuiCol_TableHeaderBg]             = ImVec4(colors[47]);
      style.Colors[ImGuiCol_TableBorderStrong]         = ImVec4(colors[48]);
      style.Colors[ImGuiCol_TableBorderLight]          = ImVec4(colors[49]);
      style.Colors[ImGuiCol_TableRowBg]                = ImVec4(colors[50]);
      style.Colors[ImGuiCol_TableRowBgAlt]             = ImVec4(colors[51]);
      style.Colors[ImGuiCol_TextLink]                  = ImVec4(colors[52]);
      style.Colors[ImGuiCol_TextSelectedBg]            = ImVec4(colors[53]);
      style.Colors[ImGuiCol_TreeLines]                 = ImVec4(colors[54]);
      style.Colors[ImGuiCol_DragDropTarget]            = ImVec4(colors[55]);
      style.Colors[ImGuiCol_DragDropTargetBg]          = ImVec4(colors[56]);
      style.Colors[ImGuiCol_UnsavedMarker]             = ImVec4(colors[57]);
      style.Colors[ImGuiCol_NavCursor]                 = ImVec4(colors[58]);
      style.Colors[ImGuiCol_NavWindowingHighlight]     = ImVec4(colors[59]);
      style.Colors[ImGuiCol_NavWindowingDimBg]         = ImVec4(colors[60]);
      style.Colors[ImGuiCol_ModalWindowDimBg]          = ImVec4(colors[61]);

      ImGuiIO& io                                      = ImGui::GetIO();
      // Handle font loading.
      static const ImWchar utf8TR[] = {0x0020, 0x00FF, 0x00c7, 0x00c7, 0x00e7, 0x00e7, 0x011e, 0x011e, 0x011f,
                                       0x011f, 0x0130, 0x0130, 0x0131, 0x0131, 0x00d6, 0x00d6, 0x00f6, 0x00f6,
                                       0x015e, 0x015e, 0x015f, 0x015f, 0x00dc, 0x00dc, 0x00fc, 0x00fc, 0};

      io.Fonts->Clear();
      LiberationSans =
          io.Fonts->AddFontFromFileTTF(FontPath("LiberationSans-Regular.ttf", true).c_str(), 14.0f, nullptr, utf8TR);

      static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
      ImFontConfig icons_config;
      icons_config.MergeMode = true;
      // icons_config.PixelSnapH = true;
      IconFont               = io.Fonts->AddFontFromFileTTF(FontPath(FONT_ICON_FILE_NAME_FA, true).c_str(),
                                              14.0f,
                                              &icons_config,
                                              icons_ranges);

      LiberationSansBold =
          io.Fonts->AddFontFromFileTTF(FontPath("LiberationSans-Bold.ttf", true).c_str(), 14.0f, nullptr, utf8TR);
    }

    LauncherApp::LauncherApp()
    {
      m_logoPath             = "/Icons/app.png";
      m_defaultThumbnailPath = "/splash.png";
      m_thumbnailPath        = "../../thumbnail.png";
      m_launchIconPath       = "/Icons/play.png";
      m_folderIconPath       = "/Icons/folder.png";
      m_shortcutIconPath     = "/Icons/file.png";

      m_workspace            = std::make_shared<Workspace>();
      m_workspace->Init();

      m_logoTexture = GetTextureManager()->Create<Texture>(TexturePath(m_logoPath.c_str(), true));
      if (m_logoTexture)
      {
        m_logoTexture->Init();
      }

      m_defaultProjectThumbnail =
          GetTextureManager()->Create<Texture>(TexturePath(m_defaultThumbnailPath.c_str(), true));
      if (m_defaultProjectThumbnail)
      {
        m_defaultProjectThumbnail->Init();
      }

      if (m_launchIconTexture == nullptr)
      {
        m_launchIconTexture = GetTextureManager()->Create<Texture>(TexturePath(m_launchIconPath.c_str(), true));
        if (m_launchIconTexture)
        {
          m_launchIconTexture->Init();
        }
      }

      if (m_folderIconTexture == nullptr)
      {
        m_folderIconTexture = GetTextureManager()->Create<Texture>(TexturePath(m_folderIconPath.c_str(), true));
        if (m_folderIconTexture)
        {
          m_folderIconTexture->Init();
        }
      }

      if (m_shortcutIconTexture == nullptr)
      {
        m_shortcutIconTexture = GetTextureManager()->Create<Texture>(TexturePath(m_shortcutIconPath.c_str(), true));
        if (m_shortcutIconTexture)
        {
          m_shortcutIconTexture->Init();
        }
      }
    }

    LauncherApp::~LauncherApp() {}

    void LauncherApp::OpenProject(const Project& project)
    {
      if (!m_sysComExecFn)
      {
        return;
      }

      m_workspace->SetActiveProject(project);
      m_workspace->Serialize(nullptr, nullptr);

      String workspacePath = m_workspace->GetActiveWorkspace();
      UnixifyPath(workspacePath);
#ifdef TK_DEBUG
      String cmd = "Editord.exe --workspace \"" + workspacePath + "\" --project-name \"" + project.name + "\"";
#else
      String cmd = "Editor.exe --workspace \"" + workspacePath + "\" --project-name \"" + project.name + "\"";
#endif

      m_sysComExecFn(cmd, true, false, nullptr);
      g_running = false;
    }

    void LauncherApp::UpdateThumbnailCache()
    {
      m_thumbnailCache.clear();
      if (m_workspace)
      {
        for (const Project& project : m_workspace->m_projects)
        {
          String thumbnailPath = ConcatPaths({m_workspace->GetActiveWorkspace(), project.name, "thumbnail.png"});
          m_thumbnailCache[project.name] = CheckSystemFile(thumbnailPath);
        }
      }
    }

    void LauncherApp::ShowLauncherWindow()
    {
      const float padding = 1.0f;

      ImGui::PushFont(LiberationSansBold);

      ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImVec2 center     = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
      ImVec2 windowSize = ImVec2(m_windowWidth, m_windowHeight);
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 5.0f));

      ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar;

      ImGui::Begin("Engine Launcher", nullptr, windowFlags);

      ImDrawList* drawList = ImGui::GetWindowDrawList();
      ImVec2 windowPos     = ImGui::GetWindowPos();
      ImVec2 headerSize(windowSize.x, 50.0f);
      ImU32 headerColor = ImGui::GetColorU32(ImGuiCol_TitleBg);
      drawList->AddRectFilled(windowPos, ImVec2(windowPos.x + headerSize.x, windowPos.y + headerSize.y), headerColor);

      // Close button.
      float closeButtonSize    = 28.0f;
      float closeButtonPadding = 5.0f;
      ImVec2 closeButtonPos(windowSize.x - closeButtonSize - closeButtonPadding, closeButtonPadding);

      ImGui::SetCursorPos(closeButtonPos);
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.8f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));

      if (ImGui::Button("x", ImVec2(closeButtonSize, closeButtonSize)))
      {
        g_running = false;
      }
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(3);

      // Logo / title.
      float logoSize    = 32.0f;
      float logoPadding = 15.0f;
      ImVec2 logoPos(logoPadding, (headerSize.y - logoSize) * 0.5f);

      ImGui::SetCursorPos(logoPos);
      if (m_logoTexture && Renderer::GetNativeTextureHandle(m_logoTexture) != 0)
      {
        ImGui::Image(Convert2ImGuiTexture(m_logoTexture), ImVec2(logoSize, logoSize), ImVec2(0, 1), ImVec2(1, 0));
      }
      else
      {
        ImVec2 logoScreenPos = ImVec2(windowPos.x + logoPos.x, windowPos.y + logoPos.y);
        ImU32 logoBgColor    = ImGui::GetColorU32(ImGuiCol_FrameBg);
        drawList->AddRectFilled(logoScreenPos,
                                ImVec2(logoScreenPos.x + logoSize, logoScreenPos.y + logoSize),
                                logoBgColor,
                                4.0f);
        ImVec2 logoTextPos = ImVec2(logoScreenPos.x + logoSize * 0.5f - 8.0f, logoScreenPos.y + logoSize * 0.5f - 6.0f);
        ImU32 textColor    = ImGui::GetColorU32(ImGuiCol_Text);
        drawList->AddText(logoTextPos, textColor, "TK");
      }

      ImGui::SetCursorPos(ImVec2(logoPos.x + logoSize + 10.0f, (headerSize.y - ImGui::GetTextLineHeight()) * 0.5f));
      ImGui::Text("ToolKit Launcher");

      ImGui::SetCursorPosY(headerSize.y);
      ImGui::Separator();
      ImGui::Spacing();

      // Layout constants.
      float panelPadding = 15.0f;
      float panelSpacing = 10.0f;
      float panelWidth   = windowSize.x - panelPadding * 2;

      ImGui::SetCursorPosX(panelPadding);

      float bottomPanelHeight = 40.0f;
      float bottomPadding     = 20.0f;
      float tabBarHeight      = ImGui::GetFrameHeight();
      float listHeight =
          windowSize.y - headerSize.y - tabBarHeight - bottomPanelHeight - bottomPadding - panelPadding - 10.0f;

      if (ImGui::BeginTabBar("##MainTabs"))
      {
        if (ImGui::BeginTabItem("Projects"))
        {
          float toolsPanelWidth    = 180.0f;
          float projectsPanelWidth = panelWidth - toolsPanelWidth - panelSpacing;
          if (projectsPanelWidth < 200.0f)
          {
            projectsPanelWidth = 200.0f;
          }

          float listStartY = ImGui::GetCursorPosY();

          if (m_workspace && !m_workspace->m_projects.empty())
          {
            if (m_thumbnailCache.empty())
            {
              UpdateThumbnailCache();
            }

            // Search bar.
            ImGui::SetCursorPosX(panelPadding + 15.0f);
            ImGui::SetCursorPosY(listStartY + 10.0f);
            float searchBarWidth = glm::min(300.0f, projectsPanelWidth - 30.0f);
            ImGui::PushItemWidth(searchBarWidth);
            ImGui::InputTextWithHint("##searchProjects", "Search...", &m_searchFilter);
            ImGui::PopItemWidth();

            float searchBarHeight    = ImGui::GetItemRectSize().y;
            float projectsListStartY = listStartY + searchBarHeight + 14.0f;
            float projectsListHeight = listHeight - (projectsListStartY - listStartY);

            ImGui::SetCursorPos(ImVec2(panelPadding, projectsListStartY));
            ImGui::BeginChild("ProjectsList", ImVec2(projectsPanelWidth, projectsListHeight), false);

            const float cardSize    = 120.0f;
            const float cardSpacing = 15.0f;
            const float gridPadding = 15.0f;
            const int itemsPerRow   = 6;

            ImGui::SetCursorPos(ImVec2(gridPadding, gridPadding));

            // Filter projects.
            std::vector<size_t> filteredIndices;
            for (size_t i = 0; i < m_workspace->m_projects.size(); ++i)
            {
              const Project& project = m_workspace->m_projects[i];
              if (m_searchFilter.empty())
              {
                filteredIndices.push_back(i);
              }
              else
              {
                String projectNameLower = project.name;
                String filterLower      = m_searchFilter;
                for (char& c : projectNameLower)
                {
                  if (c >= 'A' && c <= 'Z')
                    c = c - 'A' + 'a';
                }
                for (char& c : filterLower)
                {
                  if (c >= 'A' && c <= 'Z')
                    c = c - 'A' + 'a';
                }
                if (projectNameLower.find(filterLower) != String::npos)
                {
                  filteredIndices.push_back(i);
                }
              }
            }

            const size_t numProjects = filteredIndices.size();

            if (m_selectedProjectIndex >= (int) m_workspace->m_projects.size())
            {
              m_selectedProjectIndex = -1;
            }

            float availableWidth  = ImGui::GetContentRegionAvail().x - gridPadding * 2;
            int actualItemsPerRow = (int) ((availableWidth + cardSpacing) / (cardSize + cardSpacing));
            if (actualItemsPerRow < 1)
              actualItemsPerRow = 1;
            if (actualItemsPerRow > itemsPerRow)
              actualItemsPerRow = itemsPerRow;

            for (size_t filterIdx = 0; filterIdx < numProjects; ++filterIdx)
            {
              size_t i               = filteredIndices[filterIdx];
              const Project& project = m_workspace->m_projects[i];

              ImGui::PushID((int) i);

              int col = (int) (filterIdx % actualItemsPerRow);

              if (col == 0)
              {
                if (filterIdx > 0)
                {
                  ImGui::NewLine();
                }
                ImGui::SetCursorPosX(gridPadding);
              }

              ImVec2 cardSizeVec(cardSize, cardSize);

              if (ImGui::InvisibleButton("##projectCard", cardSizeVec))
              {
                m_selectedProjectIndex = (int) i;
              }

              bool isHovered            = ImGui::IsItemHovered();
              ImVec2 itemPos            = ImGui::GetItemRectMin();
              ImVec2 itemMax            = ImGui::GetItemRectMax();
              ImDrawList* childDrawList = ImGui::GetWindowDrawList();
              bool isSelected           = (m_selectedProjectIndex == (int) i);

              if (isHovered && ImGui::IsMouseDoubleClicked(0))
              {
                m_selectedProjectIndex = (int) i;
                OpenProject(project);
              }

              // Hover / selection highlight.
              if (isHovered || isSelected)
              {
                ImVec4 hoverColor = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
                if (isHovered)
                {
                  hoverColor.x = glm::min(hoverColor.x + 0.02f, 1.0f);
                  hoverColor.y = glm::min(hoverColor.y + 0.02f, 1.0f);
                  hoverColor.z = glm::min(hoverColor.z + 0.02f, 1.0f);
                }
                childDrawList->AddRectFilled(itemPos, itemMax, ImGui::GetColorU32(hoverColor), 4.0f);

                if (isSelected)
                {
                  ImU32 borderColor = ImGui::GetColorU32(ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                  childDrawList->AddRect(ImVec2(itemPos.x - 1, itemPos.y - 1),
                                         ImVec2(itemMax.x + 1, itemMax.y + 1),
                                         borderColor,
                                         4.0f,
                                         0,
                                         2.0f);
                }
              }

              // Thumbnail.
              const char* projectName = project.name.c_str();
              float textHeight        = ImGui::GetTextLineHeight();
              float imagePadding      = 4.0f;
              float imageHeight       = cardSize - textHeight - 4.0f - imagePadding * 2;
              float displaySize       = glm::min(cardSize - imagePadding * 2, imageHeight);
              float imageX            = itemPos.x + (cardSize - displaySize) * 0.5f;
              float imageY            = itemPos.y + imagePadding + (imageHeight - displaySize) * 0.5f;
              ImVec2 imagePos         = ImVec2(imageX, imageY);
              ImVec2 imageMax         = ImVec2(imageX + displaySize, imageY + displaySize);

              const String thumbnailPath =
                  ConcatPaths({m_workspace->GetActiveWorkspace(), project.name, "thumbnail.png"});

              bool thumbnailExists = false;
              if (m_thumbnailCache.count(project.name) > 0)
              {
                thumbnailExists = m_thumbnailCache[project.name];
              }
              else
              {
                thumbnailExists                = CheckSystemFile(thumbnailPath);
                m_thumbnailCache[project.name] = thumbnailExists;
              }

              TexturePtr thumbTexture = nullptr;
              if (thumbnailExists)
              {
                thumbTexture = GetTextureManager()->Create<Texture>(thumbnailPath);
                if (thumbTexture)
                {
                  thumbTexture->Init();
                }
              }

              if (!thumbTexture)
              {
                thumbTexture = m_defaultProjectThumbnail;
              }

              if (thumbTexture && Renderer::GetNativeTextureHandle(thumbTexture) != 0)
              {
                childDrawList->AddImageRounded(Convert2ImGuiTexture(thumbTexture),
                                               imagePos,
                                               imageMax,
                                               ImVec2(0, 1),
                                               ImVec2(1, 0),
                                               ImGui::GetColorU32(ImVec4(1, 1, 1, 1)),
                                               4.0f);
              }
              else
              {
                childDrawList->AddRectFilled(imagePos, imageMax, ImGui::GetColorU32(ImGuiCol_Button), 4.0f);
              }

              // Project name text.
              float textWidth = ImGui::CalcTextSize(projectName).x;
              float textY     = itemPos.y + imageHeight + 2.0f;
              ImVec2 textPos  = ImVec2(itemPos.x + (cardSize - textWidth) * 0.5f, textY);
              childDrawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), projectName);

              ImGui::SameLine(0.0f, cardSpacing);

              ImGui::PopID();
            }

            if (numProjects == 0)
            {
              ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }

            ImGui::EndChild();
          }
          else if (m_workspace)
          {
            m_selectedProjectIndex = -1;
            ImGui::SetCursorPos(ImVec2(panelPadding, listStartY));
            ImGui::SetCursorPosX(
                (panelWidth - ImGui::CalcTextSize("No projects found. Create a new project to get started.").x) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::Text("No projects found. Create a new project to get started.");
            ImGui::PopStyleColor();
          }

          // Tools panel.
          ImGui::SetCursorPos(ImVec2(panelPadding + projectsPanelWidth + panelSpacing, listStartY));
          ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg));
          ImGui::BeginChild("ToolsPanel", ImVec2(toolsPanelWidth, listHeight), true);
          ImGui::PopStyleColor();
          {
            float buttonWidth  = toolsPanelWidth - 20.0f;
            float buttonHeight = 28.0f;

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);

            bool hasSelection = m_workspace && m_selectedProjectIndex >= 0 &&
                                m_selectedProjectIndex < (int) m_workspace->m_projects.size();

            ImGui::BeginDisabled(!hasSelection);

            ImGui::SetCursorPosX((toolsPanelWidth - buttonWidth) * 0.5f);
            if (ImGui::Button("Launch", ImVec2(buttonWidth, buttonHeight)))
            {
              if (hasSelection)
              {
                OpenProject(m_workspace->m_projects[m_selectedProjectIndex]);
              }
            }

            ImGui::SetCursorPosX((toolsPanelWidth - buttonWidth) * 0.5f);
            if (ImGui::Button("Open in Folder", ImVec2(buttonWidth, buttonHeight)))
            {
              if (hasSelection)
              {
                const Project& selected      = m_workspace->m_projects[m_selectedProjectIndex];
                String projectFolder         = ConcatPaths({m_workspace->GetActiveWorkspace(), selected.name});

                ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
                if (platform_io.Platform_OpenInShellFn)
                {
                  platform_io.Platform_OpenInShellFn(ImGui::GetCurrentContext(), projectFolder.c_str());
                }
              }
            }

            if (m_createProjectShortcutOnDesktopFn)
            {
              ImGui::SetCursorPosX((toolsPanelWidth - buttonWidth) * 0.5f);
              if (ImGui::Button("Create Shortcut", ImVec2(buttonWidth, buttonHeight)))
              {
                if (hasSelection)
                {
                  const Project& selected = m_workspace->m_projects[m_selectedProjectIndex];
                  String workspacePath    = m_workspace->GetActiveWorkspace();
                  UnixifyPath(workspacePath);
                  String args = "--workspace \"" + workspacePath + "\" --project-name \"" + selected.name + "\"";
                  m_createProjectShortcutOnDesktopFn(selected.name, args);
                }
              }
            }

            ImGui::EndDisabled();
          }
          ImGui::EndChild();

          // Bottom workspace panel.
          ImGui::SetCursorPosY(windowSize.y - bottomPanelHeight - bottomPadding);

          ImGui::SetCursorPosX(panelPadding);
          float workspacePanelWidth = panelWidth * 0.5f;
          ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg));
          ImGui::BeginChild("WorkspacePanel", ImVec2(workspacePanelWidth, bottomPanelHeight), true);
          ImGui::PopStyleColor();

          float contentHeight  = ImGui::GetTextLineHeight();
          float verticalOffset = (bottomPanelHeight - contentHeight) * 0.5f;
          ImGui::SetCursorPosY(verticalOffset);

          HandleWorkspace();

          ImGui::EndChild();

          // New project button.
          if (m_workspace && m_workspace->IsWorkspaceSane(false, false))
          {
            float btnWidth  = 150.0f;
            float btnHeight = 35.0f;

            ImGui::SetCursorPosY(windowSize.y - btnHeight - bottomPadding);
            ImGui::SetCursorPosX(windowSize.x - btnWidth - panelPadding);

            if (ImGui::Button("+ New Project", ImVec2(btnWidth, btnHeight)))
            {
              m_showNewProjectPopup = true;
            }
          }

          ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
      }

      ShowWorkspacePopup();
      ShowNewProjectPopup();

      ImGui::PopFont();

      ImGui::PopStyleVar(4);

      ImGui::End();
    }

    void LauncherApp::HandleWorkspace()
    {
      if (!m_workspace)
      {
        return;
      }

      String currentWorkspace = m_workspace->GetActiveWorkspace();
      bool hasWorkspace       = !currentWorkspace.empty() && CheckSystemFile(currentWorkspace);

      float panelWidth        = ImGui::GetWindowWidth();
      float textPadding       = 10.0f;
      float buttonPadding     = 10.0f;

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
          m_workspacePathOnUI  = m_workspace->GetDefaultWorkspace();
        }
      }
      else
      {
        float editButtonWidth = 80.0f;
        float spacing         = ImGui::GetStyle().ItemSpacing.x;
        float textLabelWidth  = ImGui::CalcTextSize("Workspace:").x;
        float availableWidth =
            panelWidth - textLabelWidth - spacing - editButtonWidth - textPadding - buttonPadding - spacing;

        String displayPath = currentWorkspace;
        float pathWidth    = ImGui::CalcTextSize(displayPath.c_str()).x;

        if (pathWidth > availableWidth)
        {
          while (pathWidth > availableWidth && displayPath.length() > 0)
          {
            displayPath.pop_back();
            pathWidth = ImGui::CalcTextSize((displayPath + "...").c_str()).x;
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
          m_workspacePathOnUI  = currentWorkspace;
        }
      }
    }

    void LauncherApp::ShowWorkspacePopup()
    {
      if (!m_showWorkspacePopup)
        return;

      ImGuiViewport* vp = ImGui::GetMainViewport();
      ImVec2 center     = ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);

      float minWidth    = 500.0f;
      float minHeight   = 130.0f;

      ImGui::OpenPopup("Set Workspace");
      ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(minWidth, minHeight), ImGuiCond_Always);

      if (ImGui::BeginPopupModal("Set Workspace",
                                 &m_showWorkspacePopup,
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
      {
        float innerPad = 10.0f;

        ImGui::SetCursorPosX(innerPad);
        ImGui::Text("Workspace Path:");
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

        ImGui::SetCursorPosX(innerPad);
        ImGui::PushItemWidth(ImGui::GetWindowWidth() - innerPad * 2);
        bool enterPressed =
            ImGui::InputText("##workspacePath", &m_workspacePathOnUI, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);

        float buttonWidth   = 120.0f;
        float buttonSpacing = 15.0f;
        float totalWidth    = buttonWidth * 2 + buttonSpacing;
        float startX        = (ImGui::GetWindowWidth() - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(startX);

        if (ImGui::Button("OK", ImVec2(buttonWidth, 0)) || enterPressed)
        {
          if (!m_workspacePathOnUI.empty())
          {
            m_workspace->SetDefaultWorkspace(m_workspacePathOnUI);
            m_workspace->RefreshProjects();
            UpdateThumbnailCache();
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

    void LauncherApp::ShowNewProjectPopup()
    {
      if (!m_showNewProjectPopup)
        return;

      ImGuiViewport* vp = ImGui::GetMainViewport();
      ImVec2 center     = ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);

      float minWidth    = 500.0f;
      float minHeight   = 200.0f;

      ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(minWidth, minHeight), ImGuiCond_Appearing);
      ImGui::OpenPopup("New Project");

      if (ImGui::BeginPopupModal("New Project",
                                 &m_showNewProjectPopup,
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
      {
        float innerPad = 15.0f;

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);

        ImGui::SetCursorPosX(innerPad);
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

        if (m_newProjectTabLocal)
        {
          ImGui::SetCursorPosX(innerPad);
          ImGui::Text("Project Name:");
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

          ImGui::SetCursorPosX(innerPad);
          ImGui::PushItemWidth(ImGui::GetWindowWidth() - innerPad * 2);

          bool projectNameExists  = false;
          bool projectNameInvalid = false;
          if (m_workspace && !m_newProjectName.empty())
          {
            for (const Project& project : m_workspace->m_projects)
            {
              if (project.name == m_newProjectName)
              {
                projectNameExists = true;
                break;
              }
            }
            if (!projectNameExists)
            {
              projectNameInvalid = !IsValidCppLibraryName(m_newProjectName);
            }
          }

          if (projectNameExists || projectNameInvalid)
          {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
          }

          enterPressed = ImGui::InputText("##newProjectName", &m_newProjectName, ImGuiInputTextFlags_EnterReturnsTrue);

          if (projectNameExists || projectNameInvalid)
          {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered())
            {
              if (projectNameExists)
              {
                ImGui::SetTooltip("There is already a project with this name!");
              }
              else
              {
                ImGui::SetTooltip("%s", g_validLibraryNameRules.c_str());
              }
            }
          }

          ImGui::PopItemWidth();
        }
        else
        {
          ImGui::SetCursorPosX(innerPad);
          ImGui::Text("Git URL:");
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

          ImGui::SetCursorPosX(innerPad);
          ImGui::PushItemWidth(ImGui::GetWindowWidth() - innerPad * 2);
          enterPressed =
              ImGui::InputText("##newProjectUrl", &m_newProjectPathOrUrl, ImGuiInputTextFlags_EnterReturnsTrue);
          ImGui::PopItemWidth();
        }

        if (m_isCloning)
        {
          ImGui::EndDisabled();
        }

        if (m_isCloning)
        {
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);
          ImGui::SetCursorPosX(innerPad);
          ImGui::Text("Cloning repository...");
        }

        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth   = 120.0f;
        float buttonSpacing = 15.0f;
        float totalWidth    = buttonWidth * 2 + buttonSpacing;
        float startX        = (ImGui::GetWindowWidth() - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(startX);

        bool projectNameExists  = false;
        bool projectNameInvalid = false;
        if (m_workspace && !m_newProjectName.empty() && m_newProjectTabLocal)
        {
          for (const Project& project : m_workspace->m_projects)
          {
            if (project.name == m_newProjectName)
            {
              projectNameExists = true;
              break;
            }
          }
          if (!projectNameExists)
          {
            projectNameInvalid = !IsValidCppLibraryName(m_newProjectName);
          }
        }

        bool createDisabled = (m_isCloning || projectNameExists || projectNameInvalid);
        if (createDisabled)
        {
          ImGui::BeginDisabled();
        }

        bool createClicked = ImGui::Button("Create", ImVec2(buttonWidth, 0));

        if (createDisabled)
        {
          ImGui::EndDisabled();
        }

        bool shouldCreate = (createClicked || (enterPressed && !m_isCloning && !projectNameExists));
        if (shouldCreate)
        {
          bool canCreate     = false;
          String projectName = m_newProjectName;

          if (m_newProjectTabLocal)
          {
            canCreate = !m_newProjectName.empty() && IsValidCppLibraryName(m_newProjectName);
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
              size_t startPos =
                  (lastSlash != String::npos) ? lastSlash + 1 : ((lastColon != String::npos) ? lastColon + 1 : 0);
              if (startPos < url.size())
              {
                projectName = url.substr(startPos);
                canCreate   = !projectName.empty() && IsValidCppLibraryName(projectName);
              }
            }
          }

          if (canCreate)
          {
            if (!m_newProjectTabLocal)
            {
              m_isCloning          = true;
              m_cloneProgress      = "Starting git clone...";

              String workspacePath = m_workspace->GetActiveWorkspace();
              String targetPath    = ConcatPaths({workspacePath, projectName});
              String gitCmd        = "git clone \"" + m_newProjectPathOrUrl + "\" \"" + targetPath + "\"";

              m_sysComExecFn(gitCmd,
                             true,
                             false,
                             [this](int exitCode) -> void
                             {
                               m_isCloning = false;
                               if (exitCode == 0)
                               {
                                 m_cloneProgress = "Clone completed successfully!";
                                 m_workspace->RefreshProjects();
                                 UpdateThumbnailCache();
                                 m_showNewProjectPopup = false;
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
              bool result = m_workspace->OnNewProject(projectName);
              if (result)
              {
                g_launcherRunning = false;
                m_workspace->SetActiveProject({projectName, ""});
              }
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

        ImGui::EndPopup();
      }
    }

  } // namespace Launcher
} // namespace ToolKit
