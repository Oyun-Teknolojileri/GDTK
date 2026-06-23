/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "View.h"

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API PrefabView : public View
    {
     public:
      PrefabView();
      virtual ~PrefabView();
      virtual void Show();
      bool HasActiveEntity() const;
      EntityPtr GetActiveEntity();

     private:
      bool DrawHeader(EntityPtr ntt, ImGuiTreeNodeFlags flags);
      void ShowNode(EntityPtr e);

     public:
      EntityPtr m_activeChildEntity = nullptr;
    };

  } // namespace Editor
} // namespace ToolKit