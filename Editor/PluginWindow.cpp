/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "PluginWindow.h"

#include "App.h"
#include "Workspace.h"

#include <Plugin.h>
#include <PluginManager.h>

namespace ToolKit
{
  namespace Editor
  {

    // PluginSettingsWindow
    //////////////////////////////////////////

    TKDefineClass(PluginSettingsWindow, Window);

    PluginSettingsWindow::PluginSettingsWindow() { m_name = "PluginSettings"; }

    void PluginSettingsWindow::Show()
    {
      ImGui::SetNextWindowSize(ImVec2(400, 375), ImGuiCond_Once);

      if (ImGui::Begin("Plugin Settings", &m_visible))
      {
        // Calculate available space for content and buttons
        float availableHeight = ImGui::GetContentRegionAvail().y;
        float buttonHeight    = ImGui::GetFrameHeightWithSpacing();
        float contentHeight   = availableHeight - buttonHeight - ImGui::GetStyle().ItemSpacing.y;

        // Editable fields for PluginSettings
        static char buffer[2048];

        if (ImGui::BeginChild("SettingsContent", ImVec2(0, contentHeight), false))
        {
          // Plugin data
          ImGui::SeparatorText("Plugin");

          ImGui::BeginDisabled();
          strncpy(buffer, m_bckup.name.c_str(), IM_ARRAYSIZE(buffer));
          ImGui::InputText("Name", buffer, IM_ARRAYSIZE(buffer));
          m_bckup.name = buffer;
          ImGui::EndDisabled();

          strncpy(buffer, m_bckup.brief.c_str(), IM_ARRAYSIZE(buffer));
          ImGui::InputTextMultiline("Brief",
                                    buffer,
                                    IM_ARRAYSIZE(buffer),
                                    ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4));
          m_bckup.brief = buffer;

          strncpy(buffer, m_bckup.version.c_str(), IM_ARRAYSIZE(buffer));
          ImGui::InputText("Version", buffer, IM_ARRAYSIZE(buffer));
          m_bckup.version = buffer;

          strncpy(buffer, m_bckup.engine.c_str(), IM_ARRAYSIZE(buffer));
          ImGui::InputText("Engine", buffer, IM_ARRAYSIZE(buffer));
          m_bckup.engine = buffer;

          // Developer data
          ImGui::SeparatorText("Developer");

          strncpy(buffer, m_bckup.developer.c_str(), IM_ARRAYSIZE(buffer));
          ImGui::InputText("Developer", buffer, IM_ARRAYSIZE(buffer));
          m_bckup.developer = buffer;

          strncpy(buffer, m_bckup.web.c_str(), IM_ARRAYSIZE(buffer));
          ImGui::InputText("Web", buffer, IM_ARRAYSIZE(buffer));
          m_bckup.web = buffer;

          strncpy(buffer, m_bckup.email.c_str(), IM_ARRAYSIZE(buffer));
          ImGui::InputText("Email", buffer, IM_ARRAYSIZE(buffer));
          m_bckup.email = buffer;

          ImGui::EndChild();
        }

        // Buttons at the bottom with margin
        if (ImGui::Button("Save"))
        {
          String file = PluginConfigPath(m_bckup.name);
          m_bckup.Save(file);
          *m_settings = m_bckup;
          RemoveFromUI();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel"))
        {
          RemoveFromUI();
        }
      }

      ImGui::End();
    }

    void PluginSettingsWindow::SetPluginSettings(PluginSettings* settings)
    {
      m_bckup    = *settings;
      m_settings = settings;
    }

    // PluginWindow
    //////////////////////////////////////////

    TKDefineClass(PluginWindow, Window);

    PluginWindow::PluginWindow()
    {
      m_name = g_pluginWindow;
      LoadPluginSettings();
    }

