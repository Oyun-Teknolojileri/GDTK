/*
 * Copyright (c) 2019-2026 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otsoftware.tr] or contact us at [info@otsoftare.tr].
 */
#pragma once

#include "Window.h"

namespace ToolKit
{
  namespace Editor
  {

    /**
     * Scene level settings. Post processing belongs here rather than next to the graphics settings the
     * engine keeps, because a scene stores its own copy: a window that edits it is editing the scene, and
     * a window named after the engine would suggest the values are a property of the engine.
     */
    class TK_EDITOR_API WorldSettingsWindow : public Window
    {
     public:
      TKDeclareClass(WorldSettingsWindow, Window);

      WorldSettingsWindow();
      virtual ~WorldSettingsWindow();
      void Show() override;

     protected:
      /** Draws one collapsing section and reports whether it is open. */
      bool ShowSection(const char* label);
    };

  } // namespace Editor
} // namespace ToolKit
