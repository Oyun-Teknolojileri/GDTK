/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "AnimationView.h"

#include "UI.h"

#include <Animation.h>
#include <FileManager.h>

#include <algorithm>
#include <cctype>

namespace ToolKit
{
  namespace Editor
  {

    // AnimationView
    //////////////////////////////////////////

    AnimationView::AnimationView() : View("Animation View")
    {
      m_viewID  = 5;
      m_viewIcn = UI::m_clipIcon;
    }

    AnimationView::~AnimationView() { m_animation = nullptr; }

    void AnimationView::SetAnimation(AnimationPtr anim) { m_animation = anim; }

    void AnimationView::Show()
    {
      if (m_animation == nullptr)
      {
        ImGui::Text("No animation loaded.");
        return;
      }

      // Fixed header (does not scroll).
      String name, ext, path;
      DecomposePath(m_animation->GetFile(), &path, &name, &ext);
      UI::HeaderText(name.c_str());
      GetFileManager()->GetRelativeResourcesPath(path);
      UI::HelpMarker(TKLoc, path.c_str());

      const String& rootKey = m_animation->m_rootKey;
      ImGui::Text("FPS: %.2f   Duration: %.3f s   Root Key: %s",
                  m_animation->m_fps,
                  m_animation->m_duration,
                  rootKey.empty() ? "(none)" : rootKey.c_str());

      ImGui::InputText("Search", m_searchBuf, sizeof(m_searchBuf));

      ImGui::Separator();

      if (m_animation->m_keys.empty())
      {
        ImGui::Text("Animation has no keys.");
        return;
      }

      // Case insensitive filter.
      String filter = m_searchBuf;
      std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) -> char
                     { return (char) std::tolower(c); });

      // Key grid. Only the grid scrolls; header stays fixed above.
      if (ImGui::BeginChild("##animKeyScroll", Vec2(0.0f, 0.0f), true))
      {
        if (ImGui::BeginTable("##animKeyTable",
                              4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_Sortable))
        {
          ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 30.0f);
          ImGui::TableSetupColumn("KeyName", ImGuiTableColumnFlags_WidthStretch);
          ImGui::TableSetupColumn("KeyCount", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 60.0f);
          ImGui::TableSetupColumn("IsRoot", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 70.0f);
          ImGui::TableHeadersRow();

          // Key names in map (read) order; sorted only when the user clicks a
          // sortable column header (KeyName).
          std::vector<String> sortedKeys;
          sortedKeys.reserve(m_animation->m_keys.size());
          for (const auto& kv : m_animation->m_keys)
          {
            sortedKeys.push_back(kv.first);
          }

          bool doSort = false;
          bool sortAscending = true;
          if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
          {
            if (sortSpecs->SpecsCount > 0 && sortSpecs->Specs[0].ColumnIndex == 1)
            {
              doSort        = true;
              sortAscending = (sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
            }
          }

          if (doSort)
          {
            std::sort(sortedKeys.begin(), sortedKeys.end(), [sortAscending](const String& a, const String& b) -> bool
                      {
                        const int cmp = a.compare(b);
                        return sortAscending ? (cmp < 0) : (cmp > 0);
                      });
          }

          int rowNo = 1;
          for (const String& keyName : sortedKeys)
          {
            if (!filter.empty())
            {
              String keyLower = keyName;
              std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(), [](unsigned char c) -> char
                             { return (char) std::tolower(c); });
              if (keyLower.find(filter) == String::npos)
              {
                continue;
              }
            }

            const KeyArray& keys = m_animation->m_keys.at(keyName);

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", rowNo++);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", keyName.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%zu", keys.size());

            ImGui::TableSetColumnIndex(3);
            bool isRoot = (m_animation->m_rootKey == keyName);
            String cbId = "##root_" + keyName;
            if (ImGui::Checkbox(cbId.c_str(), &isRoot))
            {
              if (isRoot)
              {
                m_animation->m_rootKey = keyName;
              }
              else
              {
                m_animation->m_rootKey.clear();
              }
              m_animation->m_dirty = true;
              m_animation->Save(true);
            }
          }

          ImGui::EndTable();
        }
      }
      ImGui::EndChild();
    }

    // AnimationWindow
    //////////////////////////////////////////

    TKDefineClass(AnimationWindow, Window);

    AnimationWindow::AnimationWindow()
    {
      m_view               = MakeNewPtr<AnimationView>();
      m_view->m_isTempView = true;
    }

    AnimationWindow::~AnimationWindow() { m_view = nullptr; }

    void AnimationWindow::SetAnimation(AnimationPtr anim) { m_view->SetAnimation(anim); }

    void AnimationWindow::Show()
    {
      ObjectId wndId  = GetIdVal();
      String strWndId = "Animation View##" + std::to_string(wndId);

      ImGuiIO io      = ImGui::GetIO();
      ImGui::SetNextWindowSize(Vec2(400.0f, 700.0f), ImGuiCond_Once);
      ImGui::SetNextWindowPos(Vec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Once, Vec2(0.5f, 0.5f));

      if (ImGui::Begin(strWndId.c_str(), &m_visible))
      {
        HandleStates();
        m_view->Show();
      }
      ImGui::End();
    }

  } // namespace Editor
} // namespace ToolKit
