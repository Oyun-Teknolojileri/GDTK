/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */

#include "SkeletonView.h"

#include "UI.h"

#include <FileManager.h>
#include <Node.h>

namespace ToolKit
{
  namespace Editor
  {

    // SkeletonView
    //////////////////////////////////////////

    SkeletonView::SkeletonView() : View("Skeleton View")
    {
      m_viewID  = 4;
      m_viewIcn = UI::m_armatureIcon;
    }

    SkeletonView::~SkeletonView() { m_skeleton = nullptr; }

    void SkeletonView::SetSkeleton(SkeletonPtr skeleton) { m_skeleton = skeleton; }

    void SkeletonView::ShowBone(const DynamicBoneMap::DynamicBone* dBone)
    {
      const StaticBone* sBone = m_skeleton->m_bones[dBone->boneIndx];
      const bool hasChildren  = !dBone->node->m_children.empty();

      ImGuiTreeNodeFlags flags = 0;
      if (!hasChildren)
      {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
      }

      const bool open = ImGui::TreeNodeEx(sBone->m_name.c_str(), flags);

      if (open && hasChildren)
      {
        for (Node* childNode : dBone->node->m_children)
        {
          for (auto& kv : m_skeleton->m_Tpose.m_boneMap)
          {
            if (kv.second.node == childNode)
            {
              ShowBone(&kv.second);
              break;
            }
          }
        }
        ImGui::TreePop();
      }
    }

    void SkeletonView::Show()
    {
      if (m_skeleton == nullptr)
      {
        ImGui::Text("No skeleton loaded.");
        return;
      }

      String name, ext, path;
      DecomposePath(m_skeleton->GetFile(), &path, &name, &ext);
      UI::HeaderText(name.c_str());
      GetFileManager()->GetRelativeResourcesPath(path);
      UI::HelpMarker(TKLoc, path.c_str());

      ImGui::Separator();

      if (ImGui::BeginChild("##skeletonTree", Vec2(0.0f, 0.0f), true))
      {
        auto rootBoneFn = [this](const DynamicBoneMap::DynamicBone* dBone) -> void { ShowBone(dBone); };
        m_skeleton->m_Tpose.ForEachRootBone(rootBoneFn);
      }
      ImGui::EndChild();
    }

    // SkeletonWindow
    //////////////////////////////////////////

    TKDefineClass(SkeletonWindow, Window);

    SkeletonWindow::SkeletonWindow()
    {
      m_view               = MakeNewPtr<SkeletonView>();
      m_view->m_isTempView = true;
    }

    SkeletonWindow::~SkeletonWindow() { m_view = nullptr; }

    void SkeletonWindow::SetSkeleton(SkeletonPtr skeleton) { m_view->SetSkeleton(skeleton); }

    void SkeletonWindow::Show()
    {
      ObjectId wndId  = GetIdVal();
      String strWndId = "Skeleton View##" + std::to_string(wndId);

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
