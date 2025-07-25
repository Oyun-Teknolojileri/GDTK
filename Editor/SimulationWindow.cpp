/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "SimulationWindow.h"

#include "App.h"
#include "EditorViewport2d.h"
#include "PublishManager.h"

#include <UIManager.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(SimulationWindow, Window);

    SimulationWindow::SimulationWindow() : m_numDefaultResNames((int) m_emulatorResolutionNames.size())
    {
      m_name     = "Simulation";
      m_settings = &GetApp()->m_simulatorSettings;
    }

    SimulationWindow::~SimulationWindow() {}

    void SimulationWindow::AddResolutionName(const String& name)
    {
      m_emulatorResolutionNames.push_back(name);
      m_screenResolutions.push_back(Vec2(500.0f, 500.0f));
    }

    void SimulationWindow::RemoveResolutionName(size_t index)
    {
      bool canRemove = index > 0 || index < m_screenResolutions.size();
      assert(canRemove && "resolution index invalid");

      if (canRemove)
      {
        m_screenResolutions.erase(m_screenResolutions.begin() + index);
        m_emulatorResolutionNames.erase(m_emulatorResolutionNames.begin() + index);
      }
    }

    void SimulationWindow::RemoveResolutionName(const String& name)
    {
      for (int i = 0; i < m_emulatorResolutionNames.size(); i++)
      {
        if (name == m_emulatorResolutionNames[i])
        {
          RemoveResolutionName(i);
          return;
        }
      }
      TK_WRN("Resolution name does not exist.");
    }

    void SimulationWindow::Show()
    {
      ImGui::SetNextWindowSize(ImVec2(350, 150), ImGuiCond_Once);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {5, 5});

      if (ImGui::Begin(m_name.c_str(), &m_visible, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
      {
        HandleStates();
        ShowActionButtons();
        ShowHeader();
        ShowSettings();
      }

      ImGui::End();
      ImGui::PopStyleVar();
    }

    XmlNode* SimulationWindow::SerializeImp(XmlDocument* doc, XmlNode* parent) const
    {
      parent           = Super::SerializeImp(doc, parent);
      XmlNode* simNode = CreateXmlNode(doc, "Simulation", parent);
      XmlNode* resNode = CreateXmlNode(doc, "Resolution", simNode);

      int numCustomRes = (int) m_screenResolutions.size() - m_numDefaultResNames;
      for (int i = 0; i < numCustomRes; i++)
      {
        String istr = std::to_string(i);
        int index   = i + m_numDefaultResNames;

        WriteAttr(resNode, doc, "name", m_emulatorResolutionNames[index]);
        WriteAttr(resNode, doc, "sizeX", std::to_string(m_screenResolutions[index].x));
        WriteAttr(resNode, doc, "sizeY", std::to_string(m_screenResolutions[index].y));
      }

      return simNode;
    }

    XmlNode* SimulationWindow::DeSerializeImp(const SerializationFileInfo& info, XmlNode* parent)
    {
      XmlNode* simNode = Super::DeSerializeImp(info, parent);
      simNode          = simNode->first_node("Simulation");
      if (simNode == nullptr)
      {
        return nullptr;
      }

      m_screenResolutions.erase(m_screenResolutions.begin() + m_numDefaultResNames, m_screenResolutions.end());
      m_emulatorResolutionNames.erase(m_emulatorResolutionNames.begin() + m_numDefaultResNames,
                                      m_emulatorResolutionNames.end());

      XmlNode* resNode = simNode->first_node("Resolution");
      while (resNode != nullptr)
      {
        String name;
        ReadAttr(resNode, "name", name);

        IVec2 res;
        ReadAttr(resNode, "sizeX", res.x);
        ReadAttr(resNode, "sizeY", res.y);

        m_screenResolutions.push_back(res);
        m_emulatorResolutionNames.push_back(name);
        resNode = resNode->next_sibling();
      }

      return simNode;
    }

    void SimulationWindow::UpdateSimulationWndSize()
    {
      if (GetApp()->m_simulationViewport)
      {
        uint width  = uint(m_settings->Width * m_settings->Scale);
        uint height = uint(m_settings->Height * m_settings->Scale);
        if (m_settings->Landscape)
        {
          std::swap(width, height);
        }

        GetApp()->m_simulationViewport->ResizeWindow(width, height);
        UpdateCanvas(width, height);
      }
    }

    void SimulationWindow::ShowHeader()
    {
      if (m_simulationModeDisabled)
      {
        ImGui::BeginDisabled(m_simulationModeDisabled);
      }

      if (ImGui::Button(ICON_FA_SLIDERS, ImVec2(26.0f, 26.0f)))
      {
        m_settings->Windowed = !m_settings->Windowed;
      }

      if (m_simulationModeDisabled)
      {
        ImGui::EndDisabled();
      }
      ImGui::SameLine();
    }

    static void GreenTint()
    {
      ImGui::PushStyleColor(ImGuiCol_Button, g_blueTintButtonColor);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_blueTintButtonHoverColor);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_blueTintButtonActiveColor);
    }

    static void RedTint()
    {
      ImGui::PushStyleColor(ImGuiCol_Button, g_redTintButtonColor);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_redTintButtonHoverColor);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_redTintButtonActiveColor);
    }

    void SimulationWindow::ShowActionButtons()
    {
      // Draw play - pause - stop buttons.
      ImVec2 btnSize = ImVec2(20.0f, 20.0f);
      // pick middle point of the window and
      // move left half of the width of action buttons(250.0f)
      float offset   = glm::max(ImGui::GetWindowWidth() * 0.5f - 100.0f, 0.0f);
      ImGui::SetCursorPosX(offset);

      if (GetApp()->m_gameMod == GameMod::Playing)
      {
        GreenTint();
        // Pause.
        if (ImGui::ImageButton("##pause", Convert2ImGuiTexture(UI::m_pauseIcon), btnSize))
        {
          GetApp()->SetGameMod(GameMod::Paused);
        }

        ImGui::PopStyleColor(3);
      }
      else
      {
        GreenTint();
        // Play.
        if (ImGui::ImageButton("##play", Convert2ImGuiTexture(UI::m_playIcon), btnSize) && !GetApp()->IsCompiling())
        {
          m_simulationModeDisabled = true;
          GetApp()->SetGameMod(GameMod::Playing);
        }

        ImGui::PopStyleColor(3);
      }
      // Stop.
      ImGui::SameLine();
      RedTint();

      if (ImGui::ImageButton("##stop", Convert2ImGuiTexture(UI::m_stopIcon), btnSize))
      {
        if (GetApp()->m_gameMod != GameMod::Stop)
        {
          m_simulationModeDisabled = false;
          GetApp()->SetGameMod(GameMod::Stop);
        }
      }

      ImGui::PopStyleColor(3);
      ImGui::SameLine();

      // VS Code.
      if (ImGui::ImageButton("##vscode", Convert2ImGuiTexture(UI::m_vsCodeIcon), btnSize))
      {
        String codePath = ConcatPaths({GetApp()->m_workspace.GetCodeDirectory(), "..", "."});
        if (CheckFile(codePath))
        {
          String cmd = "code \"" + codePath + "\"";
          int result = GetApp()->ExecSysCommand(cmd, true, false);
          if (result != 0)
          {
            TK_ERR("Visual Studio Code can't be started. Make sure it is installed.", );
          }
        }
        else
        {
          TK_ERR("There is not a vaild code folder.");
        }
      }

      // Build.
      ImGui::SameLine();

      if (ImGui::ImageButton("##build", Convert2ImGuiTexture(UI::m_buildIcn), btnSize))
      {
        String buildBat         = GetApp()->m_workspace.GetCodeDirectory();
        PublishConfig buildType = TKDebug ? PublishConfig::Debug : PublishConfig::Develop;
        GetApp()->m_publishManager->Publish(PublishPlatform::GamePlugin, buildType);
      }

      UI::HelpMarker(TKLoc, "Build\nBuilds the projects code files.");
      ImGui::SameLine();
    }

    String SimulationWindow::EmuResToString(EmulatorResolution emuRes)
    {
      return m_emulatorResolutionNames[(uint) emuRes];
    }

    void SimulationWindow::ShowSettings()
    {
      if (!m_settings->Windowed)
      {
        return;
      }

      // Resolution Bar
      EmulatorResolution resolution = m_settings->Resolution;
      int resolutionType            = (int) resolution;
      resolutionType                = glm::min(resolutionType, (int) m_screenResolutions.size() - 1);

      float textWidth               = ImGui::CalcTextSize(m_emulatorResolutionNames[resolutionType].data()).x;
      textWidth                     = glm::max(80.0f, textWidth);
      ImGui::SetNextItemWidth(textWidth * 1.3f);

      AddResolutionName("Edit Resolutions");

      int lastEnumIndex = (int) m_emulatorResolutionNames.size() - 1;

      // in order to send to imgui we should convert to ptr array
      std::vector<char*> enumNames;
      for (size_t i = 0ull; i <= lastEnumIndex; i++)
      {
        enumNames.push_back(&m_emulatorResolutionNames[i][0]);
      }

      if (ImGui::Combo("##Resolution", &resolutionType, enumNames.data(), (int) enumNames.size()))
      {
        if (resolutionType == lastEnumIndex)
        {
          m_resolutionSettingsWindowEnabled = true;
          ImGui::SetNextWindowPos(ImGui::GetMousePos());
        }
        else
        {
          EmulatorResolution resolution = static_cast<EmulatorResolution>(resolutionType);

          IVec2 resolutionSize          = m_screenResolutions[resolutionType];

          m_settings->Width             = (float) resolutionSize.x;
          m_settings->Height            = (float) resolutionSize.y;

          m_settings->Resolution        = resolution;
          UpdateSimulationWndSize();
        }
      }
      RemoveResolutionName(lastEnumIndex);

      if (m_resolutionSettingsWindowEnabled)
      {
        ImGui::SetNextWindowSizeConstraints(Vec2(400.0f, 0.0f), Vec2(TK_FLT_MAX));
        ImGui::Begin("Edit Resolutions",
                     &m_resolutionSettingsWindowEnabled,
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_AlwaysAutoResize);

        for (int i = m_numDefaultResNames; i < (int) m_emulatorResolutionNames.size(); i++)
        {
          ImGui::PushID(i * 333);
          ImGui::InputText("name", m_emulatorResolutionNames[i].data(), 32);
          ImGui::SameLine();

          if (ImGui::InputInt2("size", &m_screenResolutions[i].x))
          {
            IVec2 res          = m_screenResolutions[i];
            res.x              = glm::clamp(res.x, 100, 1920 * 8);
            res.y              = glm::clamp(res.y, 100, 1080 * 8);

            m_settings->Width  = (float) res.x;
            m_settings->Height = (float) res.y;
            UpdateSimulationWndSize();
          }

          ImGui::SameLine();
          if (ImGui::Button(ICON_FA_MINUS))
          {
            RemoveResolutionName(i);
          }
          ImGui::PopID();
        }

        ImGui::Text("Add New");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLUS))
        {
          AddResolutionName("new resolution\0");
        }

        ImGui::End();
      }

      // Zoom
      ImGui::SameLine();
      ImGui::Text("Scale");
      ImGui::SetNextItemWidth(120.0f);
      ImGui::SameLine();

      if (ImGui::SliderFloat("##z", &m_settings->Scale, 0.5f, 2.0f, "%.1f"))
      {
        UpdateSimulationWndSize();
      }

      // Landscape - Portrait Toggle
      ImGui::SameLine();
      ImGui::Text("Rotate");
      ImGui::SameLine();

      if (ImGui::ImageButton("##rotate", Convert2ImGuiTexture(UI::m_phoneRotateIcon), ImVec2(20, 20)))
      {
        m_settings->Landscape = !m_settings->Landscape;
        UpdateSimulationWndSize();
      }
    }

    void SimulationWindow::UpdateCanvas(uint width, uint height)
    {
      if (EditorViewport2dPtr viewport = GetApp()->GetWindow<EditorViewport2d>(g_2dViewport))
      {
        UILayerPtrArray layers;
        GetUIManager()->GetLayers(viewport->m_viewportId, layers);

        // If this is 2d view, warn the user about no layer.
        if (layers.empty())
        {
          if (GetApp()->GetActiveViewport() == viewport)
          {
            if (viewport->IsShown())
            {
              GetApp()->SetStatusMsg("Resize Failed. No Layer !");
            }
          }
        }

        for (const UILayerPtr& layer : layers)
        {
          layer->ResizeUI(Vec2((float) width, (float) height));
        }
      }
    }

  } // namespace Editor
} // namespace ToolKit
