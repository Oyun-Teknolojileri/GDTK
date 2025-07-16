/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "StatsWindow.h"

#include "App.h"
#include "EditorTypes.h"
#include "EditorViewport.h"

#include <EngineSettings.h>
#include <Stats.h>

namespace ToolKit
{
  namespace Editor
  {

    TKDefineClass(StatsWindow, Window);

    StatsWindow::StatsWindow() { m_name = g_statsView; }

    StatsWindow::~StatsWindow() {}

    void StatsWindow::Show()
    {
      TKStats* tkStats = GetTKStats();
      if (tkStats == nullptr)
      {
        return;
      }

      ImGui::SetNextWindowSize(ImVec2(270, 110), ImGuiCond_Once);
      if (ImGui::Begin(m_name.c_str(), &m_visible))
      {
        HandleStates();

        bool gpuTimer = GetEngineSettings().m_graphics->GetEnableGpuTimerVal();
        if (ImGui::Checkbox("Capture Gpu Time##GpuProfileOn", &gpuTimer))
        {
          GetEngineSettings().m_graphics->SetEnableGpuTimerVal(gpuTimer);
        }

        UI::AddTooltipToLastItem("Enable to see the gpu frame time.\nHave a negative impact on cpu performance.");

        EditorViewportPtr viewport = GetApp()->GetViewport(g_3dViewport);
        ImGui::Text("Viewport Resolution: %dx%d",
                    (int) viewport->m_wndContentAreaSize.x,
                    (int) viewport->m_wndContentAreaSize.y);

        String stats = tkStats->GetPerFrameStats();
        ImGui::TextUnformatted(stats.c_str());
      }
      ImGui::End();
    }

  } // namespace Editor
} // namespace ToolKit