    void PluginWindow::Show()
    {
      ImGui::SetNextWindowSize(ImVec2(470, 110), ImGuiCond_Once);
      if (ImGui::Begin(m_name.c_str(), &m_visible))
      {
        HandleStates();

        // Calculate available space for the table
        float availableHeight = ImGui::GetContentRegionAvail().y;
        float buttonHeight    = ImGui::GetFrameHeightWithSpacing();
        float tableHeight     = availableHeight - buttonHeight - ImGui::GetStyle().ItemSpacing.y;

        if (ImGui::BeginTable("table1",
                              6,
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH |
                                  ImGuiTableFlags_BordersOuterV | ImGuiTableFlags_ScrollY,
                              ImVec2(0, tableHeight)))
        {
          ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 0);
          ImGui::TableSetupColumn("Brief", ImGuiTableColumnFlags_None, 0);
          ImGui::TableSetupColumn("Load", ImGuiTableColumnFlags_None, 0);
          ImGui::TableSetupColumn("Compile", ImGuiTableColumnFlags_None, 0);
          ImGui::TableSetupColumn("Folder", ImGuiTableColumnFlags_None, 0);
          ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_None, 0);
          ImGui::TableHeadersRow();
          ImGui::TableNextRow(0, 0);
          ImGui::TableSetColumnIndex(0);

          Vec2 btnSize   = Vec2(20.0f);
          int imPluginId = 0;
          for (PluginSettings& plugin : m_pluginSettings)
          {
            ImGui::PushID(imPluginId++);

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text(plugin.name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(plugin.brief.c_str());
            ImGui::PopTextWrapPos();

            ImGui::TableSetColumnIndex(2);
            ImGui::AlignTextToFramePadding();

            PluginManager* plugMan = GetPluginManager();
            String fullPath        = plugin.file + GetPluginExtention();
            PluginRegister* reg    = plugMan->GetRegister(fullPath);
            bool isLoaded          = reg != nullptr && reg->m_loaded;

            if (ImGui::Checkbox("##Load", &isLoaded))
            {
              EngineSettings& settings = GetEngineSettings();
              if (!isLoaded)
              {
                plugMan->Unload(fullPath);
                settings.m_loadedPlugins.erase(plugin.name);
                GetApp()->SetStatusMsg(g_statusSucceeded);
              }
              else
              {
                if (reg = plugMan->Load(plugin.file))
                {
                  reg->m_plugin->m_currentState = PluginState::Running;
                  settings.m_loadedPlugins.insert(plugin.name);
                  GetApp()->SetStatusMsg(g_statusSucceeded);
                }
                else
                {
                  GetApp()->SetStatusMsg("Failed to load plugin. Is it compiled ?");
                }
              }
            }
            UI::AddTooltipToLastItem("Loads or unloads the plugin.\n"
                                     "Save the engine settings to preserve plugin loaded state.\n"
                                     "This may cause crashes, save your work before.");

            ImGui::TableSetColumnIndex(3);
            ImGui::AlignTextToFramePadding();
            if (UI::ImageButton("##build", Convert2ImGuiTexture(UI::m_buildIcn), btnSize))
            {
              GetApp()->CompilePlugin(plugin.name, false);
            }
            UI::AddTooltipToLastItem("If a change is detected, compiles and reloads the plugin.\n"
                                     "This may cause crashes, save your work before.");

            ImGui::TableSetColumnIndex(4);
            ImGui::AlignTextToFramePadding();
            if (UI::ImageButton("##folder", Convert2ImGuiTexture(UI::m_folderIcon), btnSize))
            {
              String pluginDir = GetApp()->m_workspace->GetPluginDirectory();
              String dir       = ConcatPaths({pluginDir, plugin.name, "Codes"});
              GetApp()->m_shellOpenDirFn(dir);
            }
            UI::AddTooltipToLastItem("Show plugin folder in file explorer.");

            ImGui::TableSetColumnIndex(5);
            ImGui::AlignTextToFramePadding();
            if (UI::ImageButton("##code", Convert2ImGuiTexture(UI::m_codeIcon), btnSize))
            {
              PluginSettingsWindowPtr settingsWnd = MakeNewPtr<PluginSettingsWindow>();
              settingsWnd->SetPluginSettings(&plugin);
              settingsWnd->AddToUI();
            }

            ImGui::PopID();
          }

          ImGui::EndTable();
        }

        // Button at the bottom with margin
        if (ImGui::Button("Refresh"))
        {
          LoadPluginSettings();
        }
      }
      ImGui::End();
    }

    void PluginWindow::LoadPluginSettings()
    {
      m_pluginSettings.clear();
      String pluginDir = GetApp()->m_workspace->GetPluginDirectory();

      if (CheckSystemFile(pluginDir) && IsDirectory(pluginDir))
      {
        namespace fs = std::filesystem;
        for (const fs::directory_entry& entry : fs::directory_iterator(pluginDir))
        {
          String path    = entry.path().u8string();
          String cfgFile = ConcatPaths({path, "Config", "Plugin.settings"});
          if (CheckSystemFile(cfgFile))
          {
            PluginSettings pluginSet;
            pluginSet.Load(cfgFile);

            m_pluginSettings.emplace_back(pluginSet);
          }
        }
      }
      else
      {
        TK_ERR("Can not traverse Plugins directory.");
      }
    }

    void PluginWindow::LoadEnabledPlugins()
    {
      const EngineSettings& settings = GetEngineSettings();
      for (const PluginSettings& plugin : m_pluginSettings)
      {
        if (settings.m_loadedPlugins.find(plugin.name) != settings.m_loadedPlugins.end())
        {
          PluginManager* plugMan = GetPluginManager();
          String fullPath        = plugin.file + GetPluginExtention();
          PluginRegister* reg    = plugMan->GetRegister(fullPath);
          bool isLoaded          = reg != nullptr && reg->m_loaded;

          if (reg = plugMan->Load(plugin.file))
          {
            reg->m_plugin->m_currentState = PluginState::Running;
          }
          else
          {
            TK_ERR("Failed to load plugin: %s. Is it compiled ?", plugin.name.c_str());
          }
        }
      }
    }

    void PluginWindow::UnloadProjectPlugins()
    {
      for (const PluginSettings& plugin : m_pluginSettings)
      {
        PluginManager* plugMan = GetPluginManager();
        String fullPath        = plugin.file + GetPluginExtention();
        plugMan->Unload(fullPath);
      }
    }

  } // namespace Editor
} // namespace ToolKit