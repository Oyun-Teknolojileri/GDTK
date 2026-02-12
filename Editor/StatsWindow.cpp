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

    // Helper function to recursively draw profiler nodes in the table.
    static void DrawProfilerNode(ProfilerNode* node, float frameTime, int depth = 0)
    {
      if (node == nullptr)
      {
        return;
      }

      // Use previous frame values for display (current frame may still be recording).
      float inclTime    = node->inclusiveTimePrev;
      float exclTime    = node->exclusiveTimePrev;
      uint hitCount     = node->hitCountPrev;

      // Calculate percentages.
      float inclPercent = frameTime > 0.0f ? (inclTime / frameTime) * 100.0f : 0.0f;
      float exclPercent = frameTime > 0.0f ? (exclTime / frameTime) * 100.0f : 0.0f;

      ImGui::TableNextRow();

      // Column 0: Scope Name with tree structure.
      ImGui::TableNextColumn();

      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
      if (node->children.empty())
      {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
      }

      if (node->expanded)
      {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
      }

      bool isOpen = ImGui::TreeNodeEx(node->name.c_str(), flags);

      // Update expansion state.
      if (!node->children.empty())
      {
        node->expanded = isOpen;
      }

      // Column 1: Inclusive Time (ms).
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", inclTime);

      // Column 2: Exclusive Time (ms).
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", exclTime);

      // Column 3: Inclusive %.
      ImGui::TableNextColumn();

      // Color code based on percentage.
      ImVec4 color = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green.
      if (inclPercent > 50.0f)
      {
        color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // Red.
      }
      else if (inclPercent > 25.0f)
      {
        color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f); // Orange.
      }
      else if (inclPercent > 10.0f)
      {
        color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow.
      }

      ImGui::TextColored(color, "%.2f%%", inclPercent);

      // Column 4: Exclusive %.
      ImGui::TableNextColumn();
      ImGui::TextColored(color, "%.2f%%", exclPercent);

      // Column 5: Hit Count.
      ImGui::TableNextColumn();
      ImGui::Text("%u", hitCount);

      // Recursively draw children if expanded.
      if (isOpen && !node->children.empty())
      {
        for (ProfilerNode* child : node->children)
        {
          DrawProfilerNode(child, frameTime, depth + 1);
        }
        ImGui::TreePop();
      }
    }

    void StatsWindow::Show()
    {
      TKStats* tkStats = GetTKStats();
      if (tkStats == nullptr)
      {
        return;
      }

      ImGui::SetNextWindowSize(ImVec2(750, 450), ImGuiCond_Once);
      if (ImGui::Begin(m_name.c_str(), &m_visible))
      {
        HandleStates();

        // GPU Timer checkbox.
        bool gpuTimer = GetEngineSettings().m_graphics->GetEnableGpuTimerVal();
        if (ImGui::Checkbox("Capture Gpu Time##GpuProfileOn", &gpuTimer))
        {
          GetEngineSettings().m_graphics->SetEnableGpuTimerVal(gpuTimer);
        }
        UI::AddTooltipToLastItem("Enable to see the gpu frame time.\nHave a negative impact on cpu performance.");

        ImGui::SameLine();

        // Profiler enable checkbox.
        bool profilerEnabled = Profiler::IsProfilerEnabled();
        if (ImGui::Checkbox("Enable Profiler##ProfilerOn", &profilerEnabled))
        {
          Profiler::SetProfilerEnabled(profilerEnabled);
        }
        UI::AddTooltipToLastItem("Enable hierarchical CPU profiler.\nShows nested timing information.");

        ImGui::SameLine();

        // Reset profiler button.
        if (ImGui::Button("Reset Profiler"))
        {
          Profiler::ResetProfiler();
        }

        ImGui::Separator();

        // Viewport resolution.
        if (EditorViewportPtr viewport = GetApp()->GetViewport(g_3dViewport))
        {
          ImGui::Text("Viewport Resolution: %dx%d",
                      (int) viewport->m_wndContentAreaSize.x,
                      (int) viewport->m_wndContentAreaSize.y);
        }

        // Basic stats.
        String stats = tkStats->GetPerFrameStats();
        ImGui::TextUnformatted(stats.c_str());

        ImGui::Separator();

        // Profiler section.
        if (profilerEnabled)
        {
          TKProfiler& profiler           = tkStats->GetProfiler();
          const ProfilerNodeArray& roots = profiler.GetRootNodes();

          float frameTime                = profiler.GetFrameTime();
          float avgFrameTime             = profiler.GetAverageFrameTime();
          uint frameCount                = profiler.GetFrameCount();

          // Profiler summary header.
          ImGui::Text("Profiler: Frame: %.3f ms | Avg: %.3f ms | Frames: %u", frameTime, avgFrameTime, frameCount);

          // Expand/Collapse all buttons.
          if (ImGui::Button("Expand All"))
          {
            profiler.SetExpandAll(true);
          }
          ImGui::SameLine();
          if (ImGui::Button("Collapse All"))
          {
            profiler.SetExpandAll(false);
          }

          ImGui::Separator();

          // Profiler table.
          if (!roots.empty())
          {
            ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

            // Calculate available height for the table.
            float availableHeight = ImGui::GetContentRegionAvail().y;

            if (ImGui::BeginTable("ProfilerTable", 6, tableFlags, ImVec2(0.0f, availableHeight)))
            {
              // Setup columns.
              ImGui::TableSetupColumn("Scope Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
              ImGui::TableSetupColumn("Incl (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
              ImGui::TableSetupColumn("Excl (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
              ImGui::TableSetupColumn("Incl %", ImGuiTableColumnFlags_WidthFixed, 60.0f);
              ImGui::TableSetupColumn("Excl %", ImGuiTableColumnFlags_WidthFixed, 60.0f);
              ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthFixed, 50.0f);
              ImGui::TableSetupScrollFreeze(0, 1); // Freeze header row.
              ImGui::TableHeadersRow();

              // Draw all root nodes.
              for (ProfilerNode* root : roots)
              {
                DrawProfilerNode(root, frameTime);
              }

              ImGui::EndTable();
            }
          }
          else
          {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                               "No profiling data. Use TK_PROFILE_SCOPE(\"name\") or Stats::BeginProfileScope().");
          }
        }
        else
        {
          ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Profiler is disabled. Enable it to see timing data.");
        }
      }
      ImGui::End();
    }

  } // namespace Editor
} // namespace ToolKit
