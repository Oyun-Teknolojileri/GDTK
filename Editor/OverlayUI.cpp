/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#include "OverlayUI.h"

namespace ToolKit
{
  namespace Editor
  {

    OverlayUI::OverlayUI(EditorViewport* owner) { m_owner = owner; }

    OverlayUI::~OverlayUI() {}

    void OverlayUI::SetOwnerState()
    {
      if (m_owner && m_owner->IsActive() && m_owner->IsVisible())
      {
        if (ImGui::IsWindowHovered())
        {
          m_owner->m_mouseOverOverlay = true;
        }
      }
    }

  } // namespace Editor
} // namespace ToolKit
