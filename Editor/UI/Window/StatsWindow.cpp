/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "StatsWindow.h"

#include "App.h"
#include "EditorTypes.h"
#include "EditorViewport.h"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

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
      float inclTime = node->inclusiveTimePrev;
      float exclTime = node->exclusiveTimePrev;
      uint hitCount  = node->hitCountPrev;

      // Skip inactive nodes. If parent has 0 hits, children are guaranteed to have 0 hits too.
      if (hitCount == 0)
      {
        return;
      }

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

      // Column 3: Inclusive % and Column 4: Exclusive %.
      ImGui::TableNextColumn();

      // Color code based on percentage. Reuses the console severity palette
      // (green = success, yellow = warning, red = error); orange is the
      // profiler specific mid threshold. Resolved per theme.
      ImVec4 color = UI::GetColor(EditorColor::ConsoleSuccess);
      if (inclPercent > 50.0f)
      {
        color = UI::GetColor(EditorColor::ConsoleError);
      }
      else if (inclPercent > 25.0f)
      {
        color = UI::GetColor(EditorColor::ConsoleOrange);
      }
      else if (inclPercent > 10.0f)
      {
        color = UI::GetColor(EditorColor::ConsoleWarning);
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

    /**
     * Draws the gpu time of each pass, biggest first.
     *
     * The gpu half of the frame is invisible to the cpu profiler above: a pass can submit in
     * microseconds and still fill the frame on the device. These come from timestamp queries the
     * backend records around every pass's debug group, so the table lists whatever the renderer runs,
     * including the shadow and pre process passes that the cpu tree merges away.
     */
    static void DrawGpuPassTable()
    {
      const std::unordered_map<String, float> times = Stats::GetPassGpuTimes();
      if (times.empty())
      {
        ImGui::TextColored(UI::GetColor(EditorColor::ConsoleMemo),
                           "No gpu timing. Either no pass rendered yet or this backend has no "
                           "timestamp queries.");
        return;
      }

      std::vector<std::pair<String, float>> sorted(times.begin(), times.end());
      std::sort(sorted.begin(),
                sorted.end(),
                [](const std::pair<String, float>& a, const std::pair<String, float>& b)
                { return a.second > b.second; });

      float frameCpu = 0.0f, frameGpu = 0.0f;
      Stats::GetRenderTime(frameCpu, frameGpu);

      float passesGpu = 0.0f;
      for (const auto& entry : sorted)
      {
        passesGpu += entry.second;
      }

      // The frame time the renderer reports is what the percentages should be measured against, but a
      // host that only renders a viewport can report less than the sum of the passes inside it. Then
      // the sum is the honest denominator instead of a table full of numbers over a hundred percent.
      const float frameBase = glm::max(frameGpu, passesGpu);

      ImGui::Text("Gpu per pass: passes %.2f ms, frame %.2f ms", passesGpu, frameGpu);

      const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders |
                                    ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
      if (ImGui::BeginTable("GpuPassTable", 3, flags))
      {
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Gpu (ms)", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("% of frame", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableHeadersRow();

        for (const auto& entry : sorted)
        {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(entry.first.c_str());
          ImGui::TableNextColumn();
          ImGui::Text("%.2f", entry.second);
          ImGui::TableNextColumn();
          const float pct = frameBase > 0.0f ? (entry.second / frameBase) * 100.0f : 0.0f;
          ImGui::Text("%.0f%%", glm::min(pct, 100.0f));
        }

        ImGui::EndTable();
      }
    }

    void StatsWindow::Show()
    {
      ImGui::SetNextWindowSize(ImVec2(750, 450), ImGuiCond_Once);
      if (ImGui::Begin(m_name.c_str(), &m_visible))
      {
        HandleStates();

        // Profiler enable checkbox.
        bool profilerEnabled = Profiler::IsProfilerEnabled();
        if (ImGui::Checkbox("Enable Profiler##ProfilerOn", &profilerEnabled))
        {
          Profiler::SetProfilerEnabled(profilerEnabled);
        }

        // The gpu half rides on the same switch and is refreshed every frame, not only when the
        // checkbox moves: the window can open with the profiler already enabled, and switching it is
        // a boolean assignment on the backend.
        const bool gpuTimingOn = Stats::EnablePassGpuTiming(profilerEnabled);

        UI::AddTooltipToLastItem("Enable the cpu profiler and the per pass gpu timings.\n"
                                 "Shows nested timing information and what each pass costs the gpu.");

        ImGui::Separator();

        // Viewport resolution.
        if (EditorViewportPtr viewport = GetApp()->GetViewport(g_3dViewport))
        {
          ImGui::Text("Viewport Resolution: %dx%d",
                      (int) viewport->m_wndContentAreaSize.x,
                      (int) viewport->m_wndContentAreaSize.y);
        }

        // Basic stats.
        String stats = Stats::GetPerFrameStats();
        ImGui::TextUnformatted(stats.c_str());

        ImGui::Separator();

        // One section for the whole frame: the gpu half first, then the cpu tree. They answer the same
        // question from two sides - what the frame costs, and how that cost was spent - so they read in
        // that order, biggest first.
        if (profilerEnabled)
        {
          if (gpuTimingOn)
          {
            DrawGpuPassTable();
          }
          else
          {
            ImGui::TextColored(UI::GetColor(EditorColor::ConsoleMemo),
                               "Gpu timing is unavailable: this backend has no timestamp queries.");
          }

          ImGui::Separator();

          if (TKProfiler* profiler = Profiler::GetProfiler())
          {
            const ProfilerNodeArray& roots = profiler->GetRootNodes();

            const float frameTime    = profiler->GetFrameTime();
            const float avgFrameTime = profiler->GetAverageFrameTime();
            const uint frameCount    = profiler->GetFrameCount();

            ImGui::Text("Profiler: Frame: %.3f ms | Avg: %.3f ms | Frames: %u", frameTime, avgFrameTime, frameCount);

            ImGui::Separator();

            if (!roots.empty())
            {
              const ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders |
                                                 ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                                 ImGuiTableFlags_SizingStretchProp;

              // Whatever is left of the window goes to the tree, which is the part that scrolls.
              const float availableHeight = ImGui::GetContentRegionAvail().y;

              if (ImGui::BeginTable("ProfilerTable", 6, tableFlags, ImVec2(0.0f, availableHeight)))
              {
                ImGui::TableSetupColumn("Scope Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                ImGui::TableSetupColumn("Incl (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Excl (ms)", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Incl %", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Excl %", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (ProfilerNode* root : roots)
                {
                  DrawProfilerNode(root, frameTime);
                }

                ImGui::EndTable();
              }
            }
            else
            {
              ImGui::TextColored(UI::GetColor(EditorColor::ConsoleWarning),
                                 "No profiling data. Use TK_PROFILE_SCOPE(\"name\") or Stats::BeginProfileScope().");
            }
          }
        }
        else
        {
          ImGui::TextColored(UI::GetColor(EditorColor::ConsoleMemo),
                             "Profiler is disabled. Enable it to see timing data.");
        }
      }
      ImGui::End();
    }

  } // namespace Editor
} // namespace ToolKit
