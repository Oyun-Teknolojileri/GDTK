/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "LeftBar.h"

#include "App.h"
#include "Mod.h"

namespace ToolKit
{
  namespace Editor
  {

    OverlayLeftBar::OverlayLeftBar(EditorViewport* owner) : OverlayUI(owner) {}

    void OverlayLeftBar::Show()
    {
      const float padding = 5.0f;
      Vec2 wndPos = Vec2(m_owner->m_contentAreaLocation.x + padding, m_owner->m_contentAreaLocation.y + padding);

      ImGui::SetNextWindowPos(wndPos);
      ImGui::SetNextWindowBgAlpha(0.65f);

      static ImVec2 overlaySize(48, 0); // Set initial height to 0
      if (ImGui::BeginChildFrame(ImGui::GetID("Navigation"),
                                 overlaySize,
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
      {
        SetOwnerState();

        // Select button.
        bool isCurrentMod = ModManager::GetInstance()->m_modStack.back()->m_id == ModId::Select;
        ModManager::GetInstance()->SetMod(
            UI::ToggleButton(EditorImGuiTextureCache::Acquire(UI::m_selectIcn), ImVec2(32, 32), isCurrentMod) &&
                !isCurrentMod,
            ModId::Select);
        UI::HelpMarker(TKLoc + m_owner->m_name, "Select Box\nSelect items using box selection.");

        // Cursor button.
        isCurrentMod = ModManager::GetInstance()->m_modStack.back()->m_id == ModId::Cursor;
        ModManager::GetInstance()->SetMod(
            UI::ToggleButton(EditorImGuiTextureCache::Acquire(UI::m_cursorIcn), ImVec2(32, 32), isCurrentMod) &&
                !isCurrentMod,
            ModId::Cursor);
        UI::HelpMarker(TKLoc + m_owner->m_name, "Cursor\nSet the cursor location.");
        ImGui::Separator();

        // Move button.
        isCurrentMod = ModManager::GetInstance()->m_modStack.back()->m_id == ModId::Move;
        ModManager::GetInstance()->SetMod(
            UI::ToggleButton(EditorImGuiTextureCache::Acquire(UI::m_moveIcn), ImVec2(32, 32), isCurrentMod) &&
                !isCurrentMod,
            ModId::Move);
        UI::HelpMarker(TKLoc + m_owner->m_name, "Move\nMove selected items.");

        // Rotate button.
        isCurrentMod = ModManager::GetInstance()->m_modStack.back()->m_id == ModId::Rotate;
        ModManager::GetInstance()->SetMod(
            UI::ToggleButton(EditorImGuiTextureCache::Acquire(UI::m_rotateIcn), ImVec2(32, 32), isCurrentMod) &&
                !isCurrentMod,
            ModId::Rotate);
        UI::HelpMarker(TKLoc + m_owner->m_name, "Rotate\nRotate selected items.");

        // Scale button.
        isCurrentMod = ModManager::GetInstance()->m_modStack.back()->m_id == ModId::Scale;
        ModManager::GetInstance()->SetMod(
            UI::ToggleButton(EditorImGuiTextureCache::Acquire(UI::m_scaleIcn), ImVec2(32, 32), isCurrentMod) &&
                !isCurrentMod,
            ModId::Scale);
        UI::HelpMarker(TKLoc + m_owner->m_name, "Scale\nScale (resize) selected items.");

        // Box Edit button.
        isCurrentMod = ModManager::GetInstance()->m_modStack.back()->m_id == ModId::BoxEdit;
        ModManager::GetInstance()->SetMod(
            UI::ToggleButton(EditorImGuiTextureCache::Acquire(UI::m_cubeIcon), ImVec2(32, 32), isCurrentMod) &&
                !isCurrentMod,
            ModId::BoxEdit);
        UI::HelpMarker(TKLoc + m_owner->m_name, "Box Edit\nEdit bounding volumes by dragging face handles.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        static bool isEditingSpeed  = false;
        static float editSpeedValue = 0.0f;
        float camSpeed              = GetApp()->m_camSpeed;

        ImVec2 contentRegionMin     = ImGui::GetWindowContentRegionMin();
        ImVec2 contentRegionMax     = ImGui::GetWindowContentRegionMax();
        float contentWidth          = contentRegionMax.x - contentRegionMin.x;
        float contentHeight         = contentRegionMax.y - contentRegionMin.y;
        float inputWidth            = 40.0f;

        if (isEditingSpeed)
        {
          float startX = contentRegionMin.x + (contentWidth - inputWidth) * 0.5f;
          ImGui::SetCursorPosX(startX);
          ImGui::PushItemWidth(inputWidth);
          ImGui::SetKeyboardFocusHere();
          ImGui::InputFloat("##speedInput", &editSpeedValue, 0.0f, 0.0f, "%.1f", ImGuiInputTextFlags_AutoSelectAll);

          if (ImGui::IsItemDeactivatedAfterEdit())
          {
            if (editSpeedValue >= 0.0f && editSpeedValue <= 1000.0f)
            {
              GetApp()->m_camSpeed = editSpeedValue;
            }
            isEditingSpeed = false;
          }
          else if (ImGui::IsItemDeactivated())
          {
            isEditingSpeed = false;
          }
          ImGui::PopItemWidth();
        }
        else
        {
          char speedText[16];
          snprintf(speedText, sizeof(speedText), "%.1f", camSpeed);
          float textWidth   = ImGui::CalcTextSize(speedText).x;
          float startX      = contentRegionMin.x + (contentWidth - textWidth) * 0.5f;
          float frameHeight = ImGui::GetFrameHeight();
          ImGui::SetCursorPosX(startX);

          if (ImGui::Selectable(speedText, false, 0, ImVec2(textWidth, frameHeight)))
          {
            isEditingSpeed = true;
            editSpeedValue = camSpeed;
          }
          if (ImGui::IsItemHovered())
          {
            ImGui::SetTooltip("Camera Speed: %.1f units/sec\nClick to edit, Right click + scroll to adjust", camSpeed);
          }
        }
      }
      ImGui::EndChildFrame();
    }

  } // namespace Editor
} // namespace ToolKit