/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */

#pragma once

#include "EditorViewport.h"

namespace ToolKit
{
  namespace Editor
  {

    class TK_EDITOR_API OverlayUI
    {
     public:
      explicit OverlayUI(EditorViewport* owner);
      virtual ~OverlayUI();
      virtual void Show() = 0;

      void SetOwnerState();

     public:
      EditorViewport* m_owner;
    };

  } //  namespace Editor
} //  namespace ToolKit
